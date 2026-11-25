// dumps.h - gera os dumps GENESIS_AUDIO_APPLIEDDUMP das 17 musicas sem Python
// (porte de tools/audio/extract_all_midis.py) e utilitarios para rodar o jogo headless.
#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ecr {

// fraction em [0,1] (ou < 0 se indeterminado) e texto da etapa.
using ProgressFn = std::function<void(float fraction, const std::string& stage)>;
// Retorna true para cancelar (o processo do jogo e encerrado).
using CancelFn = std::function<bool()>;

struct GameRun {
    std::string exe;            // EternalChampionsRecomp.exe
    std::string rom;            // ROM (.gen)
    std::string work_dir;       // cwd do jogo (raiz do repo/instalacao: assets/ sao relativos a ela)
    int frames_per_track = 2400;  // ~40 s por musica
};

// Cache: <dir>/bgm_00.appdump.txt .. bgm_16.appdump.txt
std::string dump_path(const std::string& cache_dir, int bgm_id);
bool dumps_complete(const std::string& cache_dir);

// Roda o Music Test (bgm 0..15) e a selecao de personagem (16) e grava os 17 dumps em cache_dir.
// Os dumps sao derivados da ROM: o cache fica fora do git.
bool extract_dumps(const GameRun& run, const std::string& cache_dir, ProgressFn progress,
                   CancelFn cancel, std::string* err);

// --- blocos reutilizaveis (original_capture) ---
struct MusicWindow {
    int bgm_id, f_start, f_end;
};
// Script de entradas ate Options > Music Test e captura de 0..15 em sequencia.
std::vector<MusicWindow> build_music_test_script(const std::string& script_path, const std::string& ram_path,
                                                 int track_frames);
// Script da selecao de personagem (bgm 16).
void build_char_select_script(const std::string& script_path, const std::string& ram_path, int track_frames,
                              int* f_start, int* f_end);
// Script que toca SO a musica `bgm_id` (0..15) no Music Test; devolve a janela.
bool build_single_music_script(const std::string& script_path, int bgm_id, int track_frames, int* f_start,
                               int* f_end);

struct ProcessSpec {
    std::string exe;
    std::vector<std::string> args;
    std::string cwd;
    std::vector<std::pair<std::string, std::string>> env;  // somadas ao ambiente atual
    std::string log_path;                                   // stdout+stderr (opcional)
};
// Argumento de linha de comando do Windows (regras do CommandLineToArgvW), em UTF-8.
std::string quote_arg(const std::string& utf8);
#ifdef _WIN32
// CreateProcessW herdando SO `log_handle` (stdout/stderr) + um NUL como stdin, via PROC_THREAD_ATTRIBUTE_HANDLE_LIST
// (STARTUPINFOEX). `log_handle` = HANDLE inheritavel ou nullptr/INVALID_HANDLE_VALUE (nada herdado).
// `pi` = PROCESS_INFORMATION*; `cmdline` e modificavel (exigencia da API). Retorna false e GetLastError() em `err`.
bool spawn_process_w(const wchar_t* app, wchar_t* cmdline, const wchar_t* cwd, void* env_block, unsigned long flags,
                     void* log_handle, void* pi, unsigned long* err);
#endif
// Executa sem janela e espera. `poll` (opcional) roda a cada ~200 ms e devolve true para cancelar.
// Retorna o exit code; -1 se falhou ao iniciar; -2 se cancelado.
int run_process(const ProcessSpec& spec, const std::function<bool()>& poll, std::string* err);

// Ultimo "f=N" no fim de um arquivo de texto em crescimento (-1 se nao achou).
long long last_frame_in_file(const std::string& path);

}  // namespace ecr
