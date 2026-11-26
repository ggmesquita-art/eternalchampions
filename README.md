# Eternal Champions Recomp
<img width="1237" height="863" alt="ec_20261125_021152" src="https://github.com/user-attachments/assets/4a2d387a-17c5-4c6b-ac82-4e41f0e811ff" />

Recompilação estática de **Eternal Champions** (Sega Genesis / Mega Drive, 1993) para PC nativo. O código 68000 do cartucho é traduzido para C no build pelo [segagenesisrecomp](https://github.com/mstan/segagenesisrecomp) e roda sobre um runner SDL2. Não há emulação de CPU no caminho normal. Por cima do jogo original o projeto acrescenta:

- **widescreen** em tempo real (16:9, 21:9 e adaptativo), com margens reconstruídas a partir da VRAM e da ROM;
- **upscale neural B+** (ONNX Runtime / OpenGL), **shaders RetroArch** (`.glslp`) e substituição de sprites e planos em HD;
- **remaster de áudio**, com síntese SoundFont em tempo real, mixagem com o chip FM/PSG original e efeitos (DSP);
- **launcher gráfico** (`EternalChampions`), com escolha de ROM, opções, controles, saves e o Estúdio de Remaster.
<img width="2048" height="864" alt="ec_20261125_062627" src="https://github.com/user-attachments/assets/c1124d48-6f70-4772-8b59-cbe1fdc7a1e9" />

> A ROM **não** acompanha o repositório. Você precisa de um dump próprio do cartucho, com exatamente a versão descrita abaixo.

---

## Recursos

### Fidelidade ao original

- **Recompilação estática:** o código 68000 do cartucho vira C nativo. Não há emulador de CPU interpretando instruções no caminho normal.
- **Determinismo:** a mesma entrada produz a mesma imagem. Windows e Linux geram hashes de framebuffer idênticos.
- **Harness de regressão** (`harness/ec_harness.py`): compara a memória do jogo (WRAM) entre execuções e com o Genesis Plus GX. Garante que widescreen e melhorias visuais não mudam a lógica: **0 bytes divergentes**.

### Vídeo

- **Widescreen** 16:9, 21:9, 32:9 ou adaptativo à janela, sem distorcer a imagem:
  - nas arenas, no Holo-Trainer, nas Dexterity Spheres e no Santuário, as margens são reconstruídas a partir da VRAM e da ROM, com parallax nas bordas, incluindo os overkills do Santuário e do Holo-Trainer;
  - o HUD fica no centro e a lógica do jogo continua vendo 320 px;
  - menus e telas de história usam pillarbox.
- **Upscale neural B+ 4×:**
  - redes próprias, treinadas para a arte do jogo: v3 fast, v3 HQ e v3 HQ JaNai, além das v2;
  - roda em OpenGL ou em CUDA (via ONNX Runtime);
  - tem orçamento por quadro com queda suave para bilinear, intensidade ajustável e remoção do pontilhado (de-dither) do Mega Drive.
- **Modo C (HD):** troca sprites e cenários por arte 4× pré-renderizada (AnimeSharp), reconhecendo cada elemento na tela. Cobre 437 sprites e planos aprovados. Pode valer só para os planos ou para tudo.
- **Shaders RetroArch** (`.glslp`): presets `remaster-crisp`, `remaster-hq`, `remaster-smooth`, `lite-scalefx` e `lite-superxbr`, com parâmetros ajustáveis. Combinam com o B+.
- **Exibição:** entrelaçado `tv`/`raw`, cadência `fiber`/`accurate`, FPS alvo, tela cheia (F11 / Alt+Enter) e screenshot em PNG (F12).

> **macOS:** o render OpenGL ainda **não funciona** no Mac. Por isso também ficam indisponíveis o upscale B+, o Modo C e os shaders, que dependem dele. Todo o resto funciona, inclusive o widescreen, o remaster de áudio e o launcher. O backend **Metal** nativo está em [Recursos futuros](#recursos-futuros).

### Áudio

- **Remaster em tempo real**, em três modos:
  - `dsp`: o som original do chip com tratamento de estúdio;
  - `hybrid`: troca canal a canal os instrumentos FM do YM2612 por instrumentos de SoundFont;
  - `ost`: mistura a versão SoundFont com o chip original.
- **Mapeamento por instrumento:** cada patch FM é identificado por hash (`voices.toml`) e cada música tem um perfil próprio. O SoundFont TimGM6mb vem incluído.
- **Ajustes:** reverb, volume da música e do chip, mistura chip/SoundFont e recarga ao vivo do mapeamento (`hot_reload`).
- **Estúdio de Remaster** no launcher:
  - edita o mapeamento dos 52 instrumentos FM e dos canais PSG/ruído/DAC de cada uma das 17 músicas;
  - compara A/B (original × remaster) e abre o jogo direto no Music Test para ouvir.
- **Gravação da sessão em WAV** (`--wav`).

### Jogabilidade e controles

- **Eternal Champion jogável** (`--unlock-boss`), com troca de forma durante a luta: MODE + X/Y/B/A no controle de 6 botões.
- **Até 6 jogadores configuráveis:** teclado e/ou controle, padrão de 3 ou 6 botões e zona morta.
  - Controles Xbox, PlayStation e Nintendo são reconhecidos.
  - Combos: um botão físico aciona vários botões do Mega Drive, até 8 por jogador.
  - Autofire de até 30 Hz.
  - Atalhos no próprio controle.
- **Savestates** em 9 slots com miniatura: Shift+F1–F9 salva, segurar F1–F9 carrega.
- **Gerenciador de saves** no jogo (F7) e no launcher, onde dá para exportar e importar.
- **Rebobinar** (F8), com profundidade e intervalo configuráveis.
- **Turbo** (Tab) e início direto de um save (`--load-slot`).

### Launcher

- Interface no estilo do jogo, com a arte extraída da **sua** ROM.
- Confere o SHA-256 da ROM e avisa quando a versão é diferente.
- Telas de Vídeo, Aprimoramento, Áudio, Controles (com teste ao vivo), Jogo, Saves e Avançado.
- **Perfis prontos** (Original, Remaster Leve, Remaster Máximo, Sem neural), com o custo de cada um medido em ms por quadro e prévia da imagem.
- Roda em Windows, Linux e macOS.

### Distribuição

- **Instalador Windows:** não pede administrador e permite escolher a ROM durante a instalação.
- **Linux:** `.deb` e `.tar.gz`.
- **macOS (Apple Silicon):** `.app` com as dependências embutidas (SDL2, SDL3 e libpng), sem precisar de Homebrew.
- Os pacotes já trazem os modelos B+, o SoundFont e a arte HD.
- `./build.sh` compila e gera o pacote da plataforma num passo só.

---

## Recursos futuros

- **Backend Metal nativo no macOS:** leva ao Mac o remaster gráfico (upscale B+, Modo C e shaders), que hoje depende do render OpenGL e não funciona lá.
- **Lossless Upscale:** compatibilidade com o [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) (LSFG), para geração de quadros e upscale a 120/144 Hz sem mudar a lógica do jogo, que continua a ~60 ticks/s. Está previsto um perfil "Compatível com Lossless Scaling" no launcher (janela sem borda, sem interpolação nativa somada e sem o overlay do runner) e um guia de configuração com a latência medida.

Outros itens planejados, como Direct3D 11/12 e Vulkan, geração de quadros via OptiScaler, FSR 3 e DLSS, e interpolação nativa, estão em [docs/FRAMEGEN_ROADMAP.md](docs/FRAMEGEN_ROADMAP.md).

---

## Instalação (sem compilar)

Baixe o pacote do seu sistema na página de [Releases](https://github.com/ggmesquita-art/eternalchampions/releases):

| Sistema | Arquivo | Como instalar |
|---|---|---|
| Windows 10/11 x64 | `EternalChampionsRecomp-<versão>-windows-x64-setup.exe` | Rode o instalador. Ele não pede administrador e instala em `%LOCALAPPDATA%\Programs\EternalChampionsRecomp`. Uma das telas permite escolher a ROM. |
| Ubuntu 24.04+ / Debian 13+ | `eternalchampions-recomp_<versão>_amd64.deb` | `sudo apt install ./eternalchampions-recomp_<versão>_amd64.deb`, ou abra o arquivo na loja de aplicativos. |
| Outras distros x86_64 (glibc ≥ 2.39) | `EternalChampionsRecomp-<versão>-linux-x86_64.tar.gz` | Extraia e rode `./install.sh` (opcional: `--rom caminho/da/rom.gen`). Instala só para o usuário, sem root. Precisa de SDL2 e libpng instalados pelo gerenciador de pacotes. |
| macOS Apple Silicon (arm64) | `EternalChampionsRecomp-<versão>-macos-arm64.zip` | Descompacte e arraste o **Eternal Champions Recomp.app** para Aplicativos. O app não é assinado pela Apple: na primeira vez, clique com o botão direito e escolha **Abrir**. Sem o remaster gráfico por enquanto (veja [Plataformas](#plataformas)). Num Mac Intel, compile a partir do código-fonte. |

Depois abra **Eternal Champions Recomp** no menu Iniciar ou no menu de aplicativos. A ROM não acompanha o pacote. Se ela não foi escolhida na instalação, selecione-a na tela **ROM** do launcher.

No Linux, saves e configurações ficam em `~/.local/share/eternalchampions-recomp/app`; no macOS, em `~/Library/Application Support/EternalChampionsRecomp/app`.

Os pacotes trazem o jogo, o launcher, os shaders, os perfis de áudio, os modelos do upscale B+ (`assets/models`) e o SoundFont do remaster de áudio (`assets/soundfonts`). A partir da v1.0.2 os pacotes trazem também a arte HD do Modo C (conjunto AnimeSharp: `pipeline/manifest.json`, `assets/hd/` e `assets/catalog/`). A v1.0.0 não trazia os modelos nem o SoundFont, e a v1.0.1 não trazia a arte HD; nessas versões, copie as pastas do repositório para a pasta do jogo. A extração da arte da ROM para o tema do launcher precisa de Python 3 com `numpy` e `Pillow`; sem eles o launcher usa um tema procedural.

---
<img width="2048" height="793" alt="ec_20261126_000413" src="https://github.com/user-attachments/assets/b567ff84-a8a2-4e3f-b9ee-e8970c2753ef" />

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
| macOS (Apple Silicon / Intel, Clang) | ⚠️ sem remaster gráfico | ✅ | Tudo funciona, exceto o render OpenGL e, por consequência, o B+, o Modo C e os shaders `.glslp`, que dependem dele. Um build com backend **Metal** nativo está a caminho. |

Limitações fora do Windows:

- **macOS:** o render OpenGL **ainda não funciona**. Por consequência, ficam indisponíveis o upscale B+, o Modo C (HD) e os shaders `.glslp`, que dependem dele. O restante funciona, incluindo widescreen, remaster de áudio e launcher. Um build com backend **Metal** nativo está a caminho ([Recursos futuros](#recursos-futuros)).
- **Linux:** os diálogos de arquivo do launcher usam `zenity` ou, na falta dele, `kdialog`. Sem nenhum dos dois, o launcher avisa e os diálogos ficam indisponíveis. A ROM colocada em `roms/` continua aparecendo na lista da tela ROM.
- **Linux e macOS:** a interface do launcher usa fontes TTF do sistema (DejaVu, Liberation, Noto, Ubuntu, FreeSans, Georgia, Arial…). Para escolher outra, use `EC_LAUNCHER_FONT=/caminho/fonte.ttf`. Sem nenhuma dessas fontes o launcher não abre. Configurações, arte extraída e caches ficam em `~/.config/EternalChampionsRecomp` (Linux, respeita `XDG_CONFIG_HOME`) ou em `~/Library/Application Support/EternalChampionsRecomp` (macOS), em vez de `%APPDATA%`.
- **macOS:** se o launcher morrer por crash ou `SIGKILL`, o jogo e a extração iniciados por ele não são encerrados junto. No Windows (Job Object) e no Linux (`PR_SET_PDEATHSIG`) eles são.
- O harness de regressão e as ferramentas em `tools/` e `pipeline/` são scripts Python. Algumas dependem de WSL ou CUDA e não fazem parte do build.

---

## Dependências

Todas as plataformas precisam de:

- **CMake ≥ 3.16** e um compilador **C11 / C++17**. Ninja é recomendado (opcional no Linux e no macOS).
- **SDL2** (testado com 2.28.5 no Windows e 2.30 no Linux) e **libpng** (com zlib).
- **segagenesisrecomp**, que é um submódulo git em `./segagenesisrecomp/` e tem os seus próprios submódulos. Clone **sempre com `--recursive`**:

  ```bash
  git clone --recursive https://github.com/ggmesquita-art/eternalchampions.git
  ```

  Se o repositório já foi clonado sem `--recursive`, ou se o `git pull` trouxe uma versão nova do submódulo, rode:

  ```bash
  git submodule update --init --recursive
  ```

  O submódulo aponta para o fork [ggmesquita-art/segagenesisrecomp](https://github.com/ggmesquita-art/segagenesisrecomp), branch `eternalchampions`. Esse branch tem commits próprios sobre o [upstream](https://github.com/mstan/segagenesisrecomp): carga de savegame portátil V2, screenshot no F12, observador de escrita no chip de som (usado pelo remaster de áudio), combos e autofire de input, `--load-slot` e gravação atômica do `settings.ini`. O fork também puxa os forks de `m68k-recomp-core` (com ajustes no gerador de código) e `z80-recomp-core`. **O `master` do upstream não serve**: sem esses commits faltam APIs de áudio e de input que o jogo usa. O download em ZIP do GitHub também não serve, porque não traz os submódulos.

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


### Linux e macOS

```bash
./build.sh
```

- Depois de compilar, o script **gera o pacote da plataforma** em `packaging/out/`:
  - no macOS, `EternalChampionsRecomp-<versão>-macos-<arm64|x86_64>.zip`, com o `.app` e o SDL2 e o libpng embutidos;
  - no Linux, o `.tar.gz` e, se houver `dpkg-deb` e `fakeroot`, o `.deb`.

  A versão vem do `git describe`: a tag `v1.0.1` vira `1.0.1`, e fora de uma tag sai algo como `1.0.1-3-gabc1234`. Também dá para definir com `VERSION=...`. Se o empacotamento falhar, o build continua válido e o script só avisa.
- `./build.sh --no-package` (ou `NO_PACKAGE=1`) só compila.
- `./build.sh reconfigure [-Dopção=valor ...]` reconfigura o CMake.
- As variáveis `BUILD_DIR`, `BUILD_TYPE` e `JOBS` mudam a pasta, o tipo de build e o paralelismo.
- No macOS o script passa `CMAKE_PREFIX_PATH=$(brew --prefix)` automaticamente.
- A parte de compilação equivale a:

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
| `assets/` | Versionados: `shaders/`, `models/` (pesos do upscale B+ e checkpoints de treino), `soundfonts/` (TimGM6mb, GPL-2.0), os `.toml` de `audio_remaster/` e a arte do Modo C (`hd/sprites`, `hd/planes_v3` e os originais em `catalog/`, o conjunto AnimeSharp do `pipeline/manifest.json`). Os planos Flux, as capturas, os datasets e os MIDIs derivados da ROM ficam fora do git. |
| `pipeline/` | Manifests e workflows do pipeline de arte HD. |
| `harness/` | Harness de regressão determinística (`ec_harness.py`). |
| `disasm/`, `reference/`, `tools/` | Desmontagem, material de referência e ferramentas de análise. |
| `docs/` | Documentação de projeto. |

---

## Licença

O código e os arquivos próprios deste repositório estão sob a [PolyForm Noncommercial 1.0.0](LICENSE.md), a mesma licença do [segagenesisrecomp](https://github.com/mstan/segagenesisrecomp). Uso, modificação e distribuição são livres para fins não comerciais. Componentes de terceiros mantêm as próprias licenças ([packaging/THIRD_PARTY_NOTICES.txt](packaging/THIRD_PARTY_NOTICES.txt)): por exemplo, o SoundFont TimGM6mb é GPL-2.0 e os shaders libretro trazem a licença no cabeçalho de cada arquivo.

Eternal Champions © 1993 SEGA. Este é um projeto de fã, sem vínculo com a SEGA, e **não inclui a ROM**.
