To add frame generation to RetroArch (assuming "retrocade" refers to RetroArch/retro arcade emulation on your Windows 11 RTX 5060 Ti setup or MacBook Pro M5), you should use an external post-processing frame interpolator rather than native emulator settings, as classic 2D consoles run on deterministic 50/60Hz logic.

1. Lossless Scaling (Windows 11  Recommended)
Since you have an RTX 5060 Ti, Lossless Scaling (available on Steam) is the cleanest way to push 60 FPS arcade and 16-bit games to 120 FPS or 240 FPS using LSFG 3 (Lossless Scaling Frame Generation).

RetroArch Video Setup:

Go to Settings > Video > Output and set Video to vulkan or d3d11 (avoid exclusive fullscreen; use Windowed Fullscreen).

Disable V-Sync inside RetroArch if you are capping frames externally, or leave RetroArch locked at its native 60 Hz refresh while your monitor runs at 120Hz+.

Lossless Scaling Setup:

Frame Generation: Set to LSFG 3.0 (or latest 3.x), Mode Fixed with a 2x multiplier (turns 60 FPS into 120 FPS).

Scaling Type: Set to Off if you are already running RetroArch shaders (like scalefx or crt-guest-advanced-hd), or use LS1 / FSR if you want Lossless Scaling to handle upscaling too.

Capture API: Set to WGC (Windows Graphics Capture) for lowest latency on Windows 11.

Sync Mode: Set to Default or Allow Tearing (with G-Sync/FreeSync enabled in the NVIDIA Control Panel) to minimize input lag.

2. Magpie + Frame Generation / Shader Chaining (Windows 11)
If you already use Magpie for real-time neural upscaling (CuNNy / Anime4K), note that Magpie excels at spatial upscaling, whereas Lossless Scaling handles temporal frame interpolation better. You can run RetroArch in windowed mode at native resolution (1x), let Magpie or RetroArch shaders handle the spatial upscale, and layer LSFG on topthough stacking shaders inside RetroArch + Lossless Scaling (Scaling: Off, FG: 2x) gives the lowest overhead.

3. Caveats for Retro & Arcade Games
Sprite Flicker & HUD Artifacts: Old arcade and Genesis/SNES games simulate transparency by flickering sprites at 30 Hz (every other frame). Frame generation interpolates between "on" and "off" states, which can make flickering shadows or invincibility frames look smudged or ghostly.

Input Latency: Frame generation adds ~1 frame of visual latency (~816 ms). For tight action games or fighters (like Ultimate Mortal Kombat 3 or Super Street Fighter), pair LSFG with RetroArch's Run-Ahead (Settings > Latency > Run-Ahead to Reduce Latency = 1 or 2 frames) to offset the added interpolation delay.

BFI (Black Frame Insertion) Alternative: If your goal is CRT-like motion clarity rather than soap-opera smoothness, Black Frame Insertion (Settings > Video > Synchronization > Black Frame Insertion) on a 120Hz+ display eliminates sample-and-hold motion blur with zero interpolation artifacts.

Want me to dial in the exact RetroArch Run-Ahead and shader settings to pair with Lossless Scaling?
