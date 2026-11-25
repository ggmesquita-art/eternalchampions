// Tela de opcoes generica: abas (LB/RB), linhas "rotulo ... < valor >", ajuda embaixo, selos ↻ e ●.
// Cada Row e um conjunto de closures; option_row() converte uma Opt do esquema em Row.
// Salva ao mudar (Config::set grava o settings.ini de forma atomica); nao ha botao "Salvar".
#pragma once
#include "../app.h"
#include <functional>
#include <string>
#include <vector>

struct Row {
    bool header = false;                         // cabecalho de secao (nao selecionavel)
    bool info = false;                           // texto informativo (nao selecionavel)
    std::string label, help;
    std::function<std::string(App&)> value;      // texto do valor
    std::function<void(App&, int)> adjust;       // ◄/► (dir = -1/+1); null = nao ajustavel
    std::function<void(App&)> activate;          // Enter/A/clique; padrao: adjust(+1)
    std::function<void(App&)> reset;             // X (restaurar padrao)
    std::function<bool(App&)> modified;          // ● valor != padrao
    std::function<bool(App&)> enabled;           // null = sempre
    bool restart = false;                        // ↻
    bool arrows = true;                          // mostra ◄ ►
    bool rebuild = false;                        // reconstroi as paginas apos agir (conteudo dinamico)
    Color tint{255, 255, 255, 0};                // a=0: padrao
};

struct Page {
    std::string tab;
    std::vector<Row> rows;
    std::function<void(App&, const Rect&)> extra;   // area livre abaixo das linhas (ex.: pre-visualizacao)
};

using PageBuilder = std::function<std::vector<Page>(App&)>;

// Row a partir de uma opcao do esquema (tipos Bool/Int/Float/Enum/Path/Folder/SaveFile).
Row option_row(App& app, const Opt& o);
Row header_row(const std::string& text);
Row info_row(const std::string& text);
Row action_row(const std::string& label, const std::string& help, std::function<void(App&)> fn, bool rebuild = false);

std::unique_ptr<Screen> make_options_screen(const std::string& title, PageBuilder builder);

// Restaura o padrao de todas as opcoes do esquema (tab vazio = todas). Mantem skip_launcher.
void reset_options(App& app, const std::string& tab);
