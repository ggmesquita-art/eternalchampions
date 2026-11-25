// Visualizacao do patch FM (PLAN §10.2): diagrama do algoritmo (YM2612) e envelopes por operador.
#pragma once
#include "../app.h"
#include "voices_toml.h"

void draw_line(Render& r, float x0, float y0, float x1, float y1, float thick, Color c);
// Desenha dentro de `area`: topo = algoritmo + feedback; abaixo, 4 envelopes (AR/D1R/D2R/SL/RR) e parametros.
void draw_fm_patch(App& app, const Rect& area, const ecr::VoiceMeta& m);
