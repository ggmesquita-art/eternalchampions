// os.h - utilidades de SO do launcher (Windows / Linux / macOS). Caminhos e textos em UTF-8.
#pragma once
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>

namespace os {

// Caminho do executavel do launcher (Win GetModuleFileNameW; Linux /proc/self/exe; macOS _NSGetExecutablePath+realpath).
std::filesystem::path exe_path();
// Pasta de dados do usuario do jogo: Win %APPDATA%\EternalChampionsRecomp; Linux $XDG_CONFIG_HOME|~/.config/EternalChampionsRecomp;
// macOS ~/Library/Application Support/EternalChampionsRecomp. Vazio se nao foi possivel descobrir.
std::filesystem::path user_data_dir();
// Nome do executavel do jogo ("EternalChampionsRecomp" + ".exe" so no Windows); fica ao lado do launcher.
std::string game_exe_name();
// Abre arquivo/pasta no programa padrao (ShellExecuteW / xdg-open / open, sem shell). Nao bloqueia.
void shell_open(const std::filesystem::path& p);
// fopen com caminho UTF-8.
std::FILE* fopen_utf8(const std::string& path, const char* mode);
// Move `from` sobre `to` (substitui; Windows: MoveFileEx com WRITE_THROUGH; POSIX: rename). false e *err em falha.
bool replace_file(const std::filesystem::path& from, const std::filesystem::path& to, std::string* err);
// Data/hora local da ultima gravacao do arquivo.
bool file_mtime_local(const std::filesystem::path& p, std::tm* out);
// Milissegundos desde um ponto arbitrario (monotonico).
unsigned long long tick_ms();

}  // namespace os
