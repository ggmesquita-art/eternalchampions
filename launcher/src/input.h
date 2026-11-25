// Entrada unificada: teclado, SDL_GameController (hotplug) e mouse -> acoes.
#pragma once
#include "render.h"
#include <vector>

enum Action { A_UP, A_DOWN, A_LEFT, A_RIGHT, A_ACCEPT, A_BACK, A_TABPREV, A_TABNEXT, A_HELP, A_RESET, A_START, A_COUNT };

struct InputState {
    bool pressed[A_COUNT] = {};   // disparo neste frame (inclui repeticao ao segurar p/ direcionais e abas)
    bool held[A_COUNT] = {};
    // mouse (coordenadas do canvas)
    float mouse_x = -1, mouse_y = -1;
    bool mouse_inside = false;
    bool mouse_moved = false;     // moveu neste frame (hover so troca foco quando move)
    bool mouse_click = false;     // botao esquerdo solto/pressionado neste frame
    bool mouse_right_click = false;
    int wheel = 0;                // +1 = para cima
    bool quit = false;
    bool any_input = false;       // qualquer tecla/botao/clique neste frame
    bool using_pad = false;       // ultimo dispositivo usado foi controle
    bool p(Action a) const { return pressed[a]; }
};

class Input {
public:
    void init();
    void shutdown();
    // Chamar uma vez por frame: processa eventos pendentes e calcula `state`. dt em segundos.
    void poll(Render& r, float dt);
    const InputState& state() const { return st_; }
    int pad_count() const { return (int)pads_.size(); }
    const char* pad_name(int i) const;
    void inject(Action a) { inject_[a] = true; }   // uso interno/teste

private:
    void open_pad(int joy_index);
    void close_pad(SDL_JoystickID id);
    std::vector<SDL_GameController*> pads_;
    InputState st_;
    bool inject_[A_COUNT] = {};
    float hold_t_[A_COUNT] = {};
    bool prev_held_[A_COUNT] = {};
    bool last_pad_ = false;
};
