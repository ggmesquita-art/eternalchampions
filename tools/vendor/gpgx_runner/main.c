// tools/vendor/gpgx_runner/main.c — loader libretro mínimo para o Genesis Plus GX
// Roda N quadros e despeja a RAM de trabalho (referência do harness, DESIGN §8).
//
// Compilar: gcc -O2 -o gpgx_runner main.c -ldl -lm
// Uso: gpgx_runner <core.so> <rom> [frames] [dump.bin] [start@frame]

#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void crash(int sig, siginfo_t *si, void *ctx) {
  void *bt[32];
  int n = backtrace(bt, 32);
  fprintf(stderr, "CRASH (sig %d) em %p\n", sig, si->si_addr);
  Dl_info info;
  if(dladdr(si->si_addr, &info)) {
    fprintf(stderr, "  símbolo: %s base %p offset %p\n",
            info.dli_sname ? info.dli_sname : "?",
            info.dli_fbase,
            (void*)((char*)si->si_addr - (char*)info.dli_fbase));
  }
  backtrace_symbols_fd(bt, n, 2);
  _exit(1);
}

#define RETRO_MEMORY_SYSTEM_RAM 2
#define RETRO_DEVICE_ID_JOYPAD_START 7
#define RETRO_DEVICE_JOYPAD 1
#define RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY 9
#define RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY 31
#define RETRO_ENVIRONMENT_GET_VARIABLE 15
#define RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE 17
#define RETRO_ENVIRONMENT_SET_VARIABLES 43
#define RETRO_ENVIRONMENT_GET_LOG_INTERFACE 27

typedef struct {
  const char *key;
  const char *value;
} retro_variable_;

static int16_t g_start = 0;

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
  (void)device; (void)index;
  if(port == 0 && id == RETRO_DEVICE_ID_JOYPAD_START) return g_start;
  return 0;
}

static const char *var_value(const char *key) {
  if(strstr(key, "system_hw")) return "auto";
  if(strstr(key, "region_detect")) return "auto";
  if(strstr(key, "add_on")) return "auto";
  if(strstr(key, "overclock")) return "100";
  if(strstr(key, "lock_on")) return "disabled";
  if(strstr(key, "overscan")) return "disabled";
  if(strstr(key, "blargg_ntsc_filter")) return "disabled";
  if(strstr(key, "lcd_filter")) return "disabled";
  if(strstr(key, "gun_cursor")) return "disabled";
  if(strstr(key, "force_dtack")) return "enabled";
  if(strstr(key, "addr_error")) return "enabled";
  if(strstr(key, "bios")) return "disabled";
  if(strstr(key, "enhanced_vscroll")) return "enabled";
  if(strstr(key, "enhanced_vscroll_limit")) return "enabled";
  return "";
}

static int environ_cb(unsigned cmd, void *data) {
  switch(cmd) {
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *(const char**)data = "/tmp/opencode";
      return 1;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
      retro_variable_ *var = (retro_variable_*)data;
      var->value = var_value(var->key);
      return 1;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      *(unsigned char*)data = 0;  // bool: 1 byte (GPGX passa bool*)
      return 1;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
      return 1;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      return 0;
    default:
      return 0;
  }
}

static void (*retro_init_fn)(void);
static void (*retro_deinit_fn)(void);
static void (*retro_run_fn)(void);
static int (*retro_load_game_fn)(void *);
static void (*retro_unload_game_fn)(void);
static size_t (*retro_get_memory_size_fn)(unsigned);
static void *(*retro_get_memory_data_fn)(unsigned);
static void (*retro_set_environment_fn)(void *);
static void (*retro_set_input_state_fn)(void *);
static void (*retro_set_input_poll_fn)(void *);
static void (*retro_set_video_refresh_fn)(void *);
static void (*retro_set_audio_sample_fn)(void *);
static void (*retro_set_audio_sample_batch_fn)(void *);

static void input_poll(void) { /* nada */ }

static void video_cb(const void *data, unsigned width, unsigned height, size_t pitch) {
  (void)data; (void)width; (void)height; (void)pitch;
}
static void audio_cb(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch_cb(const int16_t *data, size_t frames) { (void)data; return frames; }

int main(int argc, char **argv) {
  struct sigaction sa = {0};
  sa.sa_sigaction = crash;
  sa.sa_flags = SA_SIGINFO;
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGABRT, &sa, NULL);
  sigaction(SIGFPE, &sa, NULL);
  if(argc < 3) { fprintf(stderr, "uso: %s <core.so> <rom> [frames] [dump.bin] [start@frame]\n", argv[0]); return 1; }
  void *h = dlopen(argv[1], RTLD_NOW);
  if(!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
  #define SYM(name) do { *(void**)&name##_fn = dlsym(h, #name); if(!name##_fn) { fprintf(stderr, "símbolo ausente: %s\n", #name); return 1; } } while(0)
  SYM(retro_init); SYM(retro_deinit); SYM(retro_run); SYM(retro_load_game);
  SYM(retro_unload_game); SYM(retro_get_memory_size); SYM(retro_get_memory_data);
  SYM(retro_set_environment); SYM(retro_set_input_state);
  #undef SYM
  *(void**)&retro_set_input_poll_fn = dlsym(h, "retro_set_input_poll");
  *(void**)&retro_set_video_refresh_fn = dlsym(h, "retro_set_video_refresh");
  *(void**)&retro_set_audio_sample_fn = dlsym(h, "retro_set_audio_sample");
  *(void**)&retro_set_audio_sample_batch_fn = dlsym(h, "retro_set_audio_sample_batch");

  retro_set_input_state_fn((void*)&input_state);
  if(retro_set_input_poll_fn) retro_set_input_poll_fn((void*)&input_poll);
  retro_set_video_refresh_fn((void*)&video_cb);
  if(retro_set_audio_sample_fn) retro_set_audio_sample_fn((void*)&audio_cb);
  if(retro_set_audio_sample_batch_fn) retro_set_audio_sample_batch_fn((void*)&audio_batch_cb);
  retro_set_environment_fn((void*)&environ_cb);

  retro_init_fn();
  fprintf(stderr, "init ok\n");
  FILE *rom = fopen(argv[2], "rb");
  if(!rom) { fprintf(stderr, "rom não encontrada\n"); return 1; }
  fseek(rom, 0, SEEK_END);
  long romsize = ftell(rom);
  fseek(rom, 0, SEEK_SET);
  void *romdata = malloc(romsize);
  fread(romdata, 1, romsize, rom);
  fclose(rom);

  struct { char *path; void *data; size_t size; } game = { (char*)argv[2], romdata, (size_t)romsize };
  if(!retro_load_game_fn(&game)) { fprintf(stderr, "falha ao carregar o jogo\n"); return 1; }
  fprintf(stderr, "load ok\n");

  long frames = argc > 3 ? atol(argv[3]) : 60;
  int trace = 0;
  for(int a = 5; a < argc; a++) if(strcmp(argv[a], "--trace") == 0) trace = 1;
  long startAt[8] = {-1,-1,-1,-1,-1,-1,-1,-1};
  int nstarts = 0;
  long dumpAt[8] = {-1,-1,-1,-1,-1,-1,-1,-1};
  int ndumps = 0;
  char dumpPath[8][512];
  for(int a = 5; a < argc && nstarts < 8 && ndumps < 8; a++) {
    if(strncmp(argv[a], "start@", 6) == 0) startAt[nstarts++] = atol(argv[a] + 6);
    else if(strncmp(argv[a], "dump@", 5) == 0) {
      char *comma = strchr(argv[a], ',');
      dumpAt[ndumps] = atol(argv[a] + 5);
      if(comma && ndumps < 8) { snprintf(dumpPath[ndumps], 512, "%s", comma + 1); }
      else snprintf(dumpPath[ndumps], 512, "dump_%ld.bin", dumpAt[ndumps]);
      ndumps++;
    }
  }

  uint8_t *ramlive = NULL;
  for(long i = 0; i < frames; i++) {
    g_start = 0;
    for(int s = 0; s < nstarts; s++)
      if(i >= startAt[s] && i < startAt[s] + 20) g_start = 1;
    retro_run_fn();
    if(trace) {
      if(!ramlive) ramlive = (uint8_t*)retro_get_memory_data_fn(RETRO_MEMORY_SYSTEM_RAM);
      if(ramlive) {
        unsigned st = ramlive[0xFD65] | (ramlive[0xFD64] << 8);
        printf("%ld st=%04x\n", i, st);
      }
    }
    for(int d = 0; d < ndumps; d++) {
      if(i == dumpAt[d]) {
        if(!ramlive) ramlive = (uint8_t*)retro_get_memory_data_fn(RETRO_MEMORY_SYSTEM_RAM);
        if(ramlive) {
          FILE *f = fopen(dumpPath[d], "wb");
          fwrite(ramlive, 1, 0x10000, f);
          fclose(f);
          fprintf(stderr, "dump@%ld salvo em %s\n", i, dumpPath[d]);
        }
      }
    }
  }

  size_t ramsize = retro_get_memory_size_fn(RETRO_MEMORY_SYSTEM_RAM);
  void *ram = retro_get_memory_data_fn(RETRO_MEMORY_SYSTEM_RAM);
  printf("RAM: %zu bytes em %p\n", ramsize, ram);
  if(argc > 4 && ram && ramsize >= 0x10000) {
    FILE *f = fopen(argv[4], "wb");
    fwrite(ram, 1, 0x10000, f);
    fclose(f);
    printf("dump salvo em %s\n", argv[4]);
  }
  retro_unload_game_fn();
  retro_deinit_fn();
  dlclose(h);
  return 0;
}
