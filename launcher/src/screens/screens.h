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

#include <functional>

// Dialogo de confirmacao tematico (modal, transparente). Foco inicial em "Cancelar".
std::unique_ptr<Screen> make_confirm(const std::string& title, const std::string& message, const std::string& confirm_label,
                                     std::function<void(App&)> on_confirm);

// Telas de opcoes (L3)
std::unique_ptr<Screen> make_video_screen();
std::unique_ptr<Screen> make_enhance_screen();
std::unique_ptr<Screen> make_audio_screen();
std::unique_ptr<Screen> make_game_screen();
std::unique_ptr<Screen> make_advanced_screen();
std::unique_ptr<Screen> make_saves_screen();
std::unique_ptr<Screen> make_controls_screen();
std::unique_ptr<Screen> make_studio_screen();
void apply_controls_preset(App& app, int player, const std::string& id);   // classico|arcade|stick|teclado
