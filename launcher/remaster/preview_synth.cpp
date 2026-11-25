// preview_synth.cpp - ver preview_synth.h
#include "preview_synth.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

extern "C" {
#include "../../game/third_party/tsf.h"
}

namespace ecr {

namespace {

constexpr int kRenderBlock = 512;
constexpr float kBridgeGain = 0.55f;
constexpr float kClipThresh = 0.95f;

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

float gain_and_soft_clip(float sample) {
    float x = sample * kBridgeGain;
    if (!std::isfinite(x)) return 0.0f;
    float ax = std::fabs(x);
    if (ax > kClipThresh) {
        const float head = 1.0f - kClipThresh;
        float over = (ax - kClipThresh) / head;
        float limited = kClipThresh + head * std::tanh(over);
        x = (x >= 0.0f) ? limited : -limited;
    }
    return x;
}

int auto_preset(int key, double dur_sec) {
    if (key <= 48) return 33;
    if (key >= 67) return 81;
    if (dur_sec >= 0.28) return 49;
    return 62;
}

struct Ev {
    int32_t pos;
    uint32_t seq;
    uint8_t on, key, vel;
    int16_t preset;
};

struct Chan {
    ChannelMap map;
    const std::vector<Note>* notes = nullptr;
    std::vector<Ev> ev;
    size_t idx = 0;
    tsf* inst = nullptr;
    int cur_auto = -1;
    bool audible = false;
    std::atomic<float> level{0.0f};
};

int mode_int(const std::string& m) { return m == "gm" ? 0 : (m == "percussion" ? 1 : (m == "auto" ? 2 : -1)); }

}  // namespace

// ---------------------------------------------------------------------------
// Engine: estado de render (instancias tsf por canal)
// ---------------------------------------------------------------------------

class Engine {
public:
    Engine(tsf* font, const SongNotes& song, const std::vector<ChannelMap>& maps)
        : font_(font), song_(song) {
        total_ = std::max<int64_t>(kPreviewRate, (int64_t)std::nearbyint(song_.duration_s * kPreviewRate));
        for (const ChannelMap& m : maps) {
            auto c = std::make_unique<Chan>();
            c->map = m;
            bind_notes(*c);
            chans_.push_back(std::move(c));
        }
        for (auto& c : chans_) rebuild(*c);
        refresh_audible();
    }
    ~Engine() {
        for (auto& c : chans_) if (c->inst) tsf_close(c->inst);
    }

    int64_t total() const { return total_; }
    int64_t pos() const { return pos_; }
    double duration_s() const { return song_.duration_s; }
    const std::vector<std::unique_ptr<Chan>>& chans() const { return chans_; }
    void set_master_gain_db(double db) { master_ = (float)std::pow(10.0, db / 20.0); }

    void seek(int64_t f) {
        pos_ = std::max<int64_t>(0, std::min(f, total_));
        for (auto& c : chans_) restart(*c);
    }

    void update_map(const ChannelMap& m) {
        Chan* c = nullptr;
        for (auto& x : chans_) if (x->map.key == m.key) { c = x.get(); break; }
        if (!c) return;
        const ChannelMap& o = c->map;
        bool resynth = o.mode != m.mode || o.bank != m.bank || o.program != m.program ||
                       o.transpose != m.transpose || o.drum_note != m.drum_note;
        c->map = m;
        if (resynth) {
            rebuild(*c);
            restart(*c);
        }
        refresh_audible();
    }

    // Soma `n` quadros a partir de pos_ em `out` (estereo intercalado, ja zerado pelo chamador).
    void render(float* out, int n) {
        const int64_t start = pos_, end = pos_ + n;
        for (auto& c : chans_) render_chan(*c, out, start, end);
        if (master_ != 1.0f) for (int i = 0; i < n * 2; ++i) out[i] *= master_;
        pos_ = end;
    }

private:
    void bind_notes(Chan& c) {
        c.notes = nullptr;
        if (c.map.is_voice) {
            if (const VoiceTrack* v = song_.find_voice(c.map.key)) c.notes = &v->q;
        } else if (const ChanTrack* t = song_.find_channel(c.map.key)) {
            c.notes = &t->q;
        }
    }

    // Gera os eventos como tsf_bridge_render_channel.
    void rebuild(Chan& c) {
        c.ev.clear();
        c.idx = 0;
        const int mode = mode_int(c.map.mode);
        if (c.inst) { tsf_close(c.inst); c.inst = nullptr; }
        if (mode < 0 || !c.notes || c.notes->empty()) return;
        const int drum = (mode == 1 && c.map.is_voice) ? c.map.drum_note : 0;
        uint32_t seq = 0;
        const double sr = kPreviewRate;
        const double clamp_hi = (double)total_ / sr + 1.0;
        for (const Note& n : *c.notes) {
            double s = n.start_s, e = n.end_s();
            if (!std::isfinite(s) || !std::isfinite(e) || e <= s) continue;
            double dur = e - s;
            s = std::min(std::max(s, 0.0), clamp_hi);
            e = std::min(std::max(e, 0.0), clamp_hi);
            int note = drum > 0 ? drum : n.note;
            int key = clampi(note + (mode == 1 ? 0 : clampi(c.map.transpose, -48, 48)), 0, 127);
            int vel = clampi(n.vel, 1, 127);
            int64_t on = std::llround(s * sr), off = std::llround(e * sr);
            if (off <= 0 || on >= total_) continue;
            if (on < 0) on = 0;
            if (off <= on) off = on + 1;
            int16_t preset = (int16_t)(mode == 2 ? auto_preset(key, dur) : clampi(c.map.program, 0, 127));
            c.ev.push_back({(int32_t)on, seq++, 1, (uint8_t)key, (uint8_t)vel, preset});
            if (off < total_) c.ev.push_back({(int32_t)off, seq++, 0, (uint8_t)key, 0, preset});
        }
        std::sort(c.ev.begin(), c.ev.end(), [](const Ev& a, const Ev& b) {
            if (a.pos != b.pos) return a.pos < b.pos;
            if (a.on != b.on) return a.on < b.on;
            return a.seq < b.seq;
        });
        if (c.ev.empty()) return;
        c.inst = tsf_copy(font_);
        if (!c.inst) return;
        tsf_set_output(c.inst, TSF_STEREO_INTERLEAVED, kPreviewRate, 0.0f);
        tsf_set_max_voices(c.inst, 64);
        configure(c);
    }

    void configure(Chan& c) {
        if (!c.inst) return;
        const int mode = mode_int(c.map.mode);
        const int bank = clampi(c.map.bank, 0, 128), prog = clampi(c.map.program, 0, 127);
        c.cur_auto = -1;
        if (mode == 1) {
            if (!tsf_channel_set_bank_preset(c.inst, 0, 128, prog)) tsf_channel_set_bank_preset(c.inst, 0, 128, 0);
        } else if (mode == 0) {
            if (!tsf_channel_set_bank_preset(c.inst, 0, bank, prog)) tsf_channel_set_bank_preset(c.inst, 0, 0, prog);
        } else {
            tsf_channel_set_bank_preset(c.inst, 0, 0, 33);
            c.cur_auto = 33;
        }
    }

    // zera as vozes e posiciona o indice de eventos em pos_
    void restart(Chan& c) {
        if (c.inst) {
            tsf_reset(c.inst);
            configure(c);
        }
        c.idx = (size_t)(std::lower_bound(c.ev.begin(), c.ev.end(), pos_,
                                          [](const Ev& e, int64_t p) { return (int64_t)e.pos < p; }) -
                         c.ev.begin());
    }

    void refresh_audible() {
        bool any_solo = false;
        for (auto& c : chans_) if (c->map.synth() && c->map.solo) any_solo = true;
        for (auto& c : chans_) {
            bool a = c->map.synth() && !c->map.mute && (!any_solo || c->map.solo) && c->inst != nullptr;
            if (a != c->audible) {
                c->audible = a;
                restart(*c);  // ao silenciar: corta as vozes; ao liberar: recomeca do ponto atual
            }
        }
    }

    void render_chan(Chan& c, float* out, int64_t start, int64_t end) {
        if (!c.inst || !c.audible) {
            while (c.idx < c.ev.size() && (int64_t)c.ev[c.idx].pos < end) ++c.idx;
            c.level.store(0.0f);
            return;
        }
        const int mode = mode_int(c.map.mode);
        const double vdb = std::isfinite(c.map.volume_db) ? std::max(-60.0, std::min(24.0, c.map.volume_db)) : 0.0;
        const float g = (float)std::pow(10.0, vdb / 20.0);
        const float pan = (float)std::max(-1.0, std::min(1.0, c.map.pan));
        const float gl = g * (1.0f - std::max(0.0f, pan)), gr = g * (1.0f - std::max(0.0f, -pan));
        float tmp[kRenderBlock * 2];
        float peak = 0.0f;
        int64_t fp = start;
        while (fp < end) {
            while (c.idx < c.ev.size() && (int64_t)c.ev[c.idx].pos <= fp) {
                const Ev& e = c.ev[c.idx++];
                if (!e.on) {
                    tsf_channel_note_off(c.inst, 0, e.key);
                } else {
                    if (mode == 2 && c.cur_auto != e.preset) {
                        if (!tsf_channel_set_bank_preset(c.inst, 0, 0, e.preset))
                            tsf_channel_set_bank_preset(c.inst, 0, 0, 0);
                        c.cur_auto = e.preset;
                    }
                    tsf_channel_note_on(c.inst, 0, e.key, (float)e.vel / 127.0f);
                }
            }
            int64_t next = c.idx < c.ev.size() ? std::min<int64_t>(c.ev[c.idx].pos, end) : end;
            int64_t remain = next - fp;
            while (remain > 0) {
                int chunk = (int)std::min<int64_t>(remain, kRenderBlock);
                tsf_render_float(c.inst, tmp, chunk, 0);
                float* dst = out + (fp - start) * 2;
                for (int k = 0; k < chunk; ++k) {
                    float l = gain_and_soft_clip(tmp[2 * k]) * gl;
                    float r = gain_and_soft_clip(tmp[2 * k + 1]) * gr;
                    dst[2 * k] += l;
                    dst[2 * k + 1] += r;
                    peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
                }
                fp += chunk;
                remain -= chunk;
            }
        }
        c.level.store(peak);
    }

    tsf* font_;
    SongNotes song_;
    int64_t total_ = 0, pos_ = 0;
    float master_ = 1.0f;
    std::vector<std::unique_ptr<Chan>> chans_;
};

// ---------------------------------------------------------------------------
// PreviewSynth
// ---------------------------------------------------------------------------

struct PreviewSynth::Impl {
    tsf* font = nullptr;
    std::unique_ptr<Engine> eng;
    SongNotes song;
    std::vector<ChannelMap> maps;
    bool loop = false;
    bool done = false;
    double master_db = 0.0;
    mutable std::mutex mu;
};

PreviewSynth::PreviewSynth() : p_(new Impl) {}
PreviewSynth::~PreviewSynth() {
    p_->eng.reset();
    if (p_->font) tsf_close(p_->font);
}

bool PreviewSynth::load_soundfont(const std::string& path, std::string* err) {
    std::lock_guard<std::mutex> lk(p_->mu);
    tsf* f = tsf_load_filename(path.c_str());
    if (!f) {
        if (err) *err = "SoundFont nao encontrado/invalido: " + path;
        return false;
    }
    tsf_set_max_voices(f, 96);
    p_->eng.reset();
    if (p_->font) tsf_close(p_->font);
    p_->font = f;
    return true;
}

bool PreviewSynth::has_soundfont() const { return p_->font != nullptr; }

void PreviewSynth::set_song(const SongNotes& song, const std::vector<ChannelMap>& maps) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->song = song;
    p_->maps = maps;
    p_->done = false;
    p_->eng.reset();
    if (p_->font) {
        p_->eng = std::make_unique<Engine>(p_->font, p_->song, p_->maps);
        p_->eng->set_master_gain_db(p_->master_db);
    }
}

void PreviewSynth::update_map(const ChannelMap& m) {
    std::lock_guard<std::mutex> lk(p_->mu);
    for (auto& x : p_->maps) if (x.key == m.key) { x = m; break; }
    if (p_->eng) p_->eng->update_map(m);
}

void PreviewSynth::set_master_gain_db(double db) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->master_db = db;
    if (p_->eng) p_->eng->set_master_gain_db(db);
}

std::vector<ChannelMap> PreviewSynth::maps() const {
    std::lock_guard<std::mutex> lk(p_->mu);
    return p_->maps;
}

int64_t PreviewSynth::total_frames() const {
    std::lock_guard<std::mutex> lk(p_->mu);
    return p_->eng ? p_->eng->total() : 0;
}
double PreviewSynth::duration_s() const { return (double)total_frames() / kPreviewRate; }
int64_t PreviewSynth::position_frames() const {
    std::lock_guard<std::mutex> lk(p_->mu);
    return p_->eng ? p_->eng->pos() : 0;
}
void PreviewSynth::seek_frames(int64_t f) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->done = false;
    if (p_->eng) p_->eng->seek(f);
}
void PreviewSynth::set_loop(bool l) {
    std::lock_guard<std::mutex> lk(p_->mu);
    p_->loop = l;
}
bool PreviewSynth::finished() const {
    std::lock_guard<std::mutex> lk(p_->mu);
    return p_->done;
}

void PreviewSynth::render_block(float* out, int frames) {
    std::lock_guard<std::mutex> lk(p_->mu);
    std::memset(out, 0, (size_t)frames * 2 * sizeof(float));
    if (!p_->eng || p_->done) return;
    int done = 0;
    while (done < frames) {
        if (p_->eng->pos() >= p_->eng->total()) {
            if (p_->loop) p_->eng->seek(0);
            else { p_->done = true; break; }
        }
        int n = (int)std::min<int64_t>(frames - done, p_->eng->total() - p_->eng->pos());
        p_->eng->render(out + (size_t)done * 2, n);
        done += n;
    }
}

void PreviewSynth::render_all(std::vector<float>* stereo) const {
    std::lock_guard<std::mutex> lk(p_->mu);
    stereo->clear();
    if (!p_->font) return;
    Engine e(p_->font, p_->song, p_->maps);
    e.set_master_gain_db(p_->master_db);
    stereo->assign((size_t)e.total() * 2, 0.0f);
    e.render(stereo->data(), (int)e.total());
}

float PreviewSynth::channel_level(const std::string& key) const {
    std::lock_guard<std::mutex> lk(p_->mu);
    if (!p_->eng) return 0.0f;
    for (auto& c : p_->eng->chans()) if (c->map.key == key) return c->level.load();
    return 0.0f;
}

// ---------------------------------------------------------------------------
// build_channel_maps
// ---------------------------------------------------------------------------

std::vector<ChannelMap> build_channel_maps(const SongNotes& song, const VoicesFile& voices,
                                           const ProfileFile& profile) {
    std::vector<ChannelMap> out;
    std::vector<const VoiceTrack*> vs;
    for (auto& v : song.voices) vs.push_back(&v);
    std::stable_sort(vs.begin(), vs.end(), [](const VoiceTrack* a, const VoiceTrack* b) {
        if (a->q.size() != b->q.size()) return a->q.size() > b->q.size();
        return a->hash < b->hash;
    });
    for (const VoiceTrack* v : vs) {
        ChannelMap m;
        m.key = v->hash;
        m.is_voice = true;
        VoiceEdit e;
        const VoiceOverride* ov = profile.find_override(v->hash);
        if (ov && ov->edit.mode != "auto") {
            e = ov->edit;
        } else if (const VoiceEntry* ve = voices.find(v->hash)) {
            e = ve->edit;
        } else {
            Suggestion sg = suggest(v->patch, v->notes);
            e.mode = sg.mode;
            e.program = sg.program;
            e.drum_note = sg.mode == "percussion" ? suggest_drum_note(v->notes) : 0;
        }
        m.mode = e.mode;
        m.program = e.program;
        m.bank = e.mode == "percussion" ? 128 : e.bank;
        m.transpose = e.transpose;
        m.volume_db = e.volume_db;
        m.drum_note = e.drum_note;
        out.push_back(m);
    }
    for (const ChanTrack& ct : song.channels) {
        const ProfileChannel* pc = profile.find_channel(ct.name);
        if (!pc) continue;
        ChannelMap m;
        m.key = ct.name;
        m.mode = pc->mode;
        m.bank = pc->bank;
        m.program = pc->program;
        m.transpose = pc->transpose;
        m.volume_db = pc->volume_db;
        m.pan = pc->pan;
        m.mute = pc->mute;
        m.solo = pc->solo;
        out.push_back(m);
    }
    return out;
}

}  // namespace ecr
