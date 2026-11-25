#include <cstdio>
#include "screens.h"
#include "../launch.h"
#include "../widgets.h"

namespace {
enum { ID_BACK = 1, ID_LOGS = 2, ID_LASTERR = 3 };

class GameRun : public Screen {
public:
    void on_enter(App& app) override {
        if (!start_game(app, gp_, err_)) { state_ = Failed; build_menu(app); return; }
        state_ = Running;
        app.game_running = true;
        SDL_MinimizeWindow(app.render.window);   // some o launcher enquanto o jogo roda
    }

    void update(App& app, const InputState& in, float) override {
        if (state_ == Running) {
            if (WaitForSingleObject(gp_.handle, 0) == WAIT_OBJECT_0) {
                GetExitCodeProcess(gp_.handle, &code_);
                CloseHandle(gp_.handle); gp_.handle = nullptr;
                app.game_running = false;
                SDL_RestoreWindow(app.render.window);
                SDL_RaiseWindow(app.render.window);
                state_ = Done;
                build_menu(app);
                if (app.autoplay) {
                    FILE* f = nullptr;
                    _wfopen_s(&f, (app.exe_dir / "launcher_autoplay.txt").c_str(), L"wb");
                    if (f) { fprintf(f, "exit_code=%d\n", (int)code_); fclose(f); }
                    app.quit = true;
                }
            }
            return;
        }
        int a = menu_.update(app, in);
        if (in.p(A_BACK)) a = 0;
        if (a < 0) return;
        switch (menu_.items[a].id) {
        case ID_BACK: app.pop(); break;
        case ID_LOGS: shell_open(app.logs_dir); break;
        case ID_LASTERR: shell_open(find_last_error_log(app)); break;
        }
    }

    void draw(App& app) override {
        Render& r = app.render;
        Theme& th = app.theme;
        th.draw_stone_bg(r, app.time);
        draw_header(app, state_ == Running ? "JOGANDO" : "FIM DE JOGO", CANVAS_W / 2.f, 60);
        Rect p{240, 180, 800, 150};
        th.draw_panel(r, p);
        TextStyle st = th.item_style(true, true); st.align = Align::Center;
        TextStyle hs = th.help_style(); hs.align = Align::Center;
        float cx = CANVAS_W / 2.f;
        if (state_ == Running) {
            th.draw_yinyang(r, cx, 255, 80, app.time * 90.0);
            draw_text(r, *th.fonts.help, cx, 305, "Jogo em execu\xC3\xA7\xC3\xA3o\xE2\x80\xA6", hs);
            return;
        }
        if (state_ == Failed) {
            TextStyle bad = TextStyle::solid(th.pal.bad, 2, th.pal.text_outline); bad.align = Align::Center;
            draw_text(r, *th.fonts.menu, cx, 215, "N\xC3\xA3o foi poss\xC3\xADvel iniciar o jogo", bad);
            draw_text(r, *th.fonts.help, cx, 270, err_, hs);
        } else {
            bool ok = code_ == 0;
            TextStyle ms = TextStyle::solid(ok ? th.pal.ok : th.pal.bad, 2, th.pal.text_outline); ms.align = Align::Center;
            draw_text(r, *th.fonts.menu, cx, 215, ok ? "O jogo terminou normalmente" : "O jogo terminou com erro", ms);
            draw_text(r, *th.fonts.help, cx, 270, "Código de saída: " + std::to_string((long long)(int)code_), hs);
        }
        menu_.draw(app);
        draw_help_bar(app, default_hints(app, app.input.state()));
    }

private:
    enum State { Running, Done, Failed };
    void build_menu(App& app) {
        menu_.items.clear();
        menu_.items.push_back({"Voltar ao menu", true, ID_BACK});
        bool bad = state_ == Failed || code_ != 0;
        if (bad) {
            menu_.items.push_back({"Abrir pasta de logs", true, ID_LOGS});
            menu_.items.push_back({"Ver last_error.log", !find_last_error_log(app).empty(), ID_LASTERR});
        }
        menu_.y = 370; menu_.row_h = 38;
        menu_.set_focus(0);
    }
    State state_ = Failed;
    GameProcess gp_;
    std::string err_;
    DWORD code_ = 0;
    MenuList menu_;
};
}  // namespace

std::unique_ptr<Screen> make_game_run() { return std::make_unique<GameRun>(); }
