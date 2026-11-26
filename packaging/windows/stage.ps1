# Monta packaging\out\windows\stage a partir de build\ (rode depois do build.cmd / cmake --build build).
# Uso: pwsh packaging\windows\stage.ps1
$ErrorActionPreference = 'Stop'
$root  = (Resolve-Path "$PSScriptRoot\..\..").Path
$build = Join-Path $root 'build'
$stage = Join-Path $root 'packaging\out\windows\stage'

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null

# Binarios + DLLs do build (SDL2/libpng/zlib vindos do SDL2 VC e do vcpkg)
foreach ($f in 'EternalChampions.exe','EternalChampionsRecomp.exe','SDL2.dll','libpng16.dll','libz.dll') {
    $p = Join-Path $build $f
    if (-not (Test-Path $p)) { throw "faltando $p (rode o build antes)" }
    Copy-Item $p $stage
}

# Runtime do Visual C++ (app-local, redistribuivel)
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
$crt = Get-ChildItem "$vs\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT" -Directory | Sort-Object FullName | Select-Object -Last 1
if (-not $crt) { throw 'Microsoft.VC143.CRT nao encontrado' }
foreach ($f in 'msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll') { Copy-Item (Join-Path $crt.FullName $f) $stage }

# Assets versionados: shaders e perfis/vozes do remaster de audio (so .toml)
Copy-Item -Recurse (Join-Path $root 'assets\shaders') (Join-Path $stage 'assets\shaders')
$ar = Join-Path $stage 'assets\audio_remaster'
New-Item -ItemType Directory -Force $ar | Out-Null
Push-Location (Join-Path $root 'assets\audio_remaster')
git -C $root ls-files -- 'assets/audio_remaster/*.toml' | ForEach-Object {
    $rel = $_.Substring('assets/audio_remaster/'.Length)
    $dst = Join-Path $ar $rel
    New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
    Copy-Item (Join-Path $root $_) $dst
}
Pop-Location

# Modelos do upscale B+ (so os de runtime: pesos .bin e .onnx; sem checkpoints .pt) e SoundFont
$md = Join-Path $stage 'assets\models'
New-Item -ItemType Directory -Force $md | Out-Null
Get-ChildItem (Join-Path $root 'assets\models') -File | Where-Object { $_.Extension -in '.bin','.onnx' } | Copy-Item -Destination $md
Copy-Item -Recurse (Join-Path $root 'assets\soundfonts') (Join-Path $stage 'assets\soundfonts')

# Extrator da arte da ROM usado pelo launcher (opcional: precisa de Python 3 + numpy + Pillow)
New-Item -ItemType Directory -Force (Join-Path $stage 'tools\launcher') | Out-Null
Copy-Item (Join-Path $root 'tools\launcher\extract_ui_assets.py') (Join-Path $stage 'tools\launcher')
New-Item -ItemType Directory -Force (Join-Path $stage 'roms') | Out-Null

# Documentacao e licencas
Copy-Item (Join-Path $root 'README.md'), (Join-Path $root 'LICENSE.md') $stage
New-Item -ItemType Directory -Force (Join-Path $stage 'docs') | Out-Null
Copy-Item (Join-Path $root 'docs\LAUNCHER.md'), (Join-Path $root 'docs\CONFIGURATION_AND_CLI.md') (Join-Path $stage 'docs')
$lic = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Force $lic | Out-Null
Copy-Item (Join-Path $root 'packaging\THIRD_PARTY_NOTICES.txt') $stage
foreach ($f in 'LICENSE.md','PERMISSIONS.md','THIRD-PARTY-LICENSES.md') {
    Copy-Item (Join-Path $root "segagenesisrecomp\$f") (Join-Path $lic "segagenesisrecomp-$f")
}
Write-Host "stage pronto: $stage"
