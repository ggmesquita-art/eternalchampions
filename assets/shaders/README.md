# Shaders and presets

Format: RetroArch `.glslp` + legacy libretro GLSL (`.glsl`). Presets work in RetroArch unchanged; keys prefixed `ec_` are engine extensions that RetroArch ignores.

- `libretro/` - shaders copied from libretro/glsl-shaders (see its README).
- `ec/` - project shaders (`ec-dedither.glsl`: CBOD/SGENPT de-dither, native pass, mirrors `s_dedither_cs`).
- `presets/` - curated presets.

| Preset | Passes | Neural B+ | Purpose |
|---|---|---|---|
| `remaster-crisp` | RCAS (viewport) | yes (v3 fast, runs before the chain) | clean remaster, sharpened |
| `remaster-hq` | EASU (viewport) + RCAS | yes (v3 HQ) | best quality, strong GPUs |
| `remaster-smooth` | FXAA (viewport, linear) | yes (v3 fast) | smoother than crisp, no halo |
| `lite-scalefx` | ec-dedither, ScaleFX x5, EASU, RCAS | no | weak GPUs, pixel-art smoothing |
| `lite-superxbr` | ec-dedither, super-xBR x3, jinc2 (viewport), RCAS | no | weak/medium GPUs, smooth upscale |
| `off` | none | no | no preset file; handled by the engine |

## Usage

- `--shader-preset remaster-crisp` (name of a file in `presets/`, without extension)
- `--shader-preset C:\path\any.glslp` (any RetroArch preset by path)
- `--shader-param NAME=value` (override a `#pragma parameter`, e.g. `--shader-param FSR_SHARPENING=0.1`)
- `settings.ini`: `[ec.video]` `shader_preset=remaster-crisp`
- `--shader-preset off` disables the chain.

Validate presets: `python tools/pipeline/check_shader_presets.py`.
