# Modo C — Planos HD: Flux × AnimeSharp (decisão G3.2)

Este documento registra por que o Modo C passou a usar, por padrão, os planos regerados com **pre-dither + AnimeSharp**, e não os planos feitos com **Flux**. Também explica como voltar ao Flux.

O trabalho com Flux não foi descartado: os 27 planos continuam no disco e podem ser carregados a qualquer momento.

## 1. Os dois conjuntos de planos

| | Flux (original, M7) | AnimeSharp (G3.2, padrão atual) |
|---|---|---|
| Como foi gerado | ComfyUI, workflow `remaster_flux_usdu` (`pipeline/workflows/remaster_flux_usdu.json`), `flux1-dev-fp8`, seed `19931211`, upscale 4× via Ultimate SD Upscale | `tools/pipeline/regen_planes.py`: pre-dither `both` → `4x-AnimeSharp` (spandrel) → alpha 4× NEAREST |
| Arquivos | `assets/hd/planes/<hash>_4x.png` | `assets/hd/planes_v3/<hash>_4x.png` |
| Manifest | `pipeline/manifest_flux.json` | `pipeline/manifest.json` (o `hd_file_flux` de cada plano guarda o caminho Flux) |
| Custo | horas de GPU + curadoria manual | ~43 s para os 27 planos |
| Fidelidade | reinterpreta a arte | fiel ao pixel art original |

Os sprites HD (410 assets Flux) são os mesmos nos dois manifests. Só os 27 planos mudam.

## 2. Por que não o Flux

As comparações foram feitas no jogo, com `--hd-mode planes` sobre o B+ v3 fast e o de-dither do shader já ativo. As imagens estão em `docs/img/modo_c/`. As colunas são: B+ v3 sozinho · planos Flux · planos AnimeSharp.

1. **O Flux inventa conteúdo.** É um modelo generativo e redesenha o cenário:
   - na arena do Larcen (S27), o pôster "NOW PLAYING" vira o texto sem sentido "MEAGH TING BUICERTS" e o casal desenhado vira outra figura;
   - na arena do Trident (S24), a cidade sob a cúpula é redesenhada, com outros prédios e arcos.

   Para uma recompilação que preserva o jogo original, isso troca a arte do autor por uma alucinação.
2. **O estilo não combina com o resto da tela.** Os sprites, o HUD e os tiles sem HD saem do B+ v3, que é destilado do AnimeSharp. Com planos Flux, a tela mistura dois estilos. Com planos AnimeSharp, cenário e personagens ficam coerentes.
3. **O "grão" atribuído ao Flux não vinha do Flux.** Ele foi corrigido no compositor e a correção vale para os dois conjuntos.
   - A causa era a pseudo-transparência em xadrez do Mega Drive: plano A opaco/transparente sobre o B, ou B sobre o backdrop. Ela fazia o vencedor VDP alternar a cada pixel nativo.
   - Detalhes na §4. Isso também eliminou o motivo original do G3.2, que era o grão do céu do Trident (`86cb2ec7b0cbbdd2`).
4. **Custo de manutenção.** Regerar os planos com AnimeSharp leva segundos e não exige curadoria. Com Flux, cada ajuste exige reprocessar e revisar à mão.

**O que o Flux tem de melhor:** um aspecto de "remaster" mais pintado e com mais detalhe inventado. Quem preferir esse visual pode usar o manifest Flux (§3).

## 3. Como usar cada conjunto

```powershell
# Padrão (AnimeSharp):
.\build\EternalChampionsRecomp.exe roms\eternal_champions_us.gen --hd-manifest pipeline\manifest.json --hd-mode planes --bplus-model assets\models\ec_bplus_v3_fast_weights.bin --bplus-dedither both

# Planos Flux originais:
.\build\EternalChampionsRecomp.exe roms\eternal_champions_us.gen --hd-manifest pipeline\manifest_flux.json --hd-mode planes --bplus-model assets\models\ec_bplus_v3_fast_weights.bin --bplus-dedither both
```

- Para tornar o Flux o padrão de novo, rode `tools\pipeline\regen_planes.py --restore-flux`. Ele grava `hd_file ← hd_file_flux` no próprio `pipeline/manifest.json`.
- Para regerar os planos AnimeSharp, rode `tools\pipeline\regen_planes.py`. Ele é idempotente e preserva o `hd_file_flux`.
- `--hd-mode`:
  - `off` desliga o Modo C;
  - `planes` usa só os planos/céus HD, e os sprites vêm do B+;
  - `all` usa planos + sprites HD.

## 4. Correções no compositor (`game/ec_hd.c`) feitas junto

- **`--hd-mode planes` apagava os lutadores.** O `s_sat_match_slot[]` ficava zerado, então todo sprite parecia "casado" e o plano HD era desenhado por cima dele. Agora o array é zerado para -1.
- **De-dither de pseudo-transparência no shader de planos.** Se ≥3 dos 4 vizinhos nativos são de outra camada (padrão xadrez):
  - quando o vizinho é do outro plano HD, mistura 50/50 com ele, amostrado no texel real do vizinho, que é opaco;
  - contra backdrop ou tile sem HD, mantém a base B+, que já vem de-ditherizada.
- **Diagnóstico:** `EC_HD_CAPTURE=<png>` + `EC_HD_CAPTURE_AT=<n>` + `EC_HD_CAPTURE_MAP=<png>` gravam o frame e o mapa de camadas por pixel nativo.

## 5. Preservação dos arquivos

`assets/` está no `.gitignore`, então os PNGs (`assets/hd/planes/` Flux e `assets/hd/planes_v3/`) **não estão no git**. Os dois manifests e o workflow Flux estão versionados. Para não perder o trabalho de renderização do Flux, mantenha um backup de `assets/hd/` fora do repositório.
