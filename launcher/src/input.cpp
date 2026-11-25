#include "input.h"
#include <cmath>
#include <cstring>

void Input::init() {
    SDL_GameControllerEventState(SDL_ENABLE);
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) open_pad(i);
}

void Input::shutdown() {
    for (auto* p : pads_) SDL_GameControllerClose(p);
    pads_.clear();
}

void Input::open_pad(int idx) {
    SDL_GameController* c = SDL_GameControllerOpen(idx);
    if (c) pads_.push_back(c);
}

void Input::close_pad(SDL_JoystickID id) {
    for (size_t i = 0; i < pads_.size(); ++i) {
        if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads_[i])) == id) {
            SDL_GameControllerClose(pads_[i]);
            pads_.erase(pads_.begin() + i);
            return;
        }
    }
}

const char* Input::pad_name(int i) const {
    if (i < 0 || i >= (int)pads_.size()) return "";
    const char* n = SDL_GameControllerName(pads_[i]);
    return n ? n : "Controller";
}

void Input::poll(Render& r, float dt) {
    st_.mouse_moved = false;
    st_.mouse_click = false;
    st_.mouse_right_click = false;
    st_.wheel = 0;
    st_.any_input = false;
    bool click_now[A_COUNT] = {};   // acoes por evento discreto (ex.: botao do mouse nao usado aqui)
    (void)click_now;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: st_.quit = true; break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_CLOSE) st_.quit = true;
            break;
        case SDL_CONTROLLERDEVICEADDED: open_pad(e.cdevice.which); break;
        case SDL_CONTROLLERDEVICEREMOVED: close_pad(e.cdevice.which); break;
        case SDL_MOUSEMOTION: {
            st_.mouse_inside = r.window_to_canvas(e.motion.x, e.motion.y, st_.mouse_x, st_.mouse_y);
            st_.mouse_moved = true;
            last_pad_ = false;
            break;
        }
        case SDL_MOUSEBUTTONDOWN: {
            st_.mouse_inside = r.window_to_canvas(e.button.x, e.button.y, st_.mouse_x, st_.mouse_y);
            if (st_.mouse_inside) {
                if (e.button.button == SDL_BUTTON_LEFT) st_.mouse_click = true;
                if (e.button.button == SDL_BUTTON_RIGHT) st_.mouse_right_click = true;
            }
            st_.any_input = true;
            last_pad_ = false;
            break;
        }
        case SDL_MOUSEWHEEL: st_.wheel += e.wheel.y > 0 ? 1 : (e.wheel.y < 0 ? -1 : 0); break;
        case SDL_KEYDOWN:
            if (!e.key.repeat) { st_.any_input = true; last_pad_ = false; }
            break;
        case SDL_CONTROLLERBUTTONDOWN: st_.any_input = true; last_pad_ = true; break;
        default: break;
        }
    }

    // Estado mantido (polling)
    bool held[A_COUNT] = {};
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    auto K = [&](SDL_Scancode s) { return k[s] != 0; };
    held[A_UP] = K(SDL_SCANCODE_UP) || K(SDL_SCANCODE_W);
    held[A_DOWN] = K(SDL_SCANCODE_DOWN) || K(SDL_SCANCODE_S);
    held[A_LEFT] = K(SDL_SCANCODE_LEFT) || K(SDL_SCANCODE_A);
    held[A_RIGHT] = K(SDL_SCANCODE_RIGHT) || K(SDL_SCANCODE_D);
    held[A_ACCEPT] = K(SDL_SCANCODE_RETURN) || K(SDL_SCANCODE_KP_ENTER) || K(SDL_SCANCODE_SPACE) || K(SDL_SCANCODE_Z);
    held[A_BACK] = K(SDL_SCANCODE_ESCAPE) || K(SDL_SCANCODE_BACKSPACE);
    held[A_TABPREV] = K(SDL_SCANCODE_Q) || K(SDL_SCANCODE_PAGEUP);
    held[A_TABNEXT] = K(SDL_SCANCODE_E) || K(SDL_SCANCODE_PAGEDOWN);
    held[A_HELP] = K(SDL_SCANCODE_F1) || K(SDL_SCANCODE_H);
    held[A_RESET] = K(SDL_SCANCODE_X) || K(SDL_SCANCODE_DELETE);
    held[A_START] = K(SDL_SCANCODE_F5);

    for (auto* c : pads_) {
        auto B = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(c, b) != 0; };
        auto Ax = [&](SDL_GameControllerAxis a) { return SDL_GameControllerGetAxis(c, a) / 32767.f; };
        float lx = Ax(SDL_CONTROLLER_AXIS_LEFTX), ly = Ax(SDL_CONTROLLER_AXIS_LEFTY);
        held[A_UP] |= B(SDL_CONTROLLER_BUTTON_DPAD_UP) || ly < -0.55f;
        held[A_DOWN] |= B(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || ly > 0.55f;
        held[A_LEFT] |= B(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || lx < -0.55f;
        held[A_RIGHT] |= B(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || lx > 0.55f;
        held[A_ACCEPT] |= B(SDL_CONTROLLER_BUTTON_A);
        held[A_BACK] |= B(SDL_CONTROLLER_BUTTON_B);
        held[A_RESET] |= B(SDL_CONTROLLER_BUTTON_X);
        held[A_HELP] |= B(SDL_CONTROLLER_BUTTON_Y);
        held[A_TABPREV] |= B(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        held[A_TABNEXT] |= B(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
        held[A_START] |= B(SDL_CONTROLLER_BUTTON_START);
        if (B(SDL_CONTROLLER_BUTTON_A) || std::fabs(lx) > 0.55f || std::fabs(ly) > 0.55f) last_pad_ = true;
    }
    for (int a = 0; a < A_COUNT; ++a) if (inject_[a]) { held[a] = true; inject_[a] = false; }

    // Mouse: roda = cima/baixo
    for (int a = 0; a < A_COUNT; ++a) st_.pressed[a] = false;
    if (st_.wheel > 0) st_.pressed[A_UP] = true;
    if (st_.wheel < 0) st_.pressed[A_DOWN] = true;

    const float delay = 0.35f, rate = 0.07f;
    for (int a = 0; a < A_COUNT; ++a) {
        st_.held[a] = held[a];
        bool repeats = a <= A_RIGHT || a == A_TABPREV || a == A_TABNEXT;
        if (held[a] && !prev_held_[a]) { st_.pressed[a] = true; hold_t_[a] = 0; st_.any_input = true; }
        else if (held[a] && repeats) {
            float before = hold_t_[a];
            hold_t_[a] += dt;
            if (hold_t_[a] > delay) {
                float n0 = std::floor((before - delay) / rate), n1 = std::floor((hold_t_[a] - delay) / rate);
                if (before <= delay || n1 > n0) st_.pressed[a] = true;
            }
        }
        prev_held_[a] = held[a];
    }
    st_.using_pad = last_pad_;

    // ---- captura de bind: primeira entrada fisica nova (tecla / botao / eixo) ----
    st_.cap = Capture{};
    for (int sc = 1; sc < SDL_NUM_SCANCODES; ++sc) {
        unsigned char now = k[sc];
        if (capture_enabled && now && !prev_keys_[sc] && !st_.cap.kind) { st_.cap.kind = 1; st_.cap.code = sc; }
        prev_keys_[sc] = now;
    }
    for (size_t i = 0; i < pads_.size() && i < 8; ++i) {
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) {
            unsigned char now = SDL_GameControllerGetButton(pads_[i], (SDL_GameControllerButton)b) ? 1 : 0;
            if (capture_enabled && now && !prev_btn_[i][b] && !st_.cap.kind) { st_.cap.kind = 2; st_.cap.code = b; }
            prev_btn_[i][b] = now;
        }
        for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; ++a) {
            float v = SDL_GameControllerGetAxis(pads_[i], (SDL_GameControllerAxis)a) / 32767.f;
            signed char now = v > 0.6f ? 1 : (v < -0.6f ? -1 : 0);
            if (capture_enabled && now && prev_axis_[i][a] != now && !st_.cap.kind) { st_.cap.kind = 3; st_.cap.code = a; st_.cap.dir = now; }
            prev_axis_[i][a] = now;
        }
    }
}

int Input::pad_family(int i) const {
    if (i < 0 || i >= (int)pads_.size()) return 0;
    switch (SDL_GameControllerGetType(pads_[i])) {
    case SDL_CONTROLLER_TYPE_PS3: case SDL_CONTROLLER_TYPE_PS4: case SDL_CONTROLLER_TYPE_PS5: return 1;
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO: return 2;
    default: return 0;
    }
}
