// ESTÚDIO DE REMASTER (PLAN §10): mapeia os instrumentos FM do jogo (voices.toml) e os canais PSG/DAC por musica,
// com previa sintetizada, A/B com o original, solo/mudo, desfazer/refazer, salvar (atomico, .bak) e "Testar no jogo".
// Navegacao: D-pad/teclado/mouse. Zonas: barra (transporte) -> lista de instrumentos -> editor.
#include "../studio.h"
#include "../widgets.h"
#include "fm_view.h"
#include "screens.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fs = std::filesystem;

StudioPaths make_studio_paths(App& app) {
    StudioPaths p;
    p.root = app.root_dir;
    p.voices = app.studio_voices.empty() ? app.root_dir / "assets" / "audio_remaster" / "voices.toml" : utf8_to_path(app.studio_voices);
    p.profiles_dir = app.root_dir / "assets" / "audio_remaster" / "profiles";
    std::string sf = app.cfg.get("ec.audio.sf2");
    fs::path sp = sf.empty() ? fs::path("assets/soundfonts/TimGM6mb.sf2") : utf8_to_path(sf);   // mesmo default do mixer_studio.py
    p.sf2 = sp.is_absolute() ? sp : app.root_dir / sp;
    p.cache_dir = app.studio_dumps.empty() ? app.assets_dir.parent_path() / "remaster_cache" : utf8_to_path(app.studio_dumps);
    p.run.exe = path_to_utf8(app.game_exe);
    p.run.rom = app.rom_path;
    p.run.work_dir = path_to_utf8(app.root_dir);
    return p;
}

namespace {

enum Zone { Z_BAR, Z_LIST, Z_EDIT };
enum EId { E_NAME, E_MODE, E_PROG, E_BANK, E_TRANSP, E_VOL, E_DRUM, E_SCOPE, E_SOLO, E_MUTE, E_SUGGEST, E_RESTORE, E_PATCH, E_LISTEN, E_INFO, E_HEAD };

struct LRow { int kind = 0; std::string key; };           // 0 instrumento FM, 1 canal, 2 cabecalho
struct ERow { int id = E_INFO; std::string label, value; bool enabled = true, selectable = true, arrows = false; Color tint{255, 255, 255, 0}; };

const char* kBarLabels[] = {"▶ PLAY", "⟲ LOOP", "A/B", "SALVAR", "TESTAR NO JOGO", "◄ DESFAZER", "REFAZER ►", "BIBLIOTECA"};
constexpr int kBarN = 8;
const char* kModes[] = {"gm", "percussion", "chip", "off"};

std::string mode_label(const ecr::ChannelMap& m) {
    if (m.mode == "gm") return "GM " + std::to_string(m.program) + "  " + ecr::gm_program_name(m.program);
    if (m.mode == "percussion") return m.drum_note ? "PERC " + std::to_string(m.drum_note) + "  " + ecr::gm_drum_name(m.drum_note) : std::string("PERC (nota da trilha)");
    if (m.mode == "chip") return "CHIP";
    if (m.mode == "off") return "OFF";
    return "AUTO";
}

std::string fmt_time(double s) {
    int t = (int)s; char b[24]; snprintf(b, sizeof b, "%02d:%02d", t / 60, t % 60); return b;
}

class StudioScreen : public Screen {
public:
    void on_enter(App& app) override {
        st_ = std::make_unique<Studio>(make_studio_paths(app));
        if (app.shot_mode) {   // captura de tela: espera o carregamento (sem audio nem jobs reais)
            for (int i = 0; i < 400 && (st_->stage() == Studio::Stage::Loading); ++i) { st_->poll(); SDL_Delay(25); }
            st_->poll();
            library_ = app.shot_page == 1;
            list_focus_ = app.shot_focus;
            if (app.shot_state == 4) { fake_extract_ = true; }
            if (st_->stage() == Studio::Stage::Ready) {
                rebuild_list();
                if (app.shot_state == 1) zone_ = Z_EDIT;
                if (app.shot_state == 2) { zone_ = Z_EDIT; patch_view_ = true; }
                if (app.shot_state == 3) {
                    st_->debug_set_orig_job(JobState{true, 0.45f, "Música 8: capturando o áudio original (jogo headless em turbo)…", ""});
                    st_->set_ab_original(false);
                }
                if (app.shot_state == 5) zone_ = Z_BAR;
            }
        }
    }
    void on_resume(App&) override {}
    ~StudioScreen() override { if (st_) { st_->play(false); st_->close_audio_device(); } }

    void update(App& app, const InputState& in, float dt) override {
        t_ += dt;
        if (msg_t_ > 0) msg_t_ -= dt;
        st_->poll();
        const Studio::Stage stg = st_->stage();
        if (stg != Studio::Stage::Ready) { update_prestage(app, in); return; }
        rebuild_list();
        if (text_edit_) { update_text(app, in); return; }

        // ---- atalhos
        if (in.new_key[SDL_SCANCODE_SPACE]) toggle_play();
        if (in.ctrl && in.new_key[SDL_SCANCODE_Z]) st_->undo();
        if (in.ctrl && in.new_key[SDL_SCANCODE_Y]) st_->redo();
        if (in.p(A_SELECT)) do_save(app);
        if (in.p(A_START)) do_test(app);
        if (in.p(A_TABPREV)) change_song(-1);
        if (in.p(A_TABNEXT)) change_song(+1);
        if (in.p(A_HELP)) show_help_ = !show_help_;
        if (!in.ctrl) {
            if (in.new_key[SDL_SCANCODE_M]) toggle_mute();
            if (in.new_key[SDL_SCANCODE_S]) toggle_solo();
            if (in.new_key[SDL_SCANCODE_L]) { loop_ = !loop_; st_->set_loop(loop_); }
            if (in.new_key[SDL_SCANCODE_B]) toggle_ab(app);
        }
        if (in.p(A_BACK) || in.mouse_right_click) {
            if (zone_ == Z_EDIT) { zone_ = Z_LIST; return; }
            if (st_->orig_job().running) { st_->cancel_jobs(); return; }
            if (st_->dirty()) {
                app.push(make_confirm("ALTERAÇÕES NÃO SALVAS", "Há edições não salvas no voices.toml/perfil. Sair e descartar?", "Descartar", [](App& a) { a.pop(); }));
                return;
            }
            app.pop(); return;
        }
        mouse(app, in);
        // ---- navegacao por zonas
        if (zone_ == Z_BAR) {
            if (in.p(A_LEFT)) bar_focus_ = (bar_focus_ + kBarN - 1) % kBarN;
            if (in.p(A_RIGHT)) bar_focus_ = (bar_focus_ + 1) % kBarN;
            if (in.p(A_DOWN)) zone_ = Z_LIST;
            if (in.p(A_ACCEPT)) bar_action(app, bar_focus_);
        } else if (zone_ == Z_LIST) {
            if (in.p(A_UP)) { if (!move_list(-1)) zone_ = Z_BAR; }
            if (in.p(A_DOWN)) move_list(+1);
            if (in.p(A_RIGHT) || in.p(A_ACCEPT)) { zone_ = Z_EDIT; build_edit(app); edit_focus_ = first_selectable(); }
            if (in.p(A_LEFT)) zone_ = Z_BAR;
            if (in.p(A_RESET)) restore_selected();
        } else {
            build_edit(app);
            if (in.p(A_UP)) move_edit(-1);
            if (in.p(A_DOWN)) move_edit(+1);
            if (in.p(A_LEFT)) edit_adjust(app, -1);
            else if (in.p(A_RIGHT)) edit_adjust(app, +1);
            else if (in.p(A_ACCEPT)) edit_activate(app);
            else if (in.p(A_RESET)) restore_selected();
        }
        // seek por teclado
        if (in.new_key[SDL_SCANCODE_HOME]) st_->seek_seconds(0);
    }

    void draw(App& app) override {
        Render& rd = app.render; Theme& th = app.theme;
        th.draw_stone_bg(rd, app.time);
        draw_header(app, "ESTÚDIO DE REMASTER", CANVAS_W / 2.f, 0, 0.62f);
        if (st_->stage() != Studio::Stage::Ready) { draw_prestage(app); draw_help_bar(app, "Esc / B: voltar"); return; }

        const ecr::BgmInfo* bi = ecr::bgm_by_id(st_->current_bgm());
        // ---- seletor de musica
        {
            Rect r{40, 58, 560, 34};
            rd.fill(r, Color::hex(0x07111f, 215)); rd.line_rect(r, Color::hex(0x6aa9b0, 160), 1);
            TextStyle st = th.item_style(true, true); st.scale = 0.72f; st.align = Align::Center;
            draw_text(rd, *th.fonts.menu, r.x + r.w / 2, r.y + 4, "◄  " + std::string(bi ? bi->title : "?") + "  ►", st);
            TextStyle hs = th.help_style();
            draw_text(rd, *th.fonts.help, r.x + r.w + 14, r.y + 6, "LB/RB: música " + std::to_string(song_index() + 1) + "/" + std::to_string(ecr::bgm_count()), hs);
        }
        // ---- estado (nao salvo / job)
        {
            TextStyle ds = TextStyle::solid(st_->dirty() ? Color::hex(0xffb347) : Color::hex(0x6fe38a), 1, th.pal.text_outline);
            draw_text(rd, *th.fonts.help, 840, 64, st_->dirty() ? "● alterações não salvas (F2 / Select)" : "✓ tudo salvo", ds);
        }
        draw_bar(app);
        draw_list(app);
        draw_editor(app);
        draw_jobs(app);
        {
            std::string hint = zone_ == Z_EDIT ? "A: alterar   ◄ ►: valor   X: restaurar   B: voltar à lista" :
                               zone_ == Z_LIST ? "▲▼: instrumento   A/►: editar   X: restaurar   Y: ajuda" : "◄ ►: botões   A: acionar   ▼: lista";
            hint += "   Start: TESTAR NO JOGO   Select(F2): SALVAR";
            if (!app.input.state().using_pad) hint = (zone_ == Z_EDIT ? "Enter: alterar   ←→: valor   X: restaurar   Esc: lista" : "↑↓: instrumento   Enter/→: editar   Espaço: play   S/M: solo/mudo   B: A/B   Ctrl+Z/Y: desfazer") + std::string("   F5: testar no jogo   F2: salvar");
            draw_help_bar(app, hint);
        }
        if (msg_t_ > 0 && !msg_.empty()) {
            float w = th.fonts.help->measure(msg_) + 40;
            Rect r{CANVAS_W / 2.f - w / 2, 632, w, 32};
            rd.fill(r, Color::hex(0x07111f, 230)); rd.line_rect(r, Color::hex(0xf9d64a), 2);
            TextStyle hs = th.help_style(); hs.align = Align::Center;
            draw_text(rd, *th.fonts.help, CANVAS_W / 2.f, r.y + 5, msg_, hs);
        }
        if (text_edit_) {
            rd.fill({0, 0, CANVAS_W, CANVAS_H}, Color::hex(0x040a14, 170));
            Rect p{340, 280, 600, 150};
            th.draw_panel(rd, p, 0.95f);
            TextStyle g = th.gold_style(0.4f); g.align = Align::Center;
            draw_text(rd, *th.fonts.title, CANVAS_W / 2.f, p.y + 10, "NOME DO INSTRUMENTO", g);
            TextStyle it = th.item_style(true, true); it.scale = 0.8f; it.align = Align::Center;
            std::string shown = text_buf_ + (((int)(t_ * 2) & 1) ? "_" : " ");
            draw_text(rd, *th.fonts.menu, CANVAS_W / 2.f, p.y + 70, shown, it);
            TextStyle hs = th.help_style(); hs.align = Align::Center;
            draw_text(rd, *th.fonts.help, CANVAS_W / 2.f, p.y + 112, "Digite no teclado  —  Enter confirma, Esc cancela", hs);
        }
        if (show_help_) draw_help_overlay(app);
    }

private:
    // ------------------------------------------------------------------ pre-estagios
    void update_prestage(App& app, const InputState& in) {
        const auto stg = st_->stage();
        if (in.p(A_BACK) || in.mouse_right_click) {
            if (stg == Studio::Stage::Extracting) { st_->cancel_jobs(); return; }
            app.pop(); return;
        }
        if ((in.p(A_ACCEPT) || in.mouse_click) && (stg == Studio::Stage::NeedDumps || stg == Studio::Stage::Error)) st_->start_extract();
        if (in.p(A_START)) request_play(app);
    }
    void draw_prestage(App& app) {
        Render& rd = app.render; Theme& th = app.theme;
        Rect p{180, 150, 920, 400};
        th.draw_panel(rd, p, 0.85f);
        TextStyle it = th.item_style(true, true); it.align = Align::Center; it.scale = 0.9f;
        TextStyle hs = th.help_style(); hs.align = Align::Center;
        float cx = CANVAS_W / 2.f, y = p.y + 40;
        auto para = [&](const std::string& s) {
            for (auto& l : wrap_text(*th.fonts.help, s, p.w - 80)) { draw_text(rd, *th.fonts.help, cx, y, l, hs); y += 28; }
        };
        const auto stg = fake_extract_ ? Studio::Stage::Extracting : st_->stage();
        const StudioPaths& sp = st_->paths();
        switch (stg) {
        case Studio::Stage::NoRom:
            draw_text(rd, *th.fonts.menu, cx, y, "Configure a ROM primeiro", it); y += 60;
            para("O Estúdio precisa rodar o jogo uma vez (headless) para extrair as notas das 17 músicas. Escolha uma ROM válida no menu ROM e volte aqui.");
            break;
        case Studio::Stage::NoSf2:
            draw_text(rd, *th.fonts.menu, cx, y, "SoundFont não encontrado", it); y += 60;
            para("A prévia usa o mesmo SoundFont do jogo (o mesmo do mixer_studio.py). Arquivo esperado:");
            y += 6; para(path_to_utf8(sp.sf2));
            y += 6; para("Coloque o .sf2 nesse caminho ou escolha outro em ÁUDIO › Arquivos › SoundFont (SF2).");
            break;
        case Studio::Stage::NeedDumps:
        case Studio::Stage::Error: {
            draw_text(rd, *th.fonts.menu, cx, y, stg == Studio::Stage::Error ? "Falha na extração" : "Primeira vez no Estúdio", it); y += 60;
            if (stg == Studio::Stage::Error) para(st_->error());
            para("É preciso extrair os dados das músicas rodando o jogo headless: cerca de 17 músicas × 40 s (≈ 12 min), em segundo plano, com progresso e cancelamento. Os dados ficam em " + path_to_utf8(sp.cache_dir) + " (fora do git).");
            y += 14;
            TextStyle go = th.item_style(true, true); go.align = Align::Center; go.scale = 0.9f;
            draw_text(rd, *th.fonts.menu, cx, p.y + p.h - 70, "Enter / A: extrair agora", go);
            break;
        }
        case Studio::Stage::Extracting: {
            JobState j = fake_extract_ ? JobState{true, 0.36f, "Música 6/17: capturando notas (Stage S26)…", ""} : st_->dump_job();
            draw_text(rd, *th.fonts.menu, cx, y, "Extraindo os dados das músicas…", it); y += 70;
            Rect bar{p.x + 60, y, p.w - 120, 26};
            rd.fill(bar, Color::hex(0x07111f)); rd.line_rect(bar, Color::hex(0xdde6ee), 2);
            float f = j.frac < 0 ? std::fmod(t_ * 0.4f, 1.f) : j.frac;
            if (j.frac < 0) rd.fill({bar.x + 3 + bar.w * 0.6f * f, bar.y + 3, bar.w * 0.4f - 6, bar.h - 6}, Color::hex(0xffe63a));
            else rd.fill({bar.x + 3, bar.y + 3, (bar.w - 6) * f, bar.h - 6}, Color::hex(0xffe63a));
            y += 44;
            para(j.text);
            if (j.frac >= 0) { char b[24]; snprintf(b, sizeof b, "%.0f%%", j.frac * 100); draw_text(rd, *th.fonts.help, cx, y, b, hs); }
            draw_text(rd, *th.fonts.help, cx, p.y + p.h - 50, "Esc / B: cancelar", hs);
            th.draw_yinyang(rd, cx - 360, p.y + 60, 56, t_ * 90.0);
            th.draw_yinyang(rd, cx + 360, p.y + 60, 56, -t_ * 90.0);
            break;
        }
        case Studio::Stage::Loading:
            draw_text(rd, *th.fonts.menu, cx, y + 40, "Carregando as músicas…", it);
            th.draw_yinyang(rd, cx, y + 150, 80, t_ * 120.0);
            break;
        default: break;
        }
    }

    // ------------------------------------------------------------------ dados
    int song_index() const {
        for (int i = 0; i < ecr::bgm_count(); ++i) if (ecr::bgm_table()[i].id == st_->current_bgm()) return i;
        return 0;
    }
    void change_song(int d) {
        int n = ecr::bgm_count(), i = (song_index() + d + n) % n;
        st_->select_song(ecr::bgm_table()[i].id);
        list_focus_ = 0; list_scroll_ = 0; edit_focus_ = 0;
        rebuild_list();
        if (!list_.empty() && list_[0].kind == 2) list_focus_ = 1;
        say("Música: " + std::string(ecr::bgm_table()[i].title));
    }
    void say(const std::string& s) { msg_ = s; msg_t_ = 3.f; }

    void rebuild_list() {
        list_.clear();
        if (library_) {
            std::vector<const ecr::VoiceEntry*> v;
            for (auto& e : st_->voices().voices()) v.push_back(&e);
            std::stable_sort(v.begin(), v.end(), [](const ecr::VoiceEntry* a, const ecr::VoiceEntry* b) {
                if (a->meta.songs.size() != b->meta.songs.size()) return a->meta.songs.size() > b->meta.songs.size();
                return a->hash < b->hash;
            });
            for (auto* e : v) list_.push_back({0, e->hash});
        } else {
            auto maps = st_->maps();
            bool head = false;
            for (auto& m : maps) if (m.is_voice) list_.push_back({0, m.key});
            for (auto& m : maps) if (!m.is_voice) { if (!head) { list_.push_back({2, ""}); head = true; } list_.push_back({1, m.key}); }
        }
        if (list_focus_ >= (int)list_.size()) list_focus_ = std::max(0, (int)list_.size() - 1);
        if (!list_.empty() && list_[(size_t)list_focus_].kind == 2) list_focus_ = std::min((int)list_.size() - 1, list_focus_ + 1);
    }
    bool move_list(int d) {
        int n = (int)list_.size(); if (!n) return false;
        int i = list_focus_;
        do { i += d; } while (i >= 0 && i < n && list_[(size_t)i].kind == 2);
        if (i < 0) return false;
        if (i >= n) return true;
        list_focus_ = i;
        if (list_focus_ < list_scroll_) list_scroll_ = list_focus_;
        if (list_focus_ >= list_scroll_ + kLVis) list_scroll_ = list_focus_ - kLVis + 1;
        return true;
    }
    const LRow* cur() const { return list_focus_ >= 0 && list_focus_ < (int)list_.size() ? &list_[(size_t)list_focus_] : nullptr; }
    const ecr::VoiceEntry* cur_entry() const { const LRow* r = cur(); return r && r->kind == 0 ? st_->voices().find(r->key) : nullptr; }
    const ecr::VoiceTrack* cur_track() const {
        const LRow* r = cur(); const ecr::SongNotes* sg = st_->song();
        return r && r->kind == 0 && sg ? sg->find_voice(r->key) : nullptr;
    }
    bool in_song(const std::string& key) const { return st_->song() && st_->song()->find_voice(key) != nullptr; }

    // ------------------------------------------------------------------ transporte
    void toggle_play() { if (!st_->playing() && !st_->synth().has_soundfont()) return; st_->play(!st_->playing()); }
    void toggle_ab(App& app) {
        (void)app;
        if (!st_->original_ready()) {
            if (st_->orig_job().running) { say("Capturando o original… aguarde."); return; }
            st_->start_capture_original();
            say("Capturando o áudio original (≈ 70 s, em segundo plano)…");
            return;
        }
        st_->set_ab_original(!st_->ab_original());
        say(st_->ab_original() ? "A: ORIGINAL (chip do jogo)" : "B: REMASTER");
    }
    void do_save(App& app) {
        (void)app;
        std::string err;
        if (!st_->dirty()) { say("Nada a salvar."); return; }
        say(st_->save(&err) ? "Salvo (voices.toml + perfil, com .bak)." : "Falha ao salvar: " + err);
    }
    void do_test(App& app) {
        std::string err;
        if (st_->test_in_game(&err)) { say("Abrindo o jogo no Music Test (salvo antes)…"); app.log("estudio: testar no jogo bgm " + std::to_string(st_->current_bgm())); }
        else say("Não foi possível abrir o jogo: " + err);
    }
    void bar_action(App& app, int i) {
        switch (i) {
        case 0: toggle_play(); break;
        case 1: loop_ = !loop_; st_->set_loop(loop_); break;
        case 2: toggle_ab(app); break;
        case 3: do_save(app); break;
        case 4: do_test(app); break;
        case 5: st_->undo(); break;
        case 6: st_->redo(); break;
        case 7: library_ = !library_; list_focus_ = 0; list_scroll_ = 0; rebuild_list(); say(library_ ? "Biblioteca: todos os instrumentos do jogo" : "Instrumentos desta música"); break;
        }
    }
    void toggle_mute() {
        const LRow* r = cur(); if (!r || r->kind == 2 || library_) return;
        auto m = st_->map_of(r->key); st_->set_mute(r->key, !m.mute);
    }
    void toggle_solo() {
        const LRow* r = cur(); if (!r || r->kind == 2 || library_) return;
        auto m = st_->map_of(r->key); st_->set_solo(r->key, !m.solo);
    }

    // ------------------------------------------------------------------ editor
    void build_edit(App& app) {
        (void)app;
        edit_.clear();
        const LRow* r = cur(); if (!r) return;
        auto add = [&](int id, const std::string& label, const std::string& value, bool en = true, bool arrows = false) {
            ERow e; e.id = id; e.label = label; e.value = value; e.enabled = en; e.arrows = arrows; edit_.push_back(e);
        };
        auto head = [&](const std::string& s) { ERow e; e.id = E_HEAD; e.label = s; e.selectable = false; edit_.push_back(e); };
        auto info = [&](const std::string& s, Color t = Color{255, 255, 255, 0}) { ERow e; e.id = E_INFO; e.label = s; e.selectable = false; e.tint = t; edit_.push_back(e); };
        if (r->kind == 0) {
            const ecr::VoiceEntry* ve = cur_entry();
            if (!ve) return;
            ecr::VoiceEdit e = st_->effective_edit(r->key);
            auto m = st_->map_of(r->key);
            bool ov = st_->has_override(r->key);
            add(E_NAME, "Nome", e.name.empty() ? "(sem nome)" : e.name);
            add(E_MODE, "Modo", e.mode == "gm" ? "GM (SoundFont)" : e.mode == "percussion" ? "Percussão" : e.mode == "chip" ? "Chip original" : "Off", true, true);
            add(E_PROG, "Programa", e.mode == "gm" ? std::to_string(e.program) + "  " + ecr::gm_program_name(e.program) + "  [" + ecr::gm_program_category(e.program) + "]" : std::string("—"), e.mode == "gm", true);
            add(E_BANK, "Banco", std::to_string(e.bank), e.mode == "gm" || e.mode == "percussion", true);
            add(E_TRANSP, "Transposição", (e.transpose > 0 ? "+" : "") + std::to_string(e.transpose) + " semitons", e.mode == "gm" || e.mode == "percussion", true);
            { char b[24]; snprintf(b, sizeof b, "%+.1f dB", e.volume_db); add(E_VOL, "Volume", b, e.mode != "off", true); }
            add(E_DRUM, "Nota de bateria", e.mode == "percussion" ? (e.drum_note ? std::to_string(e.drum_note) + "  " + ecr::gm_drum_name(e.drum_note) : std::string("0  (nota da trilha)")) : std::string("—"), e.mode == "percussion", true);
            add(E_SCOPE, "Escopo da edição", ov ? "Só nesta música" : "Global (todas as músicas)", !library_, true);
            {
                int n = (int)ve->meta.songs.size();
                if (n > 1 && !ov) info("⚠ Edição GLOBAL: usado em " + std::to_string(n) + " músicas.", Color::hex(0xffb347));
            }
            if (!library_) {
                add(E_SOLO, "Solo", m.solo ? "Ligado" : "Desligado", true, true);
                add(E_MUTE, "Mudo", m.mute ? "Ligado" : "Desligado", true, true);
            }
            const std::string sug = ve->meta.suggestion;
            add(E_SUGGEST, "Aplicar sugestão", sug.empty() ? "—" : sug, !sug.empty());
            { ecr::VoiceEdit sv; bool has = st_->saved_edit(r->key, &sv); add(E_RESTORE, "Restaurar do disco", "X", has && sv != st_->voices().find(r->key)->edit); }
            add(E_PATCH, "Patch FM", patch_view_ ? "ocultar ▾" : "ALG " + std::to_string(ve->meta.alg) + " · FB " + std::to_string(ve->meta.fb) + "  ▸");
            if (library_) add(E_LISTEN, "Ouvir em contexto", ve->meta.songs.empty() ? "—" : "numa música que o usa", !ve->meta.songs.empty());
            char b[128];
            snprintf(b, sizeof b, "Notas %d–%d · mediana %d · %d ms médios · %d notas", ve->meta.note_min, ve->meta.note_max, ve->meta.note_median, ve->meta.avg_dur_ms, ve->meta.notes);
            info(b);
        } else {   // canal PSG/ruido/DAC
            auto m = st_->map_of(r->key);
            const ecr::ProfileChannel* pc = st_->profile_loaded() ? st_->profile().find_channel(r->key) : nullptr;
            ecr::ProfileChannel c = pc ? *pc : ecr::ProfileChannel();
            c.name = r->key;
            add(E_MODE, "Modo", c.mode, st_->profile_loaded(), true);
            add(E_PROG, "Programa", std::to_string(c.program) + "  " + ecr::gm_program_name(c.program), st_->profile_loaded() && c.mode == "gm", true);
            add(E_SOLO, "Solo", m.solo ? "Ligado" : "Desligado", true, true);
            add(E_MUTE, "Mudo", m.mute ? "Ligado" : "Desligado", true, true);
            info("Canais PSG, ruído e DAC são por música (perfil da faixa).");
        }
        if (edit_focus_ >= (int)edit_.size() || !edit_[(size_t)edit_focus_].selectable) edit_focus_ = std::min(first_selectable(), (int)edit_.size() - 1);
    }
    int first_selectable() const { for (size_t i = 0; i < edit_.size(); ++i) if (edit_[i].selectable) return (int)i; return 0; }
    void move_edit(int d) {
        int n = (int)edit_.size(); if (!n) return;
        int i = edit_focus_;
        for (int k = 0; k < n; ++k) { i = (i + d + n) % n; if (edit_[(size_t)i].selectable) { edit_focus_ = i; break; } }
        if (edit_focus_ < edit_scroll_) edit_scroll_ = edit_focus_;
        if (edit_focus_ >= edit_scroll_ + kEVis) edit_scroll_ = edit_focus_ - kEVis + 1;
    }
    template <class F> void mutate_voice(F fn) {
        const LRow* r = cur(); if (!r || r->kind != 0) return;
        ecr::VoiceEdit e = st_->effective_edit(r->key);
        ecr::VoiceEdit before = e;
        fn(e);
        if (e == before) return;
        st_->edit_voice(r->key, e, st_->has_override(r->key) ? VoiceScope::Song : VoiceScope::Global);
    }
    void edit_adjust(App& app, int d) {
        if (edit_focus_ < 0 || edit_focus_ >= (int)edit_.size()) return;
        const ERow& er = edit_[(size_t)edit_focus_];
        if (!er.enabled) return;
        const LRow* r = cur(); if (!r) return;
        auto clampi = [](int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); };
        if (r->kind == 0) {
            switch (er.id) {
            case E_MODE: mutate_voice([&](ecr::VoiceEdit& e) { int i = 0; for (int k = 0; k < 4; ++k) if (e.mode == kModes[k]) i = k; e.mode = kModes[(i + d + 4) % 4]; }); break;
            case E_PROG: mutate_voice([&](ecr::VoiceEdit& e) { e.program = clampi(e.program + d * (app.input.state().shift ? 8 : 1), 0, 127); }); break;
            case E_BANK: mutate_voice([&](ecr::VoiceEdit& e) { e.bank = clampi(e.bank + d, 0, 128); }); break;
            case E_TRANSP: mutate_voice([&](ecr::VoiceEdit& e) { e.transpose = clampi(e.transpose + d, -24, 24); }); break;
            case E_VOL: mutate_voice([&](ecr::VoiceEdit& e) { e.volume_db = std::max(-60.0, std::min(24.0, std::round((e.volume_db + d * 0.5) * 2) / 2)); }); break;
            case E_DRUM: mutate_voice([&](ecr::VoiceEdit& e) { e.drum_note = clampi(e.drum_note + d, 0, 127); }); break;
            case E_SCOPE: {
                if (st_->has_override(r->key)) st_->clear_override(r->key);
                else { ecr::VoiceEdit e = st_->effective_edit(r->key); st_->edit_voice(r->key, e, VoiceScope::Song); }
                break;
            }
            case E_SOLO: toggle_solo(); break;
            case E_MUTE: toggle_mute(); break;
            default: break;
            }
        } else {
            auto cycle_channel = [&](auto fn) {
                const ecr::ProfileChannel* pc = st_->profile().find_channel(r->key);
                if (!pc) return;
                ecr::ProfileChannel c = *pc; fn(c); if (!(c == *pc)) st_->edit_channel(c);
            };
            static const char* cm[] = {"auto", "gm", "percussion", "chip", "off"};
            switch (er.id) {
            case E_MODE: cycle_channel([&](ecr::ProfileChannel& c) { int i = 0; for (int k = 0; k < 5; ++k) if (c.mode == cm[k]) i = k; c.mode = cm[(i + d + 5) % 5]; }); break;
            case E_PROG: cycle_channel([&](ecr::ProfileChannel& c) { c.program = clampi(c.program + d, 0, 127); }); break;
            case E_SOLO: toggle_solo(); break;
            case E_MUTE: toggle_mute(); break;
            default: break;
            }
        }
    }
    void edit_activate(App& app) {
        if (edit_focus_ < 0 || edit_focus_ >= (int)edit_.size()) return;
        const ERow& er = edit_[(size_t)edit_focus_];
        if (!er.enabled) return;
        const LRow* r = cur(); if (!r) return;
        if (r->kind == 0) {
            switch (er.id) {
            case E_NAME:
                text_buf_ = st_->effective_edit(r->key).name; text_edit_ = true; app.input.set_text_input(true); return;
            case E_SUGGEST: {
                const ecr::VoiceEntry* ve = cur_entry();
                const ecr::VoiceTrack* vt = cur_track();
                if (!ve) return;
                if (vt) {
                    ecr::Suggestion s = ecr::suggest(vt->patch, vt->notes);
                    mutate_voice([&](ecr::VoiceEdit& e) { e.mode = s.mode; e.program = s.program; if (s.mode == "percussion") e.drum_note = ecr::suggest_drum_note(vt->notes); });
                    say("Sugestão aplicada: " + s.label);
                } else say("Sugestão só disponível para instrumentos da música atual.");
                return;
            }
            case E_RESTORE: restore_selected(); return;
            case E_PATCH: patch_view_ = !patch_view_; return;
            case E_LISTEN: listen_in_context(); return;
            default: break;
            }
        }
        if (er.id == E_PATCH) { patch_view_ = !patch_view_; return; }
        edit_adjust(app, +1);
    }
    void restore_selected() {
        const LRow* r = cur(); if (!r || r->kind != 0) return;
        ecr::VoiceEdit sv;
        if (!st_->saved_edit(r->key, &sv)) return;
        const ecr::VoiceEntry* ve = st_->voices().find(r->key);
        if (ve && ve->edit != sv) st_->edit_voice(r->key, sv, VoiceScope::Global);
        if (st_->has_override(r->key)) st_->clear_override(r->key);
        say("Restaurado do disco.");
    }
    void listen_in_context() {
        const ecr::VoiceEntry* ve = cur_entry();
        if (!ve || ve->meta.songs.empty()) return;
        const std::string key = ve->hash;
        const ecr::BgmInfo* b = ecr::bgm_by_slug(ve->meta.songs.front());
        if (!b) return;
        library_ = false;
        st_->select_song(b->id);
        rebuild_list();
        for (size_t i = 0; i < list_.size(); ++i) if (list_[i].key == key) list_focus_ = (int)i;
        for (auto& m : st_->maps()) if (m.is_voice) st_->set_solo(m.key, m.key == key);
        st_->seek_seconds(0);
        st_->play(true);
        say("Solo no instrumento, tocando em " + std::string(b->title));
    }
    void update_text(App& app, const InputState& in) {
        if (in.new_key[SDL_SCANCODE_ESCAPE]) { text_edit_ = false; app.input.set_text_input(false); return; }
        if (in.new_key[SDL_SCANCODE_RETURN] || in.new_key[SDL_SCANCODE_KP_ENTER]) {
            std::string nm = ecr::clean_voice_name(text_buf_);
            mutate_voice([&](ecr::VoiceEdit& e) { e.name = nm; });
            text_edit_ = false; app.input.set_text_input(false); return;
        }
        if (in.new_key[SDL_SCANCODE_BACKSPACE] && !text_buf_.empty()) {
            size_t i = text_buf_.size() - 1;
            while (i > 0 && ((unsigned char)text_buf_[i] & 0xC0) == 0x80) --i;
            text_buf_.erase(i);
        }
        if (text_buf_.size() < 60) text_buf_ += in.text;
    }

    // ------------------------------------------------------------------ mouse
    void mouse(App& app, const InputState& in) {
        if (!in.mouse_inside || (!in.mouse_click)) return;
        float mx = in.mouse_x, my = in.mouse_y;
        // seletor de musica
        if (my >= 58 && my < 92 && mx >= 40 && mx < 600) { change_song(mx < 320 ? -1 : +1); return; }
        // barra
        for (int i = 0; i < kBarN; ++i) { Rect b = bar_rect(i); if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) { zone_ = Z_BAR; bar_focus_ = i; bar_action(app, i); return; } }
        // progresso
        { Rect s = seek_rect(); if (mx >= s.x && mx < s.x + s.w && my >= s.y - 4 && my < s.y + s.h + 4) { st_->seek_seconds((mx - s.x) / s.w * st_->duration_s()); return; } }
        // lista
        for (int i = list_scroll_; i < std::min((int)list_.size(), list_scroll_ + kLVis); ++i) {
            Rect r = lrow_rect(i);
            if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h && list_[(size_t)i].kind != 2) {
                list_focus_ = i; zone_ = Z_LIST;
                if (mx > r.x + r.w - 134 && list_[(size_t)i].kind != 2 && !library_) { if (mx < r.x + r.w - 108) toggle_solo(); else if (mx < r.x + r.w - 80) toggle_mute(); }
                return;
            }
        }
        // editor
        build_edit(app);
        for (int i = edit_scroll_; i < std::min((int)edit_.size(), edit_scroll_ + kEVis); ++i) {
            Rect r = erow_rect(i);
            if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h && edit_[(size_t)i].selectable) {
                zone_ = Z_EDIT; edit_focus_ = i;
                if (edit_[(size_t)i].arrows && mx < r.x + r.w * 0.55f) edit_adjust(app, -1); else edit_activate(app);
                return;
            }
        }
    }

    // ------------------------------------------------------------------ desenho
    static constexpr int kLVis = 12, kEVis = 12;
    Rect bar_rect(int i) const {
        static const float w[kBarN] = {92, 92, 70, 92, 168, 128, 118, 124};
        float x = 40; for (int k = 0; k < i; ++k) x += w[k] + 8;
        return {x, 100, w[i], 34};
    }
    Rect seek_rect() const { return {40, 142, 1000, 12}; }
    Rect lrow_rect(int i) const { return {48, 196 + (i - list_scroll_) * 34.f, 600, 32}; }
    Rect erow_rect(int i) const { return {684, 196 + (i - edit_scroll_) * 34.f, 540, 32}; }

    void draw_bar(App& app) {
        Render& rd = app.render; Theme& th = app.theme;
        for (int i = 0; i < kBarN; ++i) {
            Rect b = bar_rect(i);
            bool foc = zone_ == Z_BAR && bar_focus_ == i;
            std::string lab = kBarLabels[i];
            bool on = false, en = true;
            if (i == 0) { lab = st_->playing() ? "■ STOP" : "▶ PLAY"; on = st_->playing(); }
            if (i == 1) on = loop_;
            if (i == 2) { lab = st_->original_ready() ? (st_->ab_original() ? "A: ORIG" : "B: REMA") : "A/B"; on = st_->ab_original(); }
            if (i == 3) en = st_->dirty();
            if (i == 4) en = !st_->game_running();
            if (i == 5) en = st_->can_undo();
            if (i == 6) en = st_->can_redo();
            if (i == 7) { lab = library_ ? "INSTRUM." : "BIBLIOTECA"; on = library_; }
            rd.fill(b, on ? Color::hex(0x2a6a3a, 230) : Color::hex(0x07111f, 215));
            rd.line_rect(b, foc ? Color::hex(0xffe63a) : Color::hex(0x6aa9b0, 170), foc ? 3 : 1);
            TextStyle st = th.item_style(foc, en); st.scale = 0.6f; st.align = Align::Center;
            draw_text(rd, *th.fonts.menu, b.x + b.w / 2, b.y + 6, lab, st);
        }
        // tempo + posicao
        Rect s = seek_rect();
        rd.fill(s, Color::hex(0x07111f, 230)); rd.line_rect(s, Color::hex(0x6aa9b0, 140), 1);
        double dur = std::max(0.01, st_->duration_s()), pos = st_->position_s();
        rd.fill({s.x + 2, s.y + 2, (s.w - 4) * (float)std::min(1.0, pos / dur), s.h - 4}, st_->ab_original() ? Color::hex(0xf9a43a) : Color::hex(0x6fe38a));
        TextStyle hs = th.help_style();
        draw_text(rd, *th.fonts.help, s.x + s.w + 12, s.y - 8, fmt_time(pos) + " / " + fmt_time(dur), hs);
        // VU geral (soma dos canais)
        float lv = 0; for (auto& m : st_->maps()) lv = std::max(lv, st_->level(m.key));
        Rect vu{1130, 100, 100, 34};
        rd.fill(vu, Color::hex(0x07111f, 215)); rd.line_rect(vu, Color::hex(0x6aa9b0, 140), 1);
        for (int k = 0; k < 10; ++k) {
            float on = lv * 10 > k ? 1.f : 0.f;
            Color c = k < 6 ? Color::hex(0x6fe38a) : (k < 8 ? Color::hex(0xf9d64a) : Color::hex(0xff6a5a));
            rd.fill({vu.x + 5 + k * 9.4f, vu.y + 6, 7, 22}, on ? c : Color::hex(0x1b2230));
        }
    }

    void draw_list(App& app) {
        Render& rd = app.render; Theme& th = app.theme;
        Rect pn{40, 160, 620, 458};
        th.draw_panel(rd, pn, 0.72f);
        TextStyle ct = TextStyle::solid(th.pal.gold_top, 1, th.pal.gold_outline);
        draw_text(rd, *th.fonts.help, pn.x + 14, pn.y + 5, library_ ? "BIBLIOTECA — TODOS OS INSTRUMENTOS (por nº de músicas)" : "INSTRUMENTOS DESTA MÚSICA (por nº de notas)", ct);
        const Font& hf = *th.fonts.help;
        int end = std::min((int)list_.size(), list_scroll_ + kLVis);
        for (int i = list_scroll_; i < end; ++i) {
            const LRow& lr = list_[(size_t)i];
            Rect r = lrow_rect(i);
            if (lr.kind == 2) { draw_text(rd, hf, r.x + 8, r.y + 4, "── canais ──", TextStyle::solid(th.pal.gold_top, 1, th.pal.gold_outline)); continue; }
            bool foc = i == list_focus_;
            if (foc) { rd.fill(r, Color::hex(0xf9d64a, zone_ == Z_LIST ? 52 : 28)); rd.line_rect(r, Color::hex(0xf9d64a, zone_ == Z_LIST ? 190 : 90), 1); }
            ecr::ChannelMap m = st_->map_of(lr.key);
            std::string label, mode;
            bool dirty = false;
            if (lr.kind == 0) {
                const ecr::VoiceEntry* ve = st_->voices().find(lr.key);
                ecr::VoiceEdit e = st_->effective_edit(lr.key);
                label = lr.key + "  " + (e.name.empty() ? "—" : e.name);
                m.mode = e.mode; m.program = e.program; m.drum_note = e.drum_note;
                mode = mode_label(m);
                dirty = st_->voice_dirty(lr.key) || st_->has_override(lr.key);
                (void)ve;
            } else {
                static const char* nm[] = {"psg0", "PSG 0", "psg1", "PSG 1", "psg2", "PSG 2", "psg_noise", "PSG ruído", "dac", "DAC (chip)"};
                label = lr.key; for (int k = 0; k < 10; k += 2) if (lr.key == nm[k]) label = nm[k + 1];
                mode = lr.key == "dac" ? "CHIP" : mode_label(m);
            }
            TextStyle st = th.item_style(foc, true); st.scale = 0.58f; st.outline_px = 1;
            float ty = r.y + 3;
            if (dirty) draw_text(rd, *th.fonts.menu, r.x + 4, ty, "•", TextStyle::solid(Color::hex(0xffb347), 1, th.pal.text_outline));
            draw_text(rd, *th.fonts.menu, r.x + 18, ty, label.size() > 20 ? label.substr(0, 19) + "…" : label, st);
            TextStyle ms = TextStyle::solid(lr.kind == 1 || (m.mode == "chip" || m.mode == "off") ? Color::hex(0xaac4cc) : Color::hex(0xe8f4f2), 1, th.pal.text_outline);
            draw_text(rd, hf, r.x + 236, r.y + 5, mode.size() > 23 ? mode.substr(0, 22) + "…" : mode, ms);
            if (!library_) {
                TextStyle f1 = TextStyle::solid(m.solo ? Color::hex(0xffe63a) : Color::hex(0x56707a), 1, th.pal.text_outline);
                TextStyle f2 = TextStyle::solid(m.mute ? Color::hex(0xff6a5a) : Color::hex(0x56707a), 1, th.pal.text_outline);
                draw_text(rd, hf, r.x + r.w - 128, r.y + 5, "S", f1);
                draw_text(rd, hf, r.x + r.w - 102, r.y + 5, "M", f2);
                float lv = st_->level(lr.key);
                for (int k = 0; k < 4; ++k) rd.fill({r.x + r.w - 76 + k * 17.f, r.y + 8, 13, 16}, lv * 4 > k ? Color::hex(0x6fe38a) : Color::hex(0x1b2230));
            } else {
                const ecr::VoiceEntry* ve = st_->voices().find(lr.key);
                if (ve) draw_text(rd, hf, r.x + r.w - 110, r.y + 5, std::to_string(ve->meta.songs.size()) + " músicas", ms);
            }
        }
        if (list_scroll_ > 0) draw_text(rd, hf, pn.x + pn.w - 28, pn.y + 4, "▲", th.help_style());
        if (end < (int)list_.size()) draw_text(rd, hf, pn.x + pn.w - 28, pn.y + pn.h - 26, "▼", th.help_style());
    }

    void draw_editor(App& app) {
        Render& rd = app.render; Theme& th = app.theme;
        Rect pn{670, 160, 570, 458};
        th.draw_panel(rd, pn, 0.72f);
        const LRow* lr = cur();
        std::string title = "INSTRUMENTO SELECIONADO";
        if (lr && lr->kind == 1) title = "CANAL SELECIONADO";
        draw_text(rd, *th.fonts.help, pn.x + 14, pn.y + 5, title, TextStyle::solid(th.pal.gold_top, 1, th.pal.gold_outline));
        if (!lr) return;
        build_edit(app);
        if (patch_view_ && lr->kind == 0) {
            if (const ecr::VoiceEntry* ve = cur_entry()) draw_fm_patch(app, {pn.x + 6, pn.y + 30, pn.w - 12, pn.h - 34}, ve->meta);
            TextStyle hs = th.help_style(); hs.align = Align::Right;
            draw_text(rd, *th.fonts.help, pn.x + pn.w - 14, pn.y + 5, "A / ◄► volta", hs);
            return;
        }
        const Font& mf = *th.fonts.menu;
        int end = std::min((int)edit_.size(), edit_scroll_ + kEVis);
        for (int i = edit_scroll_; i < end; ++i) {
            const ERow& er = edit_[(size_t)i];
            Rect r = erow_rect(i);
            if (er.id == E_HEAD) continue;
            if (er.id == E_INFO) {
                TextStyle hs = er.tint.a ? TextStyle::solid(Color{er.tint.r, er.tint.g, er.tint.b, 255}, 1, th.pal.text_outline) : th.help_style();
                auto lines = wrap_text(*th.fonts.help, er.label, r.w - 10);
                draw_text(rd, *th.fonts.help, r.x + 4, r.y + 4, lines.empty() ? "" : lines[0], hs);
                continue;
            }
            bool foc = zone_ == Z_EDIT && i == edit_focus_;
            if (foc) { rd.fill(r, Color::hex(0xf9d64a, 44)); rd.line_rect(r, Color::hex(0xf9d64a, 170), 1); }
            TextStyle st = th.item_style(foc, er.enabled); st.scale = 0.58f; st.outline_px = 1;
            draw_text(rd, mf, r.x + 8, r.y + 3, er.label, st);
            TextStyle vs = st; vs.align = Align::Right;
            std::string v = er.value;
            if (v.size() > 34) v = v.substr(0, 33) + "…";
            if (foc && er.arrows && er.enabled) v = "◄ " + v + " ►";
            draw_text(rd, mf, r.x + r.w - 8, r.y + 3, v, vs);
        }
    }

    void draw_jobs(App& app) {
        Render& rd = app.render; Theme& th = app.theme;
        JobState oj = st_->orig_job();
        if (oj.running || !oj.error.empty()) {
            Rect r{40, 622, 1200, 22};
            rd.fill(r, Color::hex(0x07111f, 230)); rd.line_rect(r, Color::hex(0x6aa9b0, 150), 1);
            float f = oj.frac < 0 ? std::fmod(t_ * 0.5f, 1.f) : oj.frac;
            if (oj.running) {
                if (oj.frac < 0) rd.fill({r.x + 2 + (r.w - 4) * 0.7f * f, r.y + 2, (r.w - 4) * 0.3f, r.h - 4}, Color::hex(0xf9a43a));
                else rd.fill({r.x + 2, r.y + 2, (r.w - 4) * f, r.h - 4}, Color::hex(0xf9a43a));
            }
            TextStyle hs = TextStyle::solid(oj.error.empty() ? Color::hex(0xffffff) : Color::hex(0xff6a5a), 1, th.pal.text_outline);
            std::string txt = oj.running ? "A (original): " + oj.text + "  — Esc cancela" : "Falha ao capturar o original: " + oj.error;
            draw_text(rd, *th.fonts.help, r.x + 8, r.y - 1, txt.size() > 150 ? txt.substr(0, 150) + "…" : txt, hs);
        }
    }

    void draw_help_overlay(App& app) {
        Render& rd = app.render; Theme& th = app.theme;
        rd.fill({0, 0, CANVAS_W, CANVAS_H}, Color::hex(0x040a14, 170));
        Rect p{220, 140, 840, 440};
        th.draw_panel(rd, p, 0.95f);
        TextStyle g = th.gold_style(0.45f); g.align = Align::Center;
        draw_text(rd, *th.fonts.title, CANVAS_W / 2.f, p.y + 10, "COMO USAR O ESTÚDIO", g);
        const char* lines[] = {
            "Cada instrumento FM do jogo (52 no total) vale para TODAS as músicas que o usam (voices.toml).",
            "Escopo \"Só nesta música\" grava um override no perfil da faixa.",
            "B = REMASTER (SoundFont, em tempo real). A = ORIGINAL (chip do jogo, capturado uma vez, ≈ 70 s).",
            "A/B toca do mesmo ponto; a troca é instantânea (crossfade de 20 ms).",
            "Teclado: Espaço play · S solo · M mudo · B A/B · L loop · Ctrl+Z/Y desfazer · F2 salvar · F5 testar no jogo.",
            "Controle: D-pad navega · A confirma · B volta · X restaura · LB/RB troca de música · Select salva · Start testa no jogo.",
            "Salvar grava de forma atômica e mantém um .bak do arquivo anterior.",
            "\"Testar no jogo\" salva e abre o Music Test na música atual com o remaster híbrido.",
        };
        float y = p.y + 70;
        for (const char* l : lines) { for (auto& w : wrap_text(*th.fonts.help, l, p.w - 60)) { draw_text(rd, *th.fonts.help, p.x + 30, y, w, th.help_style()); y += 28; } y += 6; }
    }

    std::unique_ptr<Studio> st_;
    std::vector<LRow> list_;
    std::vector<ERow> edit_;
    Zone zone_ = Z_LIST;
    int bar_focus_ = 0, list_focus_ = 0, list_scroll_ = 0, edit_focus_ = 0, edit_scroll_ = 0;
    bool library_ = false, patch_view_ = false, loop_ = false, show_help_ = false, text_edit_ = false, fake_extract_ = false;
    std::string text_buf_, msg_;
    float t_ = 0, msg_t_ = 0;
};
}  // namespace

std::unique_ptr<Screen> make_studio_screen() { return std::make_unique<StudioScreen>(); }
