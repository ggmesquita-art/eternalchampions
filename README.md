# Eternal Champions Recomp

Recompilação estática de **Eternal Champions** (Sega Genesis / Mega Drive, 1993) para PC nativo. O código 68000 do cartucho é traduzido para C no build pelo [segagenesisrecomp](https://github.com/mstan/segagenesisrecomp) e roda sobre um runner SDL2. Não há emulação de CPU no caminho normal. Por cima do jogo original o projeto acrescenta:

- **widescreen** em tempo real (16:9, 21:9 e adaptativo), com margens reconstruídas a partir da VRAM e da ROM;
- **upscale neural B+** (ONNX Runtime / OpenGL), **shaders RetroArch** (`.glslp`) e substituição de sprites e planos em HD;
- **remaster de áudio**, com síntese SoundFont em tempo real, mixagem com o chip FM/PSG original e efeitos (DSP);
- **launcher gráfico** (`EternalChampions`), com escolha de ROM, opções, controles, saves e o Estúdio de Remaster.

> A ROM **não** acompanha o repositório. Você precisa de um dump próprio do cartucho, com exatamente a versão descrita abaixo.

---

## ROM suportada

O recompilador gera o C a partir de uma imagem específica. Os endereços de código, hooks e tabelas do projeto (`game/game.toml`, `game/ec_code_addrs.txt`, `game/ec_discovery.toml`) valem só para ela. **Não há garantia de que outras versões funcionem** (Europa, Japão, protótipos, relançamentos em coletâneas, hacks, dumps com cabeçalho de copiadora ou formato `.smd` intercalado). Elas podem falhar no build, travar ou se comportar de forma errada sem aviso.

| Campo | Valor |
|---|---|
| Título (cabeçalho) | `ETERNAL CHAMPIONS` |
| Região | **EUA / América do Norte** (código de região `U`) |
| Console (cabeçalho) | `SEGA GENESIS` |
| Copyright (cabeçalho) | `(C)SEGA 1993.OCT` |
| Número de série | `GM MK-1145 -00` |
| Tamanho | **3.145.728 bytes** (3 MiB / 24 Mbit), sem cabeçalho de copiadora |
| Faixa de ROM (cabeçalho) | `$000000`–`$2FFFFF` |
| RAM (cabeçalho) | `$FF0000`–`$FFFFFF` (sem SRAM / backup RAM) |
| Suporte de I/O (cabeçalho) | `J6L` (inclui controle de 3 botões `J` e de 6 botões `6`) |
| Checksum do cabeçalho | `$3630` (confere com a soma calculada) |
| Formato | binário puro (`.gen` / `.md` / `.bin`), não intercalado |
| **MD5** | `bde8b2396241aa27463ca875e604b8d3` |
| **SHA-1** | `5e978217c10b679a42d7d2966a4ccb77e1715962` |
| **SHA-256** | `6d8bdaf8c994d9f718c8f991900c58cdf1af84e030524b57f6dda263cc4ea247` |
| **CRC32** | `48f1a42e` |

O launcher confere o SHA-256 ao selecionar a ROM e avisa quando o hash é diferente ([docs/LAUNCHER.md](docs/LAUNCHER.md)).

Coloque a ROM em **`roms/eternal_champions_us.gen`**. O build lê esse caminho exato e falha sem ele. A pasta `roms/` é ignorada pelo git.

Para conferir o hash:

```powershell
Get-FileHash -Algorithm MD5 roms\eternal_champions_us.gen
```

```bash
md5sum roms/eternal_champions_us.gen      # Linux
md5 roms/eternal_champions_us.gen         # macOS
```

---

## Plataformas

| Plataforma | Jogo (`EternalChampionsRecomp`) | Launcher (`EternalChampions`) | Como foi verificado |
|---|---|---|---|
| Windows 10/11 x64 (MSVC 2022) | ✅ | ✅ | Plataforma principal de desenvolvimento. Harness de regressão (`harness/ec_harness.py`). |
| Linux x86_64 (GCC 13, Ubuntu 24.04) | ✅ | ✅ | Build completo e execução headless de 1200 frames com hashes de framebuffer **idênticos** aos do Windows. |
| macOS (Apple Silicon / Intel, Clang) | ⚠️ não testado | ⚠️ não testado | O runner do segagenesisrecomp suporta macOS, e o código do projeto evita APIs exclusivas de Windows/Linux. Ainda não foi compilado num Mac. |

Limitações fora do Windows:

- **macOS:** o OpenGL do sistema para na versão 4.1 core (e está obsoleto). Shaders `.glslp` e o B+ por GL que exijam versões maiores podem não funcionar. O jogo base e o widescreen não dependem deles. O ONNX Runtime é opcional (carregado por `dlopen`).
- **Linux:** os diálogos de arquivo do launcher usam `zenity` ou, na falta dele, `kdialog`. Sem nenhum dos dois, o launcher avisa e os diálogos ficam indisponíveis. A ROM colocada em `roms/` continua aparecendo na lista da tela ROM.
- **Linux e macOS:** a interface do launcher usa fontes TTF do sistema (DejaVu, Liberation, Noto, Ubuntu, FreeSans, Georgia, Arial…). Para escolher outra, use `EC_LAUNCHER_FONT=/caminho/fonte.ttf`. Sem nenhuma dessas fontes o launcher não abre. Configurações, arte extraída e caches ficam em `~/.config/EternalChampionsRecomp` (Linux, respeita `XDG_CONFIG_HOME`) ou em `~/Library/Application Support/EternalChampionsRecomp` (macOS), em vez de `%APPDATA%`.
- **macOS:** se o launcher morrer por crash ou `SIGKILL`, o jogo e a extração iniciados por ele não são encerrados junto. No Windows (Job Object) e no Linux (`PR_SET_PDEATHSIG`) eles são.
- O harness de regressão e as ferramentas em `tools/` e `pipeline/` são scripts Python. Algumas dependem de WSL ou CUDA e não fazem parte do build.

---

## Dependências

Todas as plataformas precisam de:

- **CMake ≥ 3.16** e um compilador **C11 / C++17**. Ninja é recomendado (opcional no Linux e no macOS).
- **SDL2** (testado com 2.28.5 no Windows e 2.30 no Linux) e **libpng** (com zlib).
- **segagenesisrecomp** clonado em `./segagenesisrecomp/`, na raiz deste repositório. É um repositório separado, ignorado pelo git daqui e **não** um submódulo:

  ```bash
  git clone https://github.com/mstan/segagenesisrecomp.git segagenesisrecomp
  ```

  > ⚠️ **Atenção:** este projeto usa o branch **`eternalchampions`** do segagenesisrecomp. Ele tem 9 commits próprios sobre o `master` upstream (`4b2247d`). Entre eles estão: carga de savegame portátil V2, hotkey de screenshot F12, observador de escrita no chip para o remaster de áudio, combos/autofire de input, `--load-slot` e salvamento atômico do `settings.ini`. Esse branch ainda **não está publicado**, e o build com o `master` upstream não foi testado. Para reproduzir o build em outra máquina, publique o branch num fork e ajuste o `git clone` acima.

### Windows

- Visual Studio 2022 Build Tools (workload "Desenvolvimento para desktop com C++", x64).
- CMake e Ninja (p.ex. `winget install Kitware.CMake Ninja-build.Ninja`).
- SDL2 2.28.5 (pacote VC), já incluído em `segagenesisrecomp/runner/external/SDL2/SDL2-2.28.5`.
- libpng e zlib via [vcpkg](https://github.com/microsoft/vcpkg), triplet `x64-windows`: `vcpkg install libpng:x64-windows`.

### Linux (Debian / Ubuntu)

```bash
sudo apt install build-essential cmake ninja-build pkg-config libsdl2-dev libpng-dev
sudo apt install zenity          # opcional: diálogos de arquivo do launcher
```

Fedora: `sudo dnf install gcc-c++ cmake ninja-build SDL2-devel libpng-devel zenity`. Arch: `sudo pacman -S base-devel cmake ninja sdl2 libpng zenity`.

### macOS

```bash
xcode-select --install
brew install cmake ninja sdl2 libpng
```

---

## Build

O build gera os executáveis em `build/`: o jogo (`EternalChampionsRecomp`, com `.exe` no Windows) e o launcher (`EternalChampions`). A primeira configuração roda o recompilador sobre a ROM e leva cerca de 1 minuto. As seguintes são incrementais.

### Windows

```powershell
.\build.cmd
```

- `build.cmd reconfigure` reconfigura o CMake. Argumentos extras vão para o configure, p.ex. `build.cmd reconfigure -DEC_BUILD_LAUNCHER=OFF`.
- O `build.cmd` tem caminhos fixos da máquina de desenvolvimento (CMake/Ninja do winget e vcpkg em `%TEMP%\opencode\vcpkg`). Em outra máquina, ajuste as variáveis no topo do script ou rode o CMake direto, num *x64 Native Tools Command Prompt*:

  ```bat
  cmake -S game -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH=%CD%/segagenesisrecomp/runner/external/SDL2/SDL2-2.28.5 ^
    -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
  cmake --build build
  ```

- Ao terminar, o `build.cmd` **abre o jogo** com o perfil de desenvolvimento (widescreen, B+, HD, remaster de áudio). Esse perfil depende de arquivos em `assets/` que não são versionados. Para só compilar, use `cmake --build build`.

### Linux e macOS

```bash
./build.sh
```

- `./build.sh reconfigure [-Dopção=valor ...]` reconfigura o CMake.
- As variáveis `BUILD_DIR`, `BUILD_TYPE` e `JOBS` mudam a pasta, o tipo de build e o paralelismo.
- No macOS o script passa `CMAKE_PREFIX_PATH=$(brew --prefix)` automaticamente.
- Equivale a:

  ```bash
  cmake -S game -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
  cmake --build build
  ```

### Opções do CMake

| Opção | Padrão | Efeito |
|---|---|---|
| `EC_BUILD_LAUNCHER` | `ON` | Compila o launcher `EternalChampions` e a biblioteca `ec_remaster_core` (+ `remaster_core_selftest`). |
| `EC_GEN_DEV_TRACE` | `OFF` | Compila os traces de som de desenvolvimento (`chip_ring`/`snd_ring`, F12/`--snd-dump`). |

---

## Executar

Pelo launcher (recomendado):

```bash
./build/EternalChampions          # Linux / macOS
```

```powershell
.\build\EternalChampions.exe
```

Direto no jogo:

```bash
./build/EternalChampionsRecomp roms/eternal_champions_us.gen --no-launcher
```

Rode a partir da raiz do repositório (ou deixe o launcher fazer isso), porque `assets/shaders`, `assets/audio_remaster` e `pipeline/` são procurados a partir dela. As configurações ficam em `build/settings.ini`.

Documentação:

- [docs/LAUNCHER.md](docs/LAUNCHER.md): uso do launcher, onde ficam ROM, saves e configurações.
- [docs/CONFIGURATION_AND_CLI.md](docs/CONFIGURATION_AND_CLI.md): todas as flags de linha de comando, variáveis de ambiente, o `settings.ini` e as hotkeys.
- [docs/widescreen.md](docs/widescreen.md) e [docs/audio_remaster_architecture.md](docs/audio_remaster_architecture.md): detalhes dos módulos.

Teste rápido sem janela (verifica que o build roda e imprime hashes do framebuffer):

```bash
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  ./build/EternalChampionsRecomp roms/eternal_champions_us.gen --no-launcher --turbo --max-frames 1200 --hash-frames 300
```

No Windows use `SDL_VIDEODRIVER=dummy` e `SDL_RENDER_DRIVER=software`.

---

## Estrutura do repositório

| Pasta | Conteúdo |
|---|---|
| `game/` | Integração do jogo com o runner: `CMakeLists.txt` principal, `game.toml` (configuração do recompilador), vídeo/widescreen, áudio, HD, shaders. |
| `launcher/` | Launcher SDL2 em C++ (`src/`) e o núcleo do Estúdio de Remaster (`remaster/`). |
| `neural/` | Inferência do upscale B+ (`infer/`) e treino dos modelos. |
| `assets/` | Só `shaders/` e os `.toml` de `audio_remaster/` são versionados. Modelos, SoundFonts e arte HD ficam fora do git. |
| `pipeline/` | Manifests e workflows do pipeline de arte HD. |
| `harness/` | Harness de regressão determinística (`ec_harness.py`). |
| `disasm/`, `reference/`, `tools/` | Desmontagem, material de referência e ferramentas de análise. |
| `docs/` | Documentação de projeto. |
