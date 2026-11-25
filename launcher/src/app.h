// Estado global do launcher: caminhos, ROM atual, pilha de telas, subsistemas.
#pragma once
#include "input.h"
#include "render.h"
#include "schema.h"
#include "theme.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#define EC_LAUNCHER_VERSION "0.1.0 (L1)"

class App;

// Contrato de tela. update() recebe o input unificado; draw() desenha no canvas 1280x720.
class Screen {
public:
    virtual ~Screen() {}
    virtual void on_enter(App&) {}
    virtual void on_resume(App&) {}     // quando a tela de cima sai da pilha
    virtual void update(App& app, const InputState& in, float dt) = 0;
    virtual void draw(App& app) = 0;
    virtual bool transparent() const { return false; }   // desenha a tela de baixo antes (modais)
};

class App {
public:
    // ---- subsistemas
    Render render;
    Theme theme;
    Input input;
    Config cfg;                          // settings.ini (preserva o que o launcher nao conhece)
    bool dev_visible = false;            // area "Desenvolvedor" (AVANCADO) visivel

    // ---- caminhos (PLAN §2): exe em build/, settings.ini e rom-EC.cfg ao lado do exe
    std::filesystem::path exe_dir;       // diretorio do EternalChampions.exe (= build/)
    std::filesystem::path root_dir;      // raiz do projeto quando existir assets/ em ".."; senao exe_dir
    std::filesystem::path game_exe;      // EternalChampionsRecomp.exe
    std::filesystem::path settings_ini;  // build/settings.ini
    std::filesystem::path rom_cfg;       // build/rom-EC.cfg (uma linha: caminho absoluto, formato do runner)
    std::filesystem::path recent_file;   // build/launcher_recent.txt
    std::filesystem::path assets_dir;    // build/ui_cache (arte extraida, manifest.json) - L5
    std::filesystem::path logs_dir;      // build/logs

    // ---- ROM atual
    std::string rom_path;                // utf8, vazio = nenhuma
    std::string rom_sha;                 // sha-256 calculado ("" se ilegivel)
    bool rom_exists = false;
    bool rom_valid = false;              // sha confere com a versao US
    std::vector<std::string> recents;
    std::vector<std::string> detected;   // ROMs achadas em <root>/roms

    // ---- loop
    float time = 0;                      // segundos desde o inicio (em shot: frame/60)
    bool quit = false;
    bool shot_mode = false;
    int shot_state = 0;                  // --state: 1 = captura de bind aberta, 2 = editor de combo (so em shot)
    std::string ini_override;            // --ini: usa outro settings.ini (testes/capturas)
    int shot_page = 0, shot_focus = 0;   // --tab / --focus (so no modo de captura)
    bool game_running = false;
    // teste automatizado: --autoplay (JOGAR ao abrir; sai ao fim do jogo gravando launcher_autoplay.txt) e
    // --game-args "..." (argumentos extras repassados ao jogo, p.ex. --max-frames 120)
    bool autoplay = false;
    std::string extra_game_args;
    std::string load_slot_arg;           // "N": proximo JOGAR passa --load-slot N (uso unico)

    bool init(bool hidden, int win_w, int win_h);
    void shutdown();

    // pilha de telas (mudancas aplicadas ao fim do frame)
    void push(std::unique_ptr<Screen> s);
    void pop();
    void replace(std::unique_ptr<Screen> s);   // troca o topo
    void reset_to(std::unique_ptr<Screen> s);  // esvazia a pilha e coloca s
    void apply_pending();
    void update(const InputState& in, float dt);
    void draw();
    bool has_screens() const { return !stack_.empty(); }

    // ROM
    void load_rom_state();                                  // le rom-EC.cfg + recentes
    bool set_rom(const std::string& utf8_path, bool write_cfg);   // calcula SHA; grava cfg se pedido
    std::string rom_display_name() const;

    // log do launcher (build/logs/launcher.log), com data/hora
    void log(const std::string& line);

    // ---- extracao da arte da ROM (tools/launcher/extract_ui_assets.py) em segundo plano
    enum class ArtState { Idle, Running, Done, Failed, NoPython, NoScript };
    ArtState art_state = ArtState::Idle;
    std::string art_progress;            // ultima linha do log do extrator
    bool art_manifest_current() const;   // manifest.json com o SHA da ROM atual e extractor_version conhecido
    void maybe_extract_art();            // dispara se a ROM e valida e a arte falta/esta velha (nada em shot)
    std::string art_status() const;      // texto para o SOBRE

    // toast
    void toast(const std::string& msg, float secs = 3.f) { toast_msg_ = msg; toast_t_ = secs; }

    // transicao (fade curto)
    float fade = 0;

private:
    struct Pending { enum Kind { Push, Pop, Replace, Reset } k; std::unique_ptr<Screen> s; };
    std::vector<std::unique_ptr<Screen>> stack_;
    std::vector<Pending> pending_;
    std::string toast_msg_; float toast_t_ = 0;
    void* art_proc_ = nullptr; float art_poll_t_ = 0;
    void poll_art(float dt);
    void scan_roms();
    void save_recents();
};

// utilitarios de caminho/texto
std::string path_to_utf8(const std::filesystem::path& p);
std::filesystem::path utf8_to_path(const std::string& s);
