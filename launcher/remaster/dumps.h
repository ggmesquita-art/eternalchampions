// dumps.h - gera os dumps GENESIS_AUDIO_APPLIEDDUMP das 17 musicas sem Python
// (porte de tools/audio/extract_all_midis.py) e utilitarios para rodar o jogo headless.
#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "proc.h"

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

// ProcessSpec / run_process / quote_arg: ver proc.h (camada portatil de processos).

// Ultimo "f=N" no fim de um arquivo de texto em crescimento (-1 se nao achou).
long long last_frame_in_file(const std::string& path);

}  // namespace ecr
