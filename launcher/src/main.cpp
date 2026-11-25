// EternalChampions.exe - launcher. Ver launcher/PLAN.md.
//
// Uso normal:   EternalChampions.exe
// Captura:      EternalChampions.exe --shot out.png --screen <splash|main|rom|about|placeholder>
//                                    [--frames N] [--size WxH]
// (renderiza a tela N frames a 60 fps simulados e grava o canvas 1280x720 em PNG, sem interacao)
#include "app.h"
#include "ini.h"
#include "png_io.h"
#include "screens/screens.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

struct Args {
    std::string shot, screen = "main";
    int frames = 30, w = 1280, h = 720;
    bool autoplay = false;
    std::string game_args, selftest_ini, apply_profile;
    int tab = 0, focus = 0, state = 0;
    std::string ini, controls_preset;
    bool dev = false;
};

static Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (!strcmp(argv[i], "--shot")) a.shot = next();
        else if (!strcmp(argv[i], "--screen")) a.screen = next();
        else if (!strcmp(argv[i], "--autoplay")) a.autoplay = true;
        else if (!strcmp(argv[i], "--selftest-ini")) a.selftest_ini = next();
        else if (!strcmp(argv[i], "--apply-profile")) a.apply_profile = next();
        else if (!strcmp(argv[i], "--tab")) a.tab = atoi(next());
        else if (!strcmp(argv[i], "--focus")) a.focus = atoi(next());
        else if (!strcmp(argv[i], "--dev")) a.dev = true;
        else if (!strcmp(argv[i], "--state")) a.state = atoi(next());
        else if (!strcmp(argv[i], "--ini")) a.ini = next();
        else if (!strcmp(argv[i], "--apply-controls-preset")) a.controls_preset = next();
        else if (!strcmp(argv[i], "--game-args")) a.game_args = next();
        else if (!strcmp(argv[i], "--frames")) a.frames = std::max(1, atoi(next()));
        else if (!strcmp(argv[i], "--size")) {
            int w = 0, h = 0;
            if (sscanf(next(), "%dx%d", &w, &h) == 2 && w > 0 && h > 0) { a.w = w; a.h = h; }
        }
    }
    return a;
}

static std::unique_ptr<Screen> make_named(const std::string& n, App& app) {
    if (n == "splash") return make_splash();
    if (n == "about") return make_about_screen();
    if (n == "placeholder") return make_placeholder("V\xC3\x8D""DEO");
    if (n == "rom") return make_rom_screen();
    if (n == "controls") return make_controls_screen();
    if (n == "video") return make_video_screen();
    if (n == "enh") return make_enhance_screen();
    if (n == "audio") return make_audio_screen();
    if (n == "game") return make_game_screen();
    if (n == "saves") return make_saves_screen();
    if (n == "adv") return make_advanced_screen();
    if (n == "confirm") return make_confirm("RESTAURAR PADRÕES", "Voltar todas as opções de Vídeo aos valores padrão? Isso não pode ser desfeito.", "Restaurar", nullptr);
    (void)app;
    return make_main_menu();
}

static int run_shot(App& app, const Args& a) {
    app.shot_mode = true;
    if (!a.controls_preset.empty()) apply_controls_preset(app, 0, a.controls_preset);   // (use --ini p/ nao tocar no settings.ini real)
    // pilha: para rom/about/placeholder, o menu fica por baixo (como na navegacao real)
    app.shot_page = a.tab; app.shot_focus = a.focus; app.shot_state = a.state; app.dev_visible = a.dev;
    if (a.screen != "splash" && a.screen != "main") app.push(make_main_menu());
    if (a.screen == "confirm") app.push(make_video_screen());
    app.push(make_named(a.screen, app));
    app.apply_pending();
    InputState idle;
    for (int f = 0; f < a.frames; ++f) {
        app.time = f / 60.f;
        app.update(idle, 1.f / 60.f);
        app.render.begin();
        app.draw();
        app.render.end(false);
    }
    std::vector<uint8_t> px;
    if (!app.render.read_canvas(px)) { fprintf(stderr, "read_canvas falhou: %s\n", SDL_GetError()); return 2; }
    std::filesystem::path out = utf8_to_path(a.shot);
    std::error_code ec;
    if (out.has_parent_path()) std::filesystem::create_directories(out.parent_path(), ec);
    if (!png_write_rgba(a.shot, px.data(), CANVAS_W, CANVAS_H)) { fprintf(stderr, "falha ao gravar %s\n", a.shot.c_str()); return 3; }
    return 0;
}

static int run_interactive(App& app, const Args& a) {
    if (!a.apply_profile.empty()) {
        bool ok = apply_profile(app.cfg, a.apply_profile);
        app.log(std::string("--apply-profile ") + a.apply_profile + (ok ? " ok" : " DESCONHECIDO"));
    }
    if (!a.controls_preset.empty()) { apply_controls_preset(app, 0, a.controls_preset); app.log("--apply-controls-preset " + a.controls_preset); }
    app.autoplay = a.autoplay;
    app.extra_game_args = a.game_args;
    app.push(a.autoplay ? make_main_menu() : make_splash());
    app.apply_pending();
    if (a.autoplay) request_play(app);
    Uint64 freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
    while (!app.quit) {
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - last) / freq);
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        app.time += dt;
        app.input.poll(app.render, dt);
        const InputState& in = app.input.state();
        if (in.quit) app.quit = true;
        app.update(in, dt);
        if (!app.has_screens()) break;

        Uint32 wf = SDL_GetWindowFlags(app.render.window);
        if (app.game_running || (wf & SDL_WINDOW_MINIMIZED)) {   // idle barato: sem desenhar
            SDL_Delay(60);
            continue;
        }
        app.render.begin();
        app.draw();
        app.render.end(true);
        // sem vsync (driver recusou): limita a ~60 fps
        double ms = (double)(SDL_GetPerformanceCounter() - now) * 1000.0 / freq;
        if (ms < 8.0) SDL_Delay(8);
    }
    return 0;
}

int main(int argc, char** argv) {
    Args a = parse_args(argc, argv);
    if (!a.selftest_ini.empty()) return ini_selftest(a.selftest_ini);
    bool shot = !a.shot.empty();
    App app;
    app.ini_override = a.ini;
    if (!app.init(shot, shot ? a.w : 1280, shot ? a.h : 720)) {
        fprintf(stderr, "Falha ao iniciar o launcher: %s\n", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Eternal Champions", "Falha ao iniciar o launcher (SDL).", nullptr);
        return 1;
    }
    int rc = shot ? run_shot(app, a) : run_interactive(app, a);
    app.shutdown();
    return rc;
}
