// Dialogos nativos de arquivo/pasta. Retornam caminho UTF-8 ou "" se cancelado/indisponivel.
//   Windows: comdlg32/shell32.  Linux: zenity (ou kdialog).  macOS: osascript.
#pragma once
#include "app.h"
#include <string>
#include <vector>

struct FileFilter {
    std::string label;      // texto exibido, ex.: "ROMs Mega Drive (*.gen;*.md;*.bin;*.smd)"
    std::string patterns;   // padroes separados por ';', ex.: "*.gen;*.md;*.bin;*.smd" ("*.*" = tudo)
};

std::string pick_open_file(App& app, const std::string& title, const std::vector<FileFilter>& filters, const std::filesystem::path& initial_dir);
std::string pick_save_file(App& app, const std::string& title, const std::vector<FileFilter>& filters, const std::string& default_ext);
std::string pick_folder(App& app, const std::string& title);
