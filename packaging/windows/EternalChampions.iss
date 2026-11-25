; Instalador Windows do Eternal Champions Recomp (Inno Setup 6).
; Monte o stage antes:  pwsh packaging\windows\stage.ps1
; Compile:              ISCC.exe /DAppVersion=1.0.0 packaging\windows\EternalChampions.iss
;
; Instala por usuario (sem admin) em %LOCALAPPDATA%\Programs: o jogo e o launcher gravam
; settings.ini, rom-EC.cfg e os saves ao lado dos executaveis, o que nao funciona em Program Files.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#define AppName "Eternal Champions Recomp"
#define Stage "..\out\windows\stage"
#define RomSha256 "6d8bdaf8c994d9f718c8f991900c58cdf1af84e030524b57f6dda263cc4ea247"

[Setup]
AppId={{6B0F3C1E-5E8A-4F43-9C1D-EC1993C0FFEE}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Eternal Champions Recomp
AppPublisherURL=https://github.com/ggmesquita-art/eternalchampions
AppSupportURL=https://github.com/ggmesquita-art/eternalchampions/issues
DefaultDirName={userpf}\EternalChampionsRecomp
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=..\out
OutputBaseFilename=EternalChampionsRecomp-{#AppVersion}-windows-x64-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\EternalChampions.exe
LicenseFile={#Stage}\THIRD_PARTY_NOTICES.txt

[Languages]
Name: "ptbr"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
ptbr.RomTitle=ROM do jogo
ptbr.RomDesc=Selecione o seu dump de Eternal Champions (EUA)
ptbr.RomInfo=O jogo precisa da ROM US de Eternal Champions (3 MiB, MD5 bde8b2396241aa27463ca875e604b8d3). Ela sera copiada para a pasta do jogo. Voce pode deixar em branco e escolher depois no launcher.
ptbr.RomLabel=Arquivo da ROM (.gen, .md, .bin):
ptbr.RomMissing=Arquivo nao encontrado.
ptbr.RomBadHash=O SHA-256 deste arquivo nao confere com a versao US suportada. Outras versoes podem nao funcionar.%n%nUsar mesmo assim?
ptbr.DesktopIcon=Criar atalho na area de trabalho
en.RomTitle=Game ROM
en.RomDesc=Select your Eternal Champions (USA) dump
en.RomInfo=The game needs the US Eternal Champions ROM (3 MiB, MD5 bde8b2396241aa27463ca875e604b8d3). It will be copied to the game folder. You may leave this empty and pick it later in the launcher.
en.RomLabel=ROM file (.gen, .md, .bin):
en.RomMissing=File not found.
en.RomBadHash=This file's SHA-256 does not match the supported US version. Other versions may not work.%n%nUse it anyway?
en.DesktopIcon=Create a desktop shortcut

[Tasks]
Name: "desktopicon"; Description: "{cm:DesktopIcon}"; Flags: unchecked

[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Dirs]
Name: "{app}\roms"

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\EternalChampions.exe"; WorkingDir: "{app}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{userdesktop}\{#AppName}"; Filename: "{app}\EternalChampions.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\EternalChampions.exe"; WorkingDir: "{app}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; a copia da ROM e o apontador para ela; settings.ini e saves ficam (dados do usuario)
Type: files; Name: "{app}\roms\eternal_champions_us.gen"
Type: files; Name: "{app}\rom-EC.cfg"
; logs e arquivos de diagnostico gerados pelo runner ao rodar
Type: filesandordirs; Name: "{app}\logs"
Type: files; Name: "{app}\dispatch_misses.toml"
Type: files; Name: "{app}\floor_coverage.toml"
Type: files; Name: "{app}\floor_unsafe.toml"
Type: files; Name: "{app}\interior_label_misses.toml"
Type: files; Name: "{app}\launcher_autoplay.txt"
Type: dirifempty; Name: "{app}"

[Code]
var
  RomPage: TInputFileWizardPage;

procedure InitializeWizard;
begin
  RomPage := CreateInputFilePage(wpSelectDir, CustomMessage('RomTitle'), CustomMessage('RomDesc'), CustomMessage('RomInfo'));
  RomPage.Add(CustomMessage('RomLabel'), 'ROM (*.gen;*.md;*.bin)|*.gen;*.md;*.bin|*.*|*.*', '.gen');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  F: String;
begin
  Result := True;
  if CurPageID = RomPage.ID then begin
    F := Trim(RomPage.Values[0]);
    if F = '' then Exit;
    if not FileExists(F) then begin
      MsgBox(CustomMessage('RomMissing'), mbError, MB_OK);
      Result := False;
      Exit;
    end;
    if LowerCase(GetSHA256OfFile(F)) <> '{#RomSha256}' then
      Result := MsgBox(CustomMessage('RomBadHash'), mbConfirmation, MB_YESNO) = IDYES;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  F, Dst: String;
  Lines: TArrayOfString;
begin
  if CurStep <> ssPostInstall then Exit;
  F := Trim(RomPage.Values[0]);
  if F = '' then Exit;
  Dst := ExpandConstant('{app}\roms\eternal_champions_us.gen');
  if CompareText(F, Dst) <> 0 then
    FileCopy(F, Dst, False);
  { rom-EC.cfg: uma linha com o caminho absoluto, em UTF-8 sem BOM (o formato que launcher e runner leem) }
  SetArrayLength(Lines, 1);
  Lines[0] := Dst;
  SaveStringsToUTF8FileWithoutBOM(ExpandConstant('{app}\rom-EC.cfg'), Lines, False);
end;
