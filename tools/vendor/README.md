# tools/vendor — ferramentas externas locais

Diretório fora do versionamento (`.gitignore`). Conteúdo:

- `vasm/` — vasm 1.9d (espelho GitHub `Leffmann/vasm`, site oficial fora do ar
  em 2026-11-18). `vasmm68k_mot` compilado com `make CPU=m68k SYNTAX=mot`.
- `ghidra_extract/ghidra_12.2_PUBLIC/` — Ghidra 12.2 (provider do REA).
- `jdk-25.0.5.1+2/` — Temurin JDK 25 (Ghidra 12.x exige 25+). `jdk/` é symlink.
- `ares_cli/` — driver headless do núcleo do ares (Mega Drive):
  - `cmake -S . -B build -DARES_SKIP_DEPS=ON && cmake --build build --target ares_cli`
  - uso: `ares_cli <rom> [quadros] [arquivo-de-dump-da-RAM]`
  - Roda N quadros, conta callbacks de vídeo e opcionalmente despeja a RAM
    de trabalho (64 KiB) para validação dinâmica da Fase 1.

## Patches aplicados no ares (árvore em ../../ares/)

1. `nall/nall/decode/mmi.hpp`: adicionado `#pragma once` (faltava o guard —
   quebrava TUs que incluem `md/md.hpp` + `mia/mia.hpp` juntos).
2. `hiro/resource/resource.cpp/hpp` e `mia/resource/resource.cpp/hpp`:
   gerados pelo `sourcery` (o build do wrapper não roda o comando automático
   do hiro). Regenerar com: `cd <ares>/hiro/resource && sourcery resource.bml
   resource.cpp resource.hpp` usando o `sourcery` em `ares_cli/build/sourcery-build/`.
3. `ares/md/controller/fighting-pad/fighting-pad.cpp`: `readData()` devolve
   máscara `0xFF` (o pad de 6 botões dirige os 8 bits, com bits 6-7 = 0 —
   assinatura de detecção do protocolo). O ares original usava `0x3F`,
   deixando os bits 6-7 flutuando. Testado: não muda o comportamento do
   Eternal Champions no estado 1 (a trava é outra — ver docs/main_loop.md).
