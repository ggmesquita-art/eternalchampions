// VÍDEO, APRIMORAMENTO, ÁUDIO, JOGO e AVANÇADO (L3) - montadas a partir do esquema (schema.cpp).
#include "../launch.h"
#include "../platform.h"
#include "../shader_params.h"
#include "../widgets.h"
#include "options_screen.h"
#include "screens.h"
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

namespace {

void add_opts(App& app, Page& pg, const std::vector<std::string>& ids) {
    for (auto& id : ids)
        if (const Opt* o = schema_find(id)) pg.rows.push_back(option_row(app, *o));
}

// ---------------------------------------------------------------- VÍDEO
std::vector<Page> build_video(App& app) {
    Page p{"Vídeo", {}};
    add_opts(app, p, {"video.window_scale", "video.fullscreen", "video.linear_filter", "video.integer_scale",
                      "video.internal_scale", "video.presentation_rate"});
    p.rows.push_back(header_row("Widescreen"));
    add_opts(app, p, {"mods.widescreen.enabled", "mods.widescreen.aspect", "args.widescreen"});
    p.rows.back().rebuild = false;
    p.rows.push_back(header_row("Imagem entrelaçada"));
    add_opts(app, p, {"args.interlace"});
    return {p};
}

// ---------------------------------------------------------------- JOGO
std::vector<Page> build_game(App& app) {
    Page p{"Jogo", {}};
    p.rows.push_back(header_row("Partida"));
    add_opts(app, p, {"ec.game.unlock_boss"});
    p.rows.push_back(header_row("Rewind"));
    add_opts(app, p, {"rewind.enabled", "rewind.depth", "rewind.interval"});
    p.rows.push_back(header_row("Diagnóstico"));
    add_opts(app, p, {"mods.diagnostics.enabled"});
    return {p};
}

// ---------------------------------------------------------------- ÁUDIO
std::vector<Page> build_audio(App& app) {
    Page out{"Saída", {}}, rem{"Remaster", {}}, files{"Arquivos", {}};
    add_opts(app, out, {"audio.volume", "audio.frequency", "audio.bypass_filter"});
    add_opts(app, rem, {"ec.audio.remaster", "ec.audio.bgm_vol", "ec.audio.sfx_vol", "ec.audio.dsp_reverb", "ec.audio.chip_blend", "ec.audio.hot_reload"});
    rem.rows[0].rebuild = false;
    {   // descricao do modo atual
        std::string m = app.cfg.get("ec.audio.remaster");
        rem.rows.push_back(header_row("Sobre o modo"));
        const char* d = m == "dsp" ? "DSP: mantém o chip FM original e aplica reverb e realce."
                      : m == "hybrid" ? "Híbrido: instrumentos FM mapeados tocam por SoundFont; o resto segue no chip."
                      : m == "ost" ? "OST: reorquestração completa por SoundFont, com mistura do chip por faixa."
                      : "Original: o som exato do Mega Drive (YM2612 + PSG).";
        rem.rows.push_back(info_row(d));
    }
    add_opts(app, files, {"ec.audio.sf2", "ec.audio.voices", "ec.audio.profiles_dir"});
    {
        std::string sf = app.cfg.get("ec.audio.sf2");
        fs::path p = sf.empty() ? app.root_dir / "assets" / "soundfonts" / "TimGM6mb.sf2" : utf8_to_path(sf);
        std::error_code ec;
        bool ok = fs::exists(p, ec);
        Row r;
        r.label = "Status do SoundFont"; r.arrows = false;
        r.help = "Verifica se o arquivo SF2 existe. Sem ele, os modos hybrid e ost voltam para o DSP.";
        r.value = [ok](App&) { return ok ? std::string("✓ encontrado") : std::string("✗ ausente"); };
        r.tint = ok ? Color::hex(0x6fe38a) : Color::hex(0xff6a5a);
        r.activate = [](App&) {};
        files.rows.push_back(r);
        files.rows.push_back(header_row("Estúdio de Remaster"));
        files.rows.push_back(action_row("Abrir o Estúdio de Remaster", "Mapeia os instrumentos FM do jogo para o SoundFont, com prévia e A/B. Em construção.",
                                        [](App& a) { a.push(make_placeholder("ESTÚDIO DE REMASTER")); }));
    }
    return {out, rem, files};
}

// ---------------------------------------------------------------- AVANÇADO
void confirm_reset(App& app, const std::string& what, const std::string& tab) {
    app.push(make_confirm("RESTAURAR PADRÕES", "Voltar " + what + " aos valores padrão? Isso não pode ser desfeito.", "Restaurar",
                          [tab](App& a) { reset_options(a, tab); a.toast("Padrões restaurados."); }));
}

std::vector<Page> build_advanced(App& app) {
    Page ex{"Execução", {}}, dev{"Desenvolvedor", {}}, files{"Arquivos e padrões", {}};
    ex.rows.push_back(header_row("Cadência"));
    add_opts(app, ex, {"args.pacing", "args.target_fps"});
    ex.rows.push_back(header_row("Legado do VDP"));
    add_opts(app, ex, {"video.widescreen", "video.widescreen_cells"});

    {   // toggle da area de desenvolvedor
        Row t;
        t.label = "Mostrar opções de desenvolvedor";
        t.help = "Porta de debug, descarte de evidências, log de yields e gravação de áudio (WAV). Pensado para testes; fica oculto por padrão.";
        t.value = [](App& a) { return a.dev_visible ? std::string("Sim") : std::string("Não"); };
        t.adjust = [](App& a, int) { a.dev_visible = !a.dev_visible; };
        t.rebuild = true;
        dev.rows.push_back(t);
        if (app.dev_visible) {
            for (const Opt& o : schema())
                if (o.tab == "adv" && o.dev) dev.rows.push_back(option_row(app, o));
        }
    }

    files.rows.push_back(header_row("Arquivos"));
    files.rows.push_back(action_row("Abrir settings.ini", "Abre o arquivo de configurações no editor padrão.", [](App& a) { shell_open(a.settings_ini); }));
    files.rows.push_back(action_row("Abrir pasta de logs", "Abre build\\logs (launcher.log, last_error.log e logs do jogo).", [](App& a) {
        std::error_code ec; fs::create_directories(a.logs_dir, ec); shell_open(a.logs_dir); }));
    files.rows.push_back(header_row("Restaurar padrões"));
    struct { const char* label; const char* what; const char* tab; } rs[] = {
        {"Restaurar Vídeo", "as opções de Vídeo", "video"}, {"Restaurar Aprimoramento", "as opções de Aprimoramento", "enh"},
        {"Restaurar Áudio", "as opções de Áudio", "audio"}, {"Restaurar Jogo", "as opções de Jogo", "game"},
        {"Restaurar Avançado", "as opções de Avançado", "adv"}, {"Restaurar TUDO", "TODAS as opções do launcher", ""}};
    for (auto& r : rs) {
        std::string what = r.what, tab = r.tab;
        files.rows.push_back(action_row(r.label, "Pede confirmação antes de restaurar. Controles e chaves desconhecidas do ini não são tocados.",
                                        [what, tab](App& a) { confirm_reset(a, what, tab); }));
    }
    return {ex, dev, files};
}

// ---------------------------------------------------------------- APRIMORAMENTO
std::vector<Page> build_enhance(App& app) {
    Page prof{"Perfis", {}}, bp{"B+ neural", {}}, hd{"Modo C", {}}, sh{"Shader", {}};

    // --- perfis de qualidade
    std::string active = active_profile_id(app.cfg);
    prof.rows.push_back(info_row("Aplicam várias opções de uma vez; depois você pode ajustar tudo. O custo é uma estimativa."));
    for (const Profile& pr : profiles()) {
        Row r;
        r.label = pr.label; r.arrows = false; r.rebuild = true;
        bool on = pr.id == active;
        r.help = pr.summary + " Custo estimado: " + pr.cost + ".";
        r.value = [pr, on](App&) { return pr.cost + (on ? "   ✓ ativo" : ""); };
        if (on) r.tint = Color::hex(0x6fe38a);
        std::string id = pr.id, label = pr.label;
        r.activate = [id, label](App& a) {
            apply_profile(a.cfg, id);
            a.log("perfil aplicado: " + id);
            a.toast("Perfil \"" + label + "\" aplicado.");
        };
        prof.rows.push_back(r);
    }
    prof.rows.push_back(header_row("Perfil atual"));
    prof.rows.push_back(info_row(active.empty() ? "Personalizado (as opções não batem com nenhum perfil)." : "Perfil ativo: " + [&] {
        for (auto& p : profiles()) if (p.id == active) return p.label; return std::string(); }()));

    // --- B+
    add_opts(app, bp, {"ec.bplus.enabled", "ec.bplus.model", "ec.bplus.backend", "ec.bplus.dedither", "ec.bplus.intensity", "ec.bplus.budget_ms"});
    for (auto& r : bp.rows) r.rebuild = true;
    {
        std::string model = app.cfg.get("ec.bplus.model"), ded = app.cfg.get("ec.bplus.dedither");
        bool v23 = model.rfind("v3", 0) == 0 || model == "v2";
        bp.rows.push_back(header_row("Avisos"));
        if (app.cfg.get("ec.bplus.enabled") == "1" && v23 && ded != "both")
            bp.rows.push_back(info_row("⚠ Os modelos v2/v3 exigem o de-dither \"Ambos (both)\"; com outro valor a imagem sai pior."));
        else if (app.cfg.get("ec.bplus.enabled") == "1" && app.cfg.get("ec.bplus.backend") == "cuda")
            bp.rows.push_back(info_row("⚠ O backend CUDA não suporta de-dither nem intensidade diferente de 1.0."));
        else
            bp.rows.push_back(info_row("Nenhum aviso."));
    }

    // --- Modo C
    add_opts(app, hd, {"ec.hd.manifest", "ec.hd.mode"});
    hd.rows.push_back(header_row("Sobre"));
    hd.rows.push_back(info_row("O Modo C troca planos (e sprites) pela arte HD pré-renderizada do manifesto escolhido."));
    if (app.cfg.get("ec.hd.mode") != "off" && app.cfg.get("ec.hd.manifest").empty())
        hd.rows.push_back(info_row("⚠ Escolha um conjunto de planos; sem manifesto o Modo C não tem o que mostrar."));

    // --- Shader
    {
        const Opt* po = schema_find("ec.video.shader_preset");
        Row pr = option_row(app, *po);
        auto adj = pr.adjust;
        pr.adjust = [adj](App& a, int d) {
            std::string before = a.cfg.get("ec.video.shader_preset");
            adj(a, d);
            if (a.cfg.get("ec.video.shader_preset") != before) a.cfg.set("ec.video.shader_params", "");   // parametros sao por preset
        };
        pr.rebuild = true;
        sh.rows.push_back(pr);
        sh.rows.push_back(action_row("Escolher .glslp…", "Usa um preset do RetroArch (.glslp) de qualquer pasta.", [](App& a) {
            std::string f = pick_open_file(a, L"Escolher preset de shader", L"Presets RetroArch (*.glslp)\0*.glslp\0Todos (*.*)\0*.*\0", a.root_dir / "assets" / "shaders");
            if (!f.empty()) { a.cfg.set("ec.video.shader_preset", f); a.cfg.set("ec.video.shader_params", ""); }
        }, true));

        std::string preset = app.cfg.get("ec.video.shader_preset");
        std::vector<ShaderParam> params;
        bool ok = load_shader_params(app.root_dir, preset, params);
        sh.rows.push_back(header_row("Parâmetros do preset"));
        if (!ok) sh.rows.push_back(info_row(preset == "off" ? "Escolha um preset para editar seus parâmetros." : "Preset não encontrado."));
        else if (params.empty()) sh.rows.push_back(info_row("Este preset não tem parâmetros editáveis."));
        for (const ShaderParam& sp : params) {
            Row r;
            r.label = sp.desc; r.help = sp.name + ": padrão " + std::to_string(sp.def).substr(0, std::to_string(sp.def).find('.') + 3) + ", faixa " +
                                        std::to_string(sp.min).substr(0, std::to_string(sp.min).find('.') + 3) + " a " + std::to_string(sp.max).substr(0, std::to_string(sp.max).find('.') + 3) + ".";
            auto cur = [sp](App& a) {
                auto m = parse_param_string(a.cfg.get("ec.video.shader_params"));
                auto it = m.find(sp.name);
                return it == m.end() ? sp.def : it->second;
            };
            auto put = [sp](App& a, double v) {
                auto m = parse_param_string(a.cfg.get("ec.video.shader_params"));
                if (std::fabs(v - sp.def) < 1e-6) m.erase(sp.name); else m[sp.name] = v;
                a.cfg.set("ec.video.shader_params", format_param_string(m));
            };
            r.value = [cur, sp](App& a) { char b[32]; snprintf(b, sizeof b, "%.*f", sp.step >= 1 ? 0 : (sp.step >= 0.1 ? 1 : 2), cur(a)); return std::string(b); };
            r.adjust = [cur, put, sp](App& a, int d) {
                double v = std::min(sp.max, std::max(sp.min, cur(a) + d * sp.step));
                v = std::round(v / sp.step) * sp.step;
                put(a, std::min(sp.max, std::max(sp.min, v)));
            };
            r.reset = [put, sp](App& a) { put(a, sp.def); };
            r.modified = [cur, sp](App& a) { return std::fabs(cur(a) - sp.def) > 1e-6; };
            sh.rows.push_back(r);
        }
    }
    return {prof, bp, hd, sh};
}

}  // namespace

std::unique_ptr<Screen> make_video_screen() { return make_options_screen("VÍDEO", build_video); }
std::unique_ptr<Screen> make_game_screen() { return make_options_screen("JOGO", build_game); }
std::unique_ptr<Screen> make_audio_screen() { return make_options_screen("ÁUDIO", build_audio); }
std::unique_ptr<Screen> make_advanced_screen() { return make_options_screen("AVANÇADO", build_advanced); }
std::unique_ptr<Screen> make_enhance_screen() { return make_options_screen("APRIMORAMENTO", build_enhance); }
