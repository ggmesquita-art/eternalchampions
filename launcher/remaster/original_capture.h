// original_capture.h - "A: ORIGINAL": o jogo headless no Music Test com --audio-remaster off --wav <tmp>,
// recorte da janela da musica e reamostragem para 48 kHz. So em RAM; o temporario e apagado.
#pragma once
#include <string>
#include <vector>

#include "dumps.h"

namespace ecr {

struct OriginalAudio {
    std::vector<float> stereo;   // intercalado L/R, -1..1
    int sample_rate = 48000;
    double duration_s() const { return sample_rate > 0 ? (double)stereo.size() / 2.0 / sample_rate : 0.0; }
};

// Captura a musica `bgm_id` (0..16). Demora cerca de 1 min (jogo headless em turbo);
// rode numa thread de fundo. progress: fracao < 0 = indeterminado.
bool capture_original(const GameRun& run, int bgm_id, OriginalAudio* out, ProgressFn progress, CancelFn cancel,
                      std::string* err);

// Reamostragem (media movel + interpolacao linear), igual a mixer_studio._resample_pcm16_stereo.
// `pcm` = int16 estereo intercalado; devolve float.
std::vector<float> resample_stereo_to(const std::vector<int16_t>& pcm, int src_rate, int dst_rate);

}  // namespace ecr
