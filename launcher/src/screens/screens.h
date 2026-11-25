// Fabricas das telas. Cada tela e uma subclasse de Screen (app.h), numa pilha gerida pelo App.
#pragma once
#include "../app.h"
#include <memory>
#include <string>

std::unique_ptr<Screen> make_splash();
std::unique_ptr<Screen> make_main_menu();
std::unique_ptr<Screen> make_rom_screen();
std::unique_ptr<Screen> make_about_screen();
std::unique_ptr<Screen> make_placeholder(const std::string& title);   // "EM CONSTRUCAO"
std::unique_ptr<Screen> make_game_run();                               // lanca o jogo e espera o termino

// JOGAR (menu, Start em qualquer tela): valida a ROM e empilha GameRun, ou avisa.
void request_play(App& app);
