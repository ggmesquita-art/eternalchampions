// Widgets do tema: lista de menu com cursor yin-yang, linha de opcao e barra de ajuda.
#pragma once
#include "app.h"
#include <string>
#include <vector>

struct MenuItem {
    std::string label;
    bool enabled = true;
    int id = 0;            // identificador livre para a tela
};

class MenuList {
public:
    std::vector<MenuItem> items;
    int focus = 0;
    // geometria: itens ancorados em `x` (Center: ponto central; Left: borda esquerda do texto)
    float x = CANVAS_W / 2.f, y = 200, row_h = 34, width = 460;
    Align align = Align::Center;
    int visible_rows = 99;       // com mais itens que isso, rola
    float text_scale = 1.f;
    int pixel_scale = 4;         // escala inteira da fonte pixel do jogo (quando ha arte extraida)
    bool yinyang_cursor = true;

    void set_focus(int i);                       // pula itens desabilitados
    // Retorna indice ativado (Enter/A/clique) ou -1.
    int update(App& app, const InputState& in);
    void draw(App& app);
    float item_y(int i) const { return y + (i - scroll_) * row_h; }

private:
    int scroll_ = 0;
    void move(int dir);
};

// Linha "rotulo ........ ◄ valor ►" (para as telas de opcoes da L3).
void draw_option_row(App& app, const Rect& r, const std::string& label, const std::string& value,
                     bool focused, bool enabled = true, bool modified = false);

// Barra de ajuda inferior (texto do widget focado / atalhos).
void draw_help_bar(App& app, const std::string& text);
// Atalhos padrao para teclado ou controle, conforme o ultimo dispositivo usado.
std::string default_hints(App& app, const InputState& in, bool tabs = false);

// Cabecalho dourado ("MAIN MENU", ...), com yin-yang opcional ao lado.
void draw_header(App& app, const std::string& title, float cx, float y, float scale = 1.f);

// Quebra de linha por palavras (largura em px do canvas, escala 1).
std::vector<std::string> wrap_text(const Font& f, const std::string& text, float width);
