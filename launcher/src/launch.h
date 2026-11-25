// JOGAR: grava skip_launcher=1, lanca EternalChampionsRecomp.exe --no-launcher "<rom>" e acompanha o processo.
#pragma once
#include "app.h"
#include <windows.h>


struct GameProcess {
    HANDLE handle = nullptr;
    DWORD pid = 0;
};

// Lanca o jogo. Em erro, devolve false e preenche `err`. cwd = raiz do projeto (se existir assets/ em ..) ou o exe_dir.
bool start_game(App& app, GameProcess& gp, std::string& err);

// Abre arquivo/pasta no shell do Windows.
void shell_open(const std::filesystem::path& p);
// Localiza o last_error.log (build/logs/last_error.log ou build/last_error.log); vazio se nao existe.
std::filesystem::path find_last_error_log(App& app);
