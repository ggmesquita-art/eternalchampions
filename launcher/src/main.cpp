// EternalChampions.exe - launcher. Ver launcher/PLAN.md.
//
// Uso normal:   EternalChampions.exe
// Captura:      EternalChampions.exe --shot out.png --screen <splash|main|rom|about|placeholder>
//                                    [--frames N] [--size WxH]
// (renderiza a tela N frames a 60 fps simulados e grava o canvas 1280x720 em PNG, sem interacao)
#include "app.h"
#include "ini.h"
#include "studio.h"
#include "widgets.h"
#include "png_io.h"
#include "screens/screens.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <windows.h>

struct Args {
    std::string shot, screen = "main";
    int frames = 30, w = 1280, h = 720;
    bool autoplay = false;
    std::string game_args, selftest_ini, apply_profile;
    int tab = 0, focus = 0, state = 0;
    std::string ini, controls_preset, dumps, voices, root, out, keys;
    bool selftest_studio = false, run_preview = false, real_ini = false;
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
        else if (!strcmp(argv[i], "--run-preview")) a.run_preview = true;
        else if (!strcmp(argv[i], "--real-ini")) a.real_ini = true;   // opt-in explicito: flags de teste gravam no settings.ini REAL
        else if (!strcmp(argv[i], "--selftest-studio")) a.selftest_studio = true;
        else if (!strcmp(argv[i], "--dumps")) a.dumps = next();
        else if (!strcmp(argv[i], "--voices")) a.voices = next();
        else if (!strcmp(argv[i], "--root")) a.root = next();
        else if (!strcmp(argv[i], "--out")) a.out = next();
        else if (!strcmp(argv[i], "--state")) a.state = atoi(next());
        else if (!strcmp(argv[i], "--ini")) a.ini = next();
        else if (!strcmp(argv[i], "--apply-controls-preset")) a.controls_preset = next();
        else if (!strcmp(argv[i], "--game-args")) a.game_args = next();
        else if (!strcmp(argv[i], "--keys")) a.keys = next();
        else if (!strcmp(argv[i], "--frames")) a.frames = std::max(1, atoi(next()));
        else if (!strcmp(argv[i], "--size")) {
            int w = 0, h = 0;
            if (sscanf(next(), "%dx%d", &w, &h) == 2 && w > 0 && h > 0) { a.w = w; a.h = h; }
        }
    }
    return a;
}

// --screen fonttest: amostra da fonte pixel do jogo com os acentos compostos (diagnostico)
class FontTest : public Screen {
public:
    void update(App&, const InputState&, float) override {}
    void draw(App& app) override {
        app.theme.draw_stone_bg(app.render, 0);
        const Font* f = app.theme.fonts.pixel ? app.theme.fonts.pixel.get() : app.theme.fonts.menu.get();
        const char* lines[] = {"ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz", "0123456789 !?.,:;-+()",
                               "ÁÀÂÃÉÊÍÓÔÕÚÜÇ", "áàâãéêíóôõúüç", "VÍDEO ÁUDIO ESTÚDIO AVANÇADO", "Configurações: não é ação"};
        float y = 20;
        if (app.shot_focus == 7) {   // --focus 7: mesmos itens do menu, via MenuList
            MenuList m;
            for (const char* s : {"JOGAR", "ROM", "VÍDEO", "APRIMORAMENTO", "ÁUDIO", "ESTÚDIO DE REMASTER", "AVANÇADO"}) m.items.push_back({s, true, 0});
            m.y = 100; m.row_h = 34;
            m.draw(app);
            TextStyle a = TextStyle::solid(Color::hex(0xffffff)); a.scale = 4; a.align = Align::Center;
            draw_text(app.render, *app.theme.fonts.pixel, 400, 400, "ÁUDIO VÍDEO", a);
            TextStyle b = app.theme.item_style(false, true); b.outline_px = 0; b.shadow_dx = b.shadow_dy = 0; b.scale = 4; b.align = Align::Center;
            draw_text(app.render, *app.theme.fonts.pixel, 400, 500, "ÁUDIO VÍDEO", b);
            draw_text(app.render, *app.theme.fonts.pixel, 900, 400, "ÁUDIO", a);
            draw_text(app.render, *app.theme.fonts.pixel, 900, 434, "AUDIO", a);
            return;
        }
        for (const char* l : lines) {
            TextStyle st = TextStyle::solid(Color::hex(0xffffff)); st.scale = 4; st.align = Align::Center;
            draw_text(app.render, *f, CANVAS_W / 2.f, y, l, st);
            y += 90;
        }
    }
};

static std::unique_ptr<Screen> make_named(const std::string& n, App& app) {
    if (n == "splash") return make_splash();
    if (n == "about") return make_about_screen();
    if (n == "placeholder") return make_placeholder("V\xC3\x8D""DEO");
    if (n == "fonttest") return std::make_unique<FontTest>();
    if (n == "studio") return make_studio_screen();
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
    if (!a.apply_profile.empty()) apply_profile(app.cfg, a.apply_profile);   // (so com --ini ou ini de teste)
    if (a.run_preview) {   // L6: roda a pre-visualizacao de verdade e espera (a UI normal nao bloqueia)
        app.preview.start(app);
        for (int i = 0; i < 4800 && app.preview.state() == PreviewJob::State::Running; ++i) { app.preview.poll(app, 0.025f); SDL_Delay(25); }
        fprintf(stdout, "preview: %s - %s\n", app.preview.state() == PreviewJob::State::Done ? "OK" : "FALHOU", app.preview.message().c_str());
    }
    app.shot_page = a.tab; app.shot_focus = a.focus; app.shot_state = a.state; app.dev_visible = a.dev;
    if (a.screen != "splash" && a.screen != "main") app.push(make_main_menu());
    if (a.screen == "confirm") app.push(make_video_screen());
    app.push(make_named(a.screen, app));
    app.apply_pending();
    InputState idle;
    if (!a.keys.empty()) {   // --keys L,R,U,D,E,Q,A,X: entrada simulada (1 quadro por tecla) com o tempo de cada update
        static const struct { const char* n; Action act; } km[] = {{"L", A_LEFT}, {"R", A_RIGHT}, {"U", A_UP}, {"D", A_DOWN},
            {"E", A_TABNEXT}, {"Q", A_TABPREV}, {"A", A_ACCEPT}, {"X", A_RESET}};
        std::string tok; std::istringstream ks(a.keys);
        Uint64 freq = SDL_GetPerformanceFrequency();
        double worst = 0;
        while (std::getline(ks, tok, ',')) {
            InputState in;
            for (auto& k : km) if (tok == k.n) { in.pressed[k.act] = in.held[k.act] = true; }
            Uint64 t0 = SDL_GetPerformanceCounter();
            app.update(in, 1.f / 60.f);
            app.render.begin(); app.draw(); app.render.end(false);
            double ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / freq;
            worst = std::max(worst, ms);
            fprintf(stdout, "key %-2s %8.2f ms\n", tok.c_str(), ms);
        }
        fprintf(stdout, "keys: pior %.2f ms\n", worst);
        fflush(stdout);
    }
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
    // --keys no modo interativo (diagnostico): abre --screen e injeta as teclas no laco real, medindo cada quadro
    const bool scripted = !a.keys.empty() && !a.screen.empty();
    std::vector<std::string> script;
    if (scripted) {
        std::string tok; std::istringstream ks(a.keys);
        while (std::getline(ks, tok, ',')) script.push_back(tok);
        app.push(make_main_menu());
        app.push(make_named(a.screen, app));
    } else {
        app.push(a.autoplay ? make_main_menu() : make_splash());
    }
    app.apply_pending();
    if (a.autoplay) request_play(app);
    Uint64 freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
    int frame = 0; size_t next_key = 0; double worst = 0;
    while (!app.quit) {
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - last) / freq);
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        app.time += dt;
        app.input.poll(app.render, dt);
        InputState in = app.input.state();
        const char* injected = nullptr;
        if (scripted) {
            ++frame;
            if (frame > 30 && (frame - 30) % 6 == 0 && next_key < script.size()) {
                static const struct { const char* n; Action act; } km[] = {{"L", A_LEFT}, {"R", A_RIGHT}, {"U", A_UP}, {"D", A_DOWN},
                    {"E", A_TABNEXT}, {"Q", A_TABPREV}, {"A", A_ACCEPT}, {"X", A_RESET}};
                injected = script[next_key].c_str();
                for (auto& k : km) if (script[next_key] == k.n) in.pressed[k.act] = in.held[k.act] = true;
                ++next_key;
            }
            if (next_key >= script.size() && frame > 30 + 6 * (int)script.size() + 90) {
                fprintf(stdout, "interativo: pior quadro com tecla %.2f ms\n", worst); fflush(stdout);
                app.quit = true;
            }
        }
        if (in.quit) app.quit = true;
        app.update(in, dt);
        if (injected) {
            double ms = (double)(SDL_GetPerformanceCounter() - now) * 1000.0 / freq;
            worst = std::max(worst, ms);
            fprintf(stdout, "key %-2s %8.2f ms\n", injected, ms); fflush(stdout);
        }
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
    if (a.selftest_studio) return studio_selftest(a.root, a.dumps, a.out);
    bool shot = !a.shot.empty();
    App app;
    app.ini_override = a.ini;
    app.shot_mode = shot;   // antes do init: a extracao da arte nao dispara em capturas
    // flags de teste que gravam no ini (--apply-profile / --apply-controls-preset / --run-preview): sempre numa COPIA
    // temporaria do settings.ini, nunca no real (a menos que se passe --ini <arq> ou --real-ini explicitamente)
    std::filesystem::path temp_ini;
    if ((!a.apply_profile.empty() || !a.controls_preset.empty() || a.run_preview) && a.ini.empty() && !a.real_ini) {
        wchar_t exe[MAX_PATH * 2];
        DWORD n = GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof exe / sizeof exe[0]));
        std::error_code ec;
        std::filesystem::path real = std::filesystem::path(std::wstring(exe, n)).parent_path() / "settings.ini";
        temp_ini = std::filesystem::temp_directory_path(ec) / ("ec_launcher_test_" + std::to_string((unsigned long long)GetTickCount64()) + ".ini");
        if (std::filesystem::exists(real, ec)) std::filesystem::copy_file(real, temp_ini, std::filesystem::copy_options::overwrite_existing, ec);
        app.ini_override = path_to_utf8(temp_ini);
    }
    app.studio_dumps = a.dumps; app.studio_voices = a.voices;
    if (!app.init(shot, shot ? a.w : 1280, shot ? a.h : 720)) {
        fprintf(stderr, "Falha ao iniciar o launcher: %s\n", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Eternal Champions", "Falha ao iniciar o launcher (SDL).", nullptr);
        return 1;
    }
    int rc = shot ? run_shot(app, a) : run_interactive(app, a);
    app.shutdown();
    if (!temp_ini.empty()) {   // copia de teste: nada fica para tras
        std::error_code ec;
        std::filesystem::remove(temp_ini, ec);
        std::filesystem::remove(temp_ini.string() + ".bak", ec);
    }
    return rc;
}
