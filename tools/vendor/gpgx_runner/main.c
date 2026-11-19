// tools/vendor/gpgx_runner/main.c — loader libretro mínimo para o Genesis Plus GX
// Roda N quadros e despeja a RAM de trabalho (referência do harness, DESIGN §8).
//
// Compilar: gcc -O2 -o gpgx_runner main.c -ldl -lm
// Uso: gpgx_runner <core.so> <rom> [frames] [dump.bin] [start@frame] [dump@N,arq] [--trace]
//      gpgx_runner <core.so> <rom> <dump.bin> movie.ecm [--trace]
//
// Formato .ecm (DESIGN §8): cabeçalho com hash da ROM + 1 linha de input por
// quadro (bitmask hex de 12 botões, bits = RETRO_DEVICE_ID_JOYPAD_*):
//   rom <sha256hex>
//   frames <N>
//   <mask0>
//   <mask1>
//   ... (N linhas)
//   seed <N>        ; opcional (RNG do harness)

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
#define RETRO_DEVICE_ID_JOYPAD_B 0
#define RETRO_DEVICE_ID_JOYPAD_Y 1
#define RETRO_DEVICE_ID_JOYPAD_SELECT 2
#define RETRO_DEVICE_ID_JOYPAD_START 3
#define RETRO_DEVICE_ID_JOYPAD_UP 4
#define RETRO_DEVICE_ID_JOYPAD_DOWN 5
#define RETRO_DEVICE_ID_JOYPAD_LEFT 6
#define RETRO_DEVICE_ID_JOYPAD_RIGHT 7
#define RETRO_DEVICE_ID_JOYPAD_A 8
#define RETRO_DEVICE_ID_JOYPAD_X 9
#define RETRO_DEVICE_ID_JOYPAD_L 10
#define RETRO_DEVICE_ID_JOYPAD_R 11
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

static int16_t g_buttons = 0;

/* ---- SHA-256 compacto (verifica o hash da ROM no .ecm) -------------------- */
typedef struct { uint32_t h[8]; uint64_t len; uint8_t buf[64]; int n; } sha256_ctx;
static const uint32_t K256[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
};
static uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static void sha256_block(sha256_ctx *c, const uint8_t *p) {
  uint32_t w[64];
  int i;
  for(i = 0; i < 16; i++) w[i] = be32(p + 4*i);
  for(i = 16; i < 64; i++) {
    uint32_t s0 = rotr32(w[i-15],7) ^ rotr32(w[i-15],18) ^ (w[i-15]>>3);
    uint32_t s1 = rotr32(w[i-2],17) ^ rotr32(w[i-2],19) ^ (w[i-2]>>10);
    w[i] = w[i-16] + s0 + w[i-7] + s1;
  }
  uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3];
  uint32_t e = c->h[4], f = c->h[5], g = c->h[6], h = c->h[7];
  for(i = 0; i < 64; i++) {
    uint32_t S1 = rotr32(e,6) ^ rotr32(e,11) ^ rotr32(e,25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = h + S1 + ch + K256[i] + w[i];
    uint32_t S0 = rotr32(a,2) ^ rotr32(a,13) ^ rotr32(a,22);
    uint32_t mj = (a & b) ^ (a & cc) ^ (b & cc);
    uint32_t t2 = S0 + mj;
    h = g; g = f; f = e;
    e = d + t1;
    d = cc; cc = b; b = a;
    a = t1 + t2;
  }
  c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
  c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}
static void sha256_init(sha256_ctx *c) {
  c->h[0]=0x6a09e667; c->h[1]=0xbb67ae85; c->h[2]=0x3c6ef372; c->h[3]=0xa54ff53a;
  c->h[4]=0x510e527f; c->h[5]=0x9b05688c; c->h[6]=0x1f83d9ab; c->h[7]=0x5be0cd19;
  c->len = 0; c->n = 0;
}
static void sha256_update(sha256_ctx *c, const void *data, size_t len) {
  const uint8_t *p = (const uint8_t*)data;
  c->len += len;
  while(len > 0) {
    size_t take = (size_t)(64 - c->n);
    if(take > len) take = len;
    memcpy(c->buf + c->n, p, take);
    c->n += (int)take; p += take; len -= take;
    if(c->n == 64) { sha256_block(c, c->buf); c->n = 0; }
  }
}
static void sha256_final(sha256_ctx *c, uint8_t out[32]) {
  uint64_t bits = c->len * 8;
  static const uint8_t zero = 0;
  uint8_t one = 0x80;
  sha256_update(c, &one, 1);
  while(c->n != 56) sha256_update(c, &zero, 1);
  uint8_t lenbuf[8];
  int i;
  for(i = 0; i < 8; i++) lenbuf[i] = (uint8_t)(bits >> (56 - 8*i));
  sha256_update(c, lenbuf, 8);
  for(i = 0; i < 8; i++) {
    out[4*i]   = (uint8_t)(c->h[i] >> 24);
    out[4*i+1] = (uint8_t)(c->h[i] >> 16);
    out[4*i+2] = (uint8_t)(c->h[i] >> 8);
    out[4*i+3] = (uint8_t)c->h[i];
  }
}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
  (void)device; (void)index;
  if(port == 0 && id < 12 && (g_buttons & (1 << id))) return 1;
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

  /* --- modo .ecm (DESIGN §8): um dos argumentos termina em .ecm ------------ */
  int movieMode = 0;
  long movieFrames = 0;
  uint16_t *movieMasks = NULL;
  long movieN = 0, movieCap = 0;
  {
    const char *moviePath = NULL;
    for(int a = 3; a < argc; a++)
      if(strlen(argv[a]) > 4 && strcmp(argv[a] + strlen(argv[a]) - 4, ".ecm") == 0)
        moviePath = argv[a];
    if(moviePath) {
      movieMode = 1;
      FILE *mf = fopen(moviePath, "r");
      if(!mf) { fprintf(stderr, "filme não encontrado: %s\n", moviePath); return 1; }
      uint8_t digest[32];
      sha256_ctx sc; sha256_init(&sc);
      sha256_update(&sc, romdata, (size_t)romsize);
      sha256_final(&sc, digest);
      char romhex[65];
      for(int i = 0; i < 32; i++) sprintf(romhex + 2*i, "%02x", digest[i]);
      char line[512];
      while(fgets(line, sizeof line, mf)) {
        char *s = line;
        while(*s == ' ' || *s == '\t') s++;
        if(!*s || *s == '#' || *s == '\n') continue;
        if(strncmp(s, "rom ", 4) == 0) {
          char want[128] = "";
          sscanf(s + 4, "%127s", want);
          if(strcasecmp(want, romhex) != 0) {
            fprintf(stderr, "ecm: hash da ROM não bate\n  filme: %s\n  rom:   %s\n", want, romhex);
            fclose(mf);
            return 1;
          }
          fprintf(stderr, "ecm: hash da ROM ok (%s)\n", romhex);
        } else if(strncmp(s, "frames ", 7) == 0) {
          movieFrames = atol(s + 7);
        } else if(strncmp(s, "seed ", 5) == 0) {
          /* seed do harness: registrada no filme, não usada pelo núcleo */
        } else {
          char *endp = NULL;
          long mask = strtol(s, &endp, 16);
          if(endp == s) { fprintf(stderr, "ecm: linha inválida: %s", s); fclose(mf); return 1; }
          if(movieN == movieCap) {
            movieCap = movieCap ? movieCap * 2 : 4096;
            movieMasks = (uint16_t*)realloc(movieMasks, movieCap * sizeof(uint16_t));
          }
          movieMasks[movieN++] = (uint16_t)mask;
        }
      }
      fclose(mf);
      if(movieN < movieFrames) {
        fprintf(stderr, "ecm: faltam linhas de input (%ld de %ld)\n", movieN, movieFrames);
        return 1;
      }
      fprintf(stderr, "filme carregado: %ld quadros\n", movieFrames);
    }
  }

  /* caminho do dump final: 1º argumento que não é opção/filme */
  char finalDump[512] = "";
  for(int a = 3; a < argc && !finalDump[0]; a++) {
    if(strlen(argv[a]) > 4 && strcmp(argv[a] + strlen(argv[a]) - 4, ".ecm") == 0) continue;
    if(strncmp(argv[a], "start@", 6) == 0 || strncmp(argv[a], "dump@", 5) == 0 ||
       strcmp(argv[a], "--trace") == 0) continue;
    snprintf(finalDump, sizeof finalDump, "%s", argv[a]);
  }

  long frames = movieMode ? movieFrames : (argc > 3 ? atol(argv[3]) : 60);
  int trace = 0;
  for(int a = 5; a < argc; a++) if(strcmp(argv[a], "--trace") == 0) trace = 1;
  long startAt[8] = {-1,-1,-1,-1,-1,-1,-1,-1};
  int nstarts = 0;
  long dumpAt[16] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
  int ndumps = 0;
  char dumpPath[16][512];
  for(int a = 5; a < argc && nstarts < 8 && ndumps < 16; a++) {
    if(strncmp(argv[a], "start@", 6) == 0) startAt[nstarts++] = atol(argv[a] + 6);
    else if(strncmp(argv[a], "dump@", 5) == 0) {
      char *comma = strchr(argv[a], ',');
      dumpAt[ndumps] = atol(argv[a] + 5);
      if(comma && ndumps < 16) { snprintf(dumpPath[ndumps], 512, "%s", comma + 1); }
      else snprintf(dumpPath[ndumps], 512, "dump_%ld.bin", dumpAt[ndumps]);
      ndumps++;
    }
  }

  uint8_t *ramlive = NULL;
  for(long i = 0; i < frames; i++) {
    g_buttons = 0;
    if(movieMode) {
      if(i < movieN) g_buttons = movieMasks[i];
    } else {
      for(int s = 0; s < nstarts; s++)
        if(i >= startAt[s] && i < startAt[s] + 20) g_buttons = 1 << 3; /* START */
    }
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
  if(finalDump[0] && ram && ramsize >= 0x10000) {
    FILE *f = fopen(finalDump, "wb");
    fwrite(ram, 1, 0x10000, f);
    fclose(f);
    printf("dump salvo em %s\n", finalDump);
  }
  retro_unload_game_fn();
  retro_deinit_fn();
  dlclose(h);
  return 0;
}
