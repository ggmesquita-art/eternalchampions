#include "schema.h"
#include "app.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fs = std::filesystem;

namespace {
using V = std::vector<Choice>;

Opt mk(const char* section, const char* key, const char* tab, const char* label, OType t, const char* def, const char* help) {
    Opt o;
    o.section = section; o.key = key; o.tab = tab; o.label = label; o.type = t; o.def = def; o.help = help;
    o.id = std::string(section) + "." + key;
    return o;
}
Opt boolean(const char* s, const char* k, const char* tab, const char* label, bool def, const char* help) {
    return mk(s, k, tab, label, OType::Bool, def ? "1" : "0", help);
}
Opt integer(const char* s, const char* k, const char* tab, const char* label, int def, int mn, int mx, int step, const char* help) {
    Opt o = mk(s, k, tab, label, OType::Int, std::to_string(def).c_str(), help);
    o.min = mn; o.max = mx; o.step = step; return o;
}
Opt flt(const char* s, const char* k, const char* tab, const char* label, const char* def, double mn, double mx, double step, const char* help, int prec = 2) {
    Opt o = mk(s, k, tab, label, OType::Float, def, help);
    o.min = mn; o.max = mx; o.step = step; o.precision = prec; return o;
}
Opt choice(const char* s, const char* k, const char* tab, const char* label, const char* def, V ch, const char* help) {
    Opt o = mk(s, k, tab, label, OType::Enum, def, help);
    o.choices = std::move(ch); return o;
}
Opt cli(const char* name, const char* flag, const char* tab, const char* label, OType t, const char* def, const char* help) {
    Opt o = mk("launcher.args", name, tab, label, t, def, help);
    o.id = std::string("args.") + name;
    o.cli_flag = flag;
    return o;
}

std::vector<Opt> build() {
    std::vector<Opt> v;
    // ================= VÍDEO (§4.1) =================
    v.push_back(integer("video", "window_scale", "video", "Escala da janela", 2, 1, 8, 1, "Tamanho inicial da janela do jogo em múltiplos de 320x224 (1 a 8)."));
    v.push_back(choice("video", "fullscreen", "video", "Tela cheia", "0", {{"0", "Janela"}, {"1", "Sem borda"}, {"2", "Exclusivo"}}, "Janela, tela cheia sem borda (borderless) ou tela cheia exclusiva."));
    v.push_back(boolean("video", "linear_filter", "video", "Filtro linear", false, "Suaviza a imagem ao escalar (desligado = pixels nítidos)."));
    v.push_back(boolean("video", "integer_scale", "video", "Escala inteira", false, "Só amplia por múltiplos inteiros, mantendo os pixels nítidos (pode deixar bordas pretas)."));
    {
        Opt o = choice("video", "internal_scale", "video", "Escala interna", "1", {{"1", "1x"}, {"2", "2x"}, {"4", "4x"}, {"8", "8x"}, {"16", "16x"}, {"32", "32x"}, {"64", "64x"}}, "Resolução de renderização interna do VDP. Valores altos pesam na GPU.");
        o.restart = true; v.push_back(o);
    }
    v.push_back(choice("video", "presentation_rate", "video", "Taxa de apresentação", "0", {{"0", "Desligada"}, {"120", "120 Hz"}, {"144", "144 Hz"}, {"-1", "Do monitor"}}, "Apresenta quadros extras interpolados para monitores de alta taxa (a lógica do jogo continua a 60 Hz)."));
    v.push_back(boolean("mods.widescreen", "enabled", "video", "Widescreen", false, "Renderizador de cena ampliado: objetos expandidos e HUD ancorado à tela."));
    {
        Opt o = choice("mods.widescreen", "aspect", "video", "Proporção widescreen", "fit", {{"fit", "Ajustar à janela"}, {"16:9", "16:9"}, {"21:9", "21:9"}, {"32:9", "32:9"}}, "Proporção usada pelo widescreen.");
        o.dep_id = "mods.widescreen.enabled"; o.dep_val = "1"; v.push_back(o);
    }
    v.push_back(cli("interlace", "--interlace-display", "video", "Exibição entrelaçada", OType::Enum, "", "Como o runner mostra quadros entrelaçados do VDP. TV (padrão do runner): comprime os campos como numa TV, mantendo a altura original. Raw: mostra o quadro expandido completo."));
    v.back().choices = {{"", "Padrão do jogo (TV)"}, {"tv", "TV (original)"}, {"raw", "Raw (expandido)"}};
    v.push_back(cli("widescreen", "--widescreen", "video", "Widescreen por argumento", OType::Enum, "", "Força o widescreen pela linha de comando (vence o ini). \"stage\" ajusta à fase; W:H livre só editando [launcher.args]."));
    v.back().choices = {{"", "Usar o ini"}, {"off", "off"}, {"fit", "fit"}, {"stage", "stage"}, {"16:9", "16:9"}, {"21:9", "21:9"}, {"32:9", "32:9"}};

    // ================= APRIMORAMENTO (§4.2/4.3) =================
    v.push_back(boolean("ec.bplus", "enabled", "enh", "B+ neural", false, "Upscale neural B+ (4x) das sprites e cenários. Exige GPU com OpenGL 4.3 ou CUDA."));
    {
        Opt o = mk("ec.bplus", "model", "enh", "Modelo B+", OType::Enum, "v1", "Modelo da rede. v3 fast: leve; v3 HQ: máxima qualidade; v2 e v1: anteriores. Só aparecem os pesos presentes em assets/models.");
        o.dyn = Dyn::Models; o.dep_id = "ec.bplus.enabled"; o.dep_val = "1"; v.push_back(o);
    }
    {
        Opt o = choice("ec.bplus", "backend", "enh", "Backend B+", "auto", {{"auto", "Automático"}, {"cuda", "CUDA (ONNX Runtime)"}, {"gl", "OpenGL compute"}}, "Onde a rede roda. CUDA não suporta de-dither nem intensidade diferente de 1.0.");
        o.dep_id = "ec.bplus.enabled"; o.dep_val = "1"; v.push_back(o);
    }
    {
        Opt o = choice("ec.bplus", "dedither", "enh", "De-dither", "off", {{"off", "Desligado"}, {"cbod", "CBOD"}, {"both", "Ambos (both)"}}, "Remove o dithering do Mega Drive antes da rede. Os modelos v2 e v3 foram treinados com \"both\".");
        o.dep_id = "ec.bplus.enabled"; o.dep_val = "1"; v.push_back(o);
    }
    {
        Opt o = flt("ec.bplus", "intensity", "enh", "Intensidade B+", "1.0", 0, 4, 0.1, "Mistura entre a imagem original (0) e a saída da rede (1); acima de 1 exagera o realce.", 1);
        o.dep_id = "ec.bplus.enabled"; o.dep_val = "1"; v.push_back(o);
    }
    {
        Opt o = flt("ec.bplus", "budget_ms", "enh", "Orçamento por quadro (ms)", "0", 0, 33, 1, "Se a rede passar desse tempo, o quadro usa a imagem sem B+. 0 = sem limite.", 0);
        o.dep_id = "ec.bplus.enabled"; o.dep_val = "1"; v.push_back(o);
    }
    {
        Opt o = mk("ec.hd", "manifest", "enh", "Modo C: conjunto de planos", OType::Enum, "", "Manifesto de arte HD pré-renderizada (AnimeSharp padrão, Flux ou um arquivo próprio).");
        o.dyn = Dyn::HdManifest; v.push_back(o);
    }
    v.push_back(choice("ec.hd", "mode", "enh", "Modo C", "off", {{"off", "Desligado"}, {"planes", "Só planos"}, {"all", "Tudo"}}, "Substitui planos (cenário) ou planos e sprites pela arte HD do manifesto."));
    {
        Opt o = mk("ec.video", "shader_preset", "enh", "Shader", OType::Enum, "off", "Cadeia de shaders RetroArch (.glslp) aplicada após o B+. Use \"Escolher .glslp…\" para um preset do RetroArch.");
        o.dyn = Dyn::ShaderPresets; v.push_back(o);
    }
    {   // preenchido pelo editor de parametros; nao aparece como linha
        Opt o = mk("ec.video", "shader_params", "enh", "Parâmetros do shader", OType::Enum, "", "Overrides de #pragma parameter (nome=valor, separados por vírgula).");
        o.dev = true; v.push_back(o);
    }

    // ================= ÁUDIO =================
    v.push_back(integer("audio", "volume", "audio", "Volume mestre", 100, 0, 100, 5, "Volume geral do jogo (0 a 100)."));
    v.push_back(choice("audio", "frequency", "audio", "Frequência", "48000", {{"32000", "32000 Hz"}, {"32040", "32040 Hz"}, {"44100", "44100 Hz"}, {"48000", "48000 Hz"}}, "Taxa de amostragem da saída de áudio."));
    v.push_back(boolean("audio", "bypass_filter", "audio", "Sem filtro do hardware", false, "Desliga o passa-baixa do Mega Drive (som mais brilhante, menos fiel ao console)."));
    v.push_back(choice("ec.audio", "remaster", "audio", "Remaster de áudio", "off", {{"off", "Original (off)"}, {"dsp", "DSP (reverb e polimento)"}, {"hybrid", "Híbrido (SoundFont + chip)"}, {"ost", "Trilha reorquestrada (ost)"}}, "off: chip original. dsp: realça o chip. hybrid: troca instrumentos FM por SoundFont mantendo o chip nos demais. ost: reorquestração completa."));
    {
        Opt o = flt("ec.audio", "bgm_vol", "audio", "Volume da música", "1.0", 0, 2, 0.05, "Ganho da trilha remasterizada (0 a 2). Não afeta o modo original.");
        o.dep_id = "ec.audio.remaster"; o.dep_val = "!off"; v.push_back(o);
    }
    {
        Opt o = flt("ec.audio", "sfx_vol", "audio", "Volume dos efeitos", "1.0", 0, 2, 0.05, "Ganho dos efeitos sonoros (0 a 2).");
        o.dep_id = "ec.audio.remaster"; o.dep_val = "!off"; v.push_back(o);
    }
    {
        Opt o = flt("ec.audio", "dsp_reverb", "audio", "Reverb (DSP)", "0.18", 0, 1, 0.02, "Quantidade de reverb do modo dsp (0 a 1).");
        o.dep_id = "ec.audio.remaster"; o.dep_val = "!off"; v.push_back(o);
    }
    {
        Opt o = cli("audio_chip_blend", "--audio-chip-blend", "audio", "Mistura do chip (modo OST)", OType::Float, "", "Quanto do chip original se mistura ao SoundFont no modo OST (0 a 1). Vazio = valor do perfil da faixa (padrão 0,18). Só vale em ost; vai por argumento (--audio-chip-blend).");
        o.min = 0; o.max = 1; o.step = 0.05; o.precision = 2; o.empty_start = "0.18";
        o.dep_id = "ec.audio.remaster"; o.dep_val = "ost";
        v.push_back(o);
    }
    v.push_back(boolean("ec.audio", "hot_reload", "audio", "Recarregar mapeamento ao vivo", true, "Recarrega voices.toml e os perfis quando mudam em disco (útil no Estúdio de Remaster)."));
    {
        Opt o = mk("ec.audio", "sf2", "audio", "SoundFont (SF2)", OType::Path, "", "Arquivo SoundFont. Vazio = assets/soundfonts/TimGM6mb.sf2.");
        v.push_back(o);
    }
    v.push_back(mk("ec.audio", "voices", "audio", "voices.toml", OType::Path, "", "Mapeamento global de instrumentos. Vazio = <perfis>/../voices.toml."));
    v.push_back(mk("ec.audio", "profiles_dir", "audio", "Pasta de perfis", OType::Folder, "", "Perfis por faixa. Vazio = assets/audio_remaster/profiles."));

    // ================= JOGO =================
    v.push_back(boolean("ec.game", "unlock_boss", "game", "Desbloquear chefe", false, "Libera o chefe (Dragon) para seleção no modo de luta."));
    v.push_back(boolean("rewind", "enabled", "game", "Rewind", true, "Permite voltar o tempo segurando a tecla de rewind."));
    {
        Opt o = integer("rewind", "depth", "game", "Profundidade do rewind", 60, 10, 600, 10, "Quantos instantâneos ficam guardados (mais = mais memória).");
        o.dep_id = "rewind.enabled"; o.dep_val = "1"; v.push_back(o);
    }
    {
        Opt o = integer("rewind", "interval", "game", "Intervalo do rewind", 6, 1, 60, 1, "Quadros entre instantâneos (menor = rewind mais fino, mais memória).");
        o.dep_id = "rewind.enabled"; o.dep_val = "1"; v.push_back(o);
    }
    v.push_back(boolean("mods.diagnostics", "enabled", "game", "Diagnóstico de replay", true, "Grava dados de diagnóstico para reproduzir e analisar partidas."));

    // ================= AVANÇADO =================
    v.push_back(cli("pacing", "--pacing", "adv", "Cadência de quadros", OType::Enum, "", "Cadência do host. fiber: o jogo roda a toda velocidade até o VBlank a cada quadro do host (quadros pesados podem disparar mais de um VBlank); accurate: limita ao orçamento de ciclos de um quadro NTSC (exatamente um VBlank por quadro). Padrão: accurate."));
    v.back().choices = {{"", "Padrão do jogo"}, {"fiber", "fiber"}, {"accurate", "accurate (ciclos exatos)"}};
    v.push_back(cli("target_fps", "--target-fps", "adv", "FPS alvo", OType::Enum, "0", "Sobrescreve a taxa de quadros alvo (acelera ou desacelera o jogo)."));
    v.back().choices = {{"0", "Padrão (60)"}, {"30", "30"}, {"50", "50"}, {"75", "75"}, {"90", "90"}, {"120", "120"}};
    v.push_back(boolean("video", "widescreen", "adv", "Widescreen legado do VDP", false, "Opção antiga do VDP; prefira o widescreen da aba Vídeo."));
    {
        Opt o = integer("video", "widescreen_cells", "adv", "Colunas extras (legado)", 8, 0, 64, 1, "Células extras de cada lado no widescreen legado do VDP.");
        o.dep_id = "video.widescreen"; o.dep_val = "1"; v.push_back(o);
    }
    v.push_back(cli("port", "--port", "adv", "Porta de debug (TCP)", OType::Enum, "0", "Abre o servidor de depuração do runner nessa porta (0 = desligado)."));
    v.back().choices = {{"0", "Desligada"}, {"4380", "4380"}, {"4381", "4381"}, {"4382", "4382"}}; v.back().dev = true;
    v.push_back(cli("fresh_evidence", "--fresh-evidence", "adv", "Descartar evidências", OType::Bool, "0", "Descarta os registros anteriores de misses de dispatch ao iniciar."));
    v.back().dev = true;
    v.push_back(cli("yield_log", "--yield-log", "adv", "Log de yields", OType::Bool, "0", "Gera yield_log_native.log com as esperas por V-Blank."));
    v.back().dev = true;
    v.push_back(cli("wav", "--wav", "adv", "Gravar áudio (WAV)", OType::SaveFile, "", "Grava o áudio da partida num arquivo WAV."));
    v.back().dev = true;
    return v;
}

bool truthy(const std::string& s) {
    return s == "1" || s == "true" || s == "on" || s == "yes" || s == "True" || s == "ON";
}
std::string norm_bool(const std::string& s) { return truthy(s) ? "1" : "0"; }

bool near_eq(const std::string& a, const std::string& b) {
    char* ea = nullptr; char* eb = nullptr;
    double x = strtod(a.c_str(), &ea), y = strtod(b.c_str(), &eb);
    if (ea != a.c_str() && *ea == 0 && eb != b.c_str() && *eb == 0) return std::fabs(x - y) < 1e-6;
    return a == b;
}
}  // namespace

const std::vector<Opt>& schema() {
    static const std::vector<Opt> s = build();
    return s;
}

const Opt* schema_find(const std::string& id) {
    for (const Opt& o : schema()) if (o.id == id) return &o;
    return nullptr;
}

// ---------------------------------------------------------------------------
bool Config::load(const fs::path& p) { return ini_.load(p); }

std::string Config::get(const Opt& o) const {
    std::string v;
    if (!ini_.get(o.section, o.key, v) || (v.empty() && o.type != OType::Path && o.type != OType::Enum)) return o.def;
    if (o.type == OType::Bool) return norm_bool(v);
    if (o.type == OType::Enum && o.section == "launcher.args" && !o.choices.empty()) {   // valor invalido no ini -> "Padrao"
        bool ok = false;
        for (auto& c : o.choices) ok |= (c.value == v);
        if (!ok) return o.def;
    }
    return v;
}
std::string Config::get(const std::string& id) const {
    const Opt* o = schema_find(id);
    return o ? get(*o) : "";
}
bool Config::is_modified(const Opt& o) const { return !near_eq(get(o), o.def); }

bool Config::set(const Opt& o, const std::string& v) {
    if (near_eq(v, o.def)) { ini_.remove(o.section, o.key); }
    else ini_.set(o.section, o.key, v);
    return save();
}
bool Config::set(const std::string& id, const std::string& v) {
    if (const Opt* o = schema_find(id)) return set(*o, v);
    return false;
}

bool Config::enabled(const Opt& o) const {
    if (o.dep_id.empty()) return true;
    std::string cur = get(o.dep_id);
    if (!o.dep_val.empty() && o.dep_val[0] == '!') return cur != o.dep_val.substr(1);
    return cur == o.dep_val;
}

std::vector<std::string> Config::build_args() const {
    std::vector<std::string> a;
    for (const Opt& o : schema()) {
        if (o.section != "launcher.args") continue;
        std::string v = get(o);
        if (near_eq(v, o.def) || !enabled(o)) continue;
        if (o.type == OType::Bool) { if (v == "1") a.push_back(o.cli_flag); }
        else if (!v.empty()) { a.push_back(o.cli_flag); a.push_back(v); }
    }
    return a;
}

bool Config::ensure_skip_launcher() {
    std::string v;
    if (ini_.get("launcher", "skip_launcher", v) && v == "1") return true;
    ini_.set("launcher", "skip_launcher", "1");
    return save();
}

// ---------------------------------------------------------------------------
std::vector<Choice> dyn_choices(const App& app, Dyn d) {
    std::vector<Choice> r;
    std::error_code ec;
    if (d == Dyn::Models) {
        static const struct { const char* v; const char* file; const char* label; } m[] = {
            {"v3_fast", "ec_bplus_v3_fast_weights.bin", "v3 fast (leve)"},
            {"v3_hq", "ec_bplus_v3_hq_weights.bin", "v3 HQ (máxima qualidade)"},
            {"v3_hq_janai", "ec_bplus_v3_hq_janai_weights.bin", "v3 HQ JaNai"},
            {"v2", "ec_bplus_v2_flux_weights.bin", "v2 Flux"},
            {"v1", "ec_bplus_espcn4x_weights.bin", "v1 ESPCN 4x"}};
        for (auto& e : m)
            if (fs::exists(app.root_dir / "assets" / "models" / e.file, ec)) r.push_back({e.v, e.label});
    } else if (d == Dyn::ShaderPresets) {
        r.push_back({"off", "Desligado"});
        std::vector<std::string> names;
        for (auto& e : fs::directory_iterator(app.root_dir / "assets" / "shaders" / "presets", ec))
            if (e.path().extension() == ".glslp") names.push_back(path_to_utf8(e.path().stem()));
        std::sort(names.begin(), names.end());
        for (auto& n : names) r.push_back({n, n});
    } else if (d == Dyn::HdManifest) {
        r.push_back({"", "Nenhum"});
        if (fs::exists(app.root_dir / "pipeline" / "manifest.json", ec)) r.push_back({"animesharp", "AnimeSharp (padrão)"});
        if (fs::exists(app.root_dir / "pipeline" / "manifest_flux.json", ec)) r.push_back({"flux", "Flux"});
    }
    return r;
}

std::vector<Choice> Config::choices(const Opt& o, const App& app) const {
    std::vector<Choice> c = o.dyn == Dyn::None ? o.choices : dyn_choices(app, o.dyn);
    std::string cur = get(o);
    bool found = false;
    for (auto& x : c) found |= (x.value == cur);
    if (!found) {
        std::string lab = cur;
        // caminho longo: mostra so o nome do arquivo
        if (cur.find_first_of("/\\") != std::string::npos) lab = path_to_utf8(utf8_to_path(cur).filename());
        c.push_back({cur, lab});
    }
    return c;
}

// ---------------------------------------------------------------------------
const std::vector<Profile>& profiles() {
    static const std::vector<Profile> p = {
        {"original", "Original", "Sem aprimoramentos: o jogo como no Mega Drive.", "≈ 0 ms",
         {{"ec.bplus.enabled", "0"}, {"ec.video.shader_preset", "off"}, {"ec.hd.mode", "off"}, {"ec.audio.remaster", "off"}}},
        {"remaster_leve", "Remaster Leve", "B+ v3 fast (1,9 ms em 4:3 / 2,9 ms em widescreen) + shader remaster-crisp (≤ 0,1 ms) + áudio híbrido.", "≈ 2–3 ms",
         {{"ec.bplus.enabled", "1"}, {"ec.bplus.model", "v3_fast"}, {"ec.bplus.dedither", "both"}, {"ec.video.shader_preset", "remaster-crisp"}, {"ec.hd.mode", "off"}, {"ec.audio.remaster", "hybrid"}}},
        {"remaster_maximo", "Remaster Máximo", "B+ v3 HQ (4,0 ms em 4:3 / 6,3 ms em widescreen) + Modo C (desprezível) + shader remaster-hq (≤ 0,1 ms) + áudio híbrido.", "≈ 4–7 ms",
         {{"ec.bplus.enabled", "1"}, {"ec.bplus.model", "v3_hq"}, {"ec.bplus.dedither", "both"}, {"ec.video.shader_preset", "remaster-hq"}, {"ec.hd.manifest", "animesharp"}, {"ec.hd.mode", "planes"}, {"ec.audio.remaster", "hybrid"}}},
        {"sem_neural", "Sem neural (GPU fraca)", "Sem B+: só o shader lite-scalefx (≈ 0,1–0,2 ms) e áudio DSP.", "≈ 0,2 ms",
         {{"ec.bplus.enabled", "0"}, {"ec.video.shader_preset", "lite-scalefx"}, {"ec.hd.mode", "off"}, {"ec.audio.remaster", "dsp"}}},
    };
    return p;
}

bool apply_profile(Config& cfg, const std::string& id) {
    for (const Profile& p : profiles()) {
        if (p.id != id) continue;
        for (auto& kv : p.values) cfg.set(kv.first, kv.second);
        return true;
    }
    return false;
}

std::string active_profile_id(const Config& cfg) {
    for (const Profile& p : profiles()) {
        bool all = true;
        for (auto& kv : p.values) all &= near_eq(cfg.get(kv.first), kv.second);
        if (all) return p.id;
    }
    return "";
}
