// proc.h - camada portatil de processos filhos (Windows / Linux / macOS), sem passar por shell.
//
// Windows: CreateProcessW herdando SO o handle do log (PROC_THREAD_ATTRIBUTE_HANDLE_LIST) + NUL como stdin;
//          o launcher fica num Job Object KILL_ON_JOB_CLOSE, entao todo filho morre junto com ele.
// POSIX:   fork + execve com argv (nunca shell); stdin=/dev/null, stdout/stderr -> log_path; chdir no filho.
//          Linux: prctl(PR_SET_PDEATHSIG, SIGTERM). macOS/outros: os filhos sao rastreados e mortos em
//          kill_all_children() (App::shutdown) e num atexit.
#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ecr {

struct SpawnOptions {
    std::string exe;                                        // caminho ou nome (POSIX: procurado no PATH)
    std::vector<std::string> args;                          // argv[1..], UTF-8, sem aspas (quote e feito aqui)
    std::string cwd;                                        // vazio = herda
    std::vector<std::pair<std::string, std::string>> env;   // somadas/substituindo o ambiente atual
    std::string log_path;                                   // stdout+stderr (criado/truncado); vazio = nada redirecionado
    bool no_window = false;                                 // Windows: CREATE_NO_WINDOW (sem efeito em POSIX)
    bool kill_with_launcher = true;                         // o filho morre se o launcher morrer
};
using ProcessSpec = SpawnOptions;

// Processo filho. Valor simples: quem spawnou deve terminar com try_wait/wait_for (que liberam ao sair),
// terminate ou release.
struct ChildProcess {
    void* handle = nullptr;     // Windows: HANDLE do processo (POSIX: nao usado)
    unsigned long pid = 0;
    bool valid() const { return pid != 0; }
};

bool spawn(const SpawnOptions& opts, ChildProcess& out, std::string* err);
// Nao bloqueante. true = terminou (preenche *exit_code, libera `p`, que passa a !valid()). Sinal em POSIX = 128+sinal.
bool try_wait(ChildProcess& p, int* exit_code);
// Espera ate timeout_ms. true = terminou (como try_wait).
bool wait_for(ChildProcess& p, int timeout_ms, int* exit_code);
// Mata (TerminateProcess / SIGKILL), espera ate wait_ms e libera. Seguro em !valid().
void terminate(ChildProcess& p, int wait_ms = 5000);
// Solta o processo sem esperar nem matar (POSIX: continua rastreado ate sair).
void release(ChildProcess& p);

// Ciclo de vida: Windows cria/entra no Job Object kill-on-close; POSIX registra o atexit. Chamar uma vez no inicio.
void enable_kill_children_on_exit();
// POSIX: mata e colhe todos os filhos rastreados (kill_with_launcher). Windows: nada (o job cuida).
void kill_all_children();

// Executa SO e espera (sem janela). `poll` (opcional) roda a cada ~200 ms e devolve true para cancelar.
// Retorna o exit code; -1 se falhou ao iniciar; -2 se cancelado.
int run_process(const SpawnOptions& spec, const std::function<bool()>& poll, std::string* err);

// Argumento de linha de comando do Windows (regras do CommandLineToArgvW), em UTF-8.
std::string quote_arg(const std::string& utf8);
// Procura `name` no PATH (Windows: acrescenta .exe se faltar; POSIX: arquivo executavel). "" se nao achou.
std::string find_in_path(const std::string& name);
// PID do processo atual.
unsigned long current_pid();

#ifndef _WIN32
// POSIX: segurar durante pipe()+fcntl(FD_CLOEXEC)+fork(). Sem isso, um fork concorrente (spawns em threads
// do Estudio/preview) pode herdar a ponta de escrita de um pipe antes do CLOEXEC e segurar o EOF dele.
void lock_fork();
void unlock_fork();
#endif

}  // namespace ecr
