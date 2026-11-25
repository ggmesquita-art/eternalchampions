// L6 - pre-visualizacao do aprimoramento (B+ / shader / Modo C): roda o jogo UMA vez, curto e sem travar a UI,
// ate a tela de abertura e captura:
//   - a imagem nativa (comando SCREENSHOT do input-script, 320x224) = "Original"
//   - a imagem final com as opcoes atuais do settings.ini (EC_SHADER_CAPTURE se ha shader; senao EC_BPLUS_CAPTURE)
// O resultado fica em cache (%APPDATA%\EternalChampionsRecomp\preview_cache) por hash das opcoes relevantes.
#pragma once
#include "render.h"
#include <atomic>
#include <memory>
#include <string>
#include <thread>

class App;

class PreviewJob {
public:
    enum class State { Idle, Running, Done, Failed };
    ~PreviewJob();

    State state() const { return state_; }
    const std::string& message() const { return msg_; }
    float elapsed() const { return elapsed_; }
    std::string current_key(App& app) const;         // hash das opcoes (ini) que afetam a imagem
    bool cached(App& app) const;                     // ha resultado em cache para as opcoes atuais
    void start(App& app);                            // inicia (ou usa o cache); nao bloqueia
    void cancel() { cancel_ = true; }
    void poll(App& app, float dt);                   // thread principal: carrega as texturas ao terminar
    SDL_Texture* original() const { return orig_; }
    SDL_Texture* enhanced() const { return enh_; }
    int enh_w() const { return enh_w_; }
    int enh_h() const { return enh_h_; }
    const std::string& enh_kind() const { return enh_kind_; }   // "shader+B+" | "B+" | "nativa"
    const std::string& shown_key() const { return shown_key_; }
    void free_textures();

private:
    void load(App& app, const std::string& key);
    State state_ = State::Idle;
    std::string msg_;
    float elapsed_ = 0;
    std::thread th_;
    std::atomic<bool> done_{false}, cancel_{false}, timed_out_{false};
    std::atomic<int> rc_{0};
    std::string pending_key_, shown_key_, enh_kind_;
    SDL_Texture* orig_ = nullptr;
    SDL_Texture* enh_ = nullptr;
    int enh_w_ = 0, enh_h_ = 0;
};
