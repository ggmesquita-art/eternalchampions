// Dialogos nativos do Windows (comdlg32/shell32). Retornam caminho UTF-8 ou "" se cancelado.
#pragma once
#include "app.h"

std::string pick_open_file(App& app, const wchar_t* title, const wchar_t* filter_pairs, const std::filesystem::path& initial_dir);
std::string pick_save_file(App& app, const wchar_t* title, const wchar_t* filter_pairs, const wchar_t* default_ext);
std::string pick_folder(App& app, const wchar_t* title);
