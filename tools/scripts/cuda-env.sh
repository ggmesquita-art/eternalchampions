#!/usr/bin/env bash
# tools/scripts/cuda-env.sh — ambiente CUDA 13 para ONNX Runtime no WSL2.
#
# Motivação (docs/PROGRESS.md, N3): onnxruntime-gpu 1.32 exige CUDA 13.
# Os wheels pip (nvidia-cuda-runtime/cublas/curand/cudnn-cu13) fornecem as
# libs DENTRO do .venv; este script só exporta o LD_LIBRARY_PATH na ordem
# certa (ptxjitcompiler do driver WSL antes das libs dos wheels).
#
# NOTE: o jogo (EternalChampionsRecomp) JÁ pré-carrega essas libs sozinho
# (neural/infer/ec_bplus_ort.c) — este script é necessário apenas para
# scripts Python que usam onnxruntime com CUDAExecutionProvider.
#
# Uso (bash ou zsh):
#   source tools/scripts/cuda-env.sh
#   .venv/bin/python ...
#
# Em fish, rode: bash -c 'source tools/scripts/cuda-env.sh && exec .venv/bin/python ...'

# Localiza o diretório do script de forma portável (bash/zsh/sh). Sem set -e:
# este arquivo é feito para ser "source"ado e não pode derrubar o shell.
if [ -n "${BASH_SOURCE:-}" ]; then
    SELF="${BASH_SOURCE[0]}"
elif [ -n "${ZSH_VERSION:-}" ]; then
    # zsh: caminho do script sendo "source"ado
    eval 'SELF="${(%):-%x}"'
else
    SELF="$0"
fi

ROOT=$(CDPATH= cd -- "$(dirname -- "$SELF")/../.." && pwd) || ROOT="$(pwd)"
VENV_LIB="$ROOT/.venv/lib/python3.12/site-packages"
NVD="$VENV_LIB/nvidia"

# Diretório do driver WSL (contém libnvidia-ptxjitcompiler.so.1 da família
# do driver Windows instalado). Procura qualquer driver nvmdi presente.
DRV_DIR=""
for d in /usr/lib/wsl/drivers/nvmdi.inf_amd64_*; do
    [ -d "$d" ] && DRV_DIR="$d" && break
done

add_path() {
    case ":$LD_LIBRARY_PATH:" in
        *":$1:"*) : ;;
        *) LD_LIBRARY_PATH="$1${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ;;
    esac
}

if [ -n "$DRV_DIR" ] && [ -d "$DRV_DIR" ]; then
    add_path "$DRV_DIR"
else
    echo "[cuda-env] aviso: diretório do driver WSL não encontrado" >&2
fi
for d in "$NVD/cu13/lib" "$NVD/cudnn/lib"; do
    [ -d "$d" ] && add_path "$d"
done

export LD_LIBRARY_PATH

# Caminho opcional da lib do onnxruntime para o backend B+ (ec_bplus_ort.c
# também tenta os caminhos relativos padrão do projeto).
if [ -z "${EC_ONNXRUNTIME_LIB:-}" ]; then
    EC_ONNXRUNTIME_LIB="$VENV_LIB/onnxruntime/capi/libonnxruntime.so.1.32.0"
    export EC_ONNXRUNTIME_LIB
fi

echo "[cuda-env] ok (LD_LIBRARY_PATH e EC_ONNXRUNTIME_LIB exportados)"
