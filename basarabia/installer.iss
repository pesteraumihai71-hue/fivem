; ============================================================================
;  Basarabia Roleplay - installer (Inno Setup 6)
;
;  Cum se folosește:
;   1. Rulați basarabia\ci\build.ps1 (sau build-ul automat din GitHub Actions)
;      -> clientul asamblat apare în dist\
;   2. Deschideți acest fișier în Inno Setup și apăsați Build > Compile
;      (sau: iscc basarabia\installer.iss).
;   3. Rezultatul: basarabia\output\BasarabiaRP-Setup.exe
; ============================================================================

#define AppName      "Basarabia RP"
#define AppLongName  "Basarabia Roleplay"
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#define AppExe       "BasarabiaRP.exe"
#define AppSite      "https://basarabiarp.md"
; Folderul cu clientul compilat (relativ la acest fișier)
#define BuildDir     "..\dist"

[Setup]
; Un GUID unic pentru produsul vostru - NU îl schimbați după prima lansare
AppId={{6A1F2C7E-5B3D-4E8A-9C21-B4A5A2D0B7E1}
AppName={#AppLongName}
AppVersion={#AppVersion}
AppVerName={#AppLongName} {#AppVersion}
AppPublisher={#AppLongName}
AppPublisherURL={#AppSite}
AppSupportURL={#AppSite}
; Același folder pe care îl folosește clientul (Installer.cpp -> GetRootPath)
DefaultDirName={localappdata}\{#AppName}
DisableDirPage=yes
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Nu cere drepturi de administrator (instalare per utilizator, ca FiveM)
PrivilegesRequired=lowest
OutputDir=output
OutputBaseFilename=BasarabiaRP-Setup
SetupIconFile=..\code\client\launcher\fivem.ico
UninstallDisplayIcon={app}\{#AppExe}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
; Imaginile cu logo-ul Basarabia RP (normal + ecrane cu scalare 200%)
WizardImageFile=assets\wizard_1x.bmp,assets\wizard_2x.bmp
WizardSmallImageFile=assets\small_1x.bmp,assets\small_2x.bmp
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

[Languages]
Name: "ro"; MessagesFile: "Romanian.isl"
Name: "ru"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
; Tot clientul compilat, fără fișierele de debug
Source: "{#BuildDir}\*"; DestDir: "{app}"; \
  Excludes: "*.pdb,*.lib,*.exp,*.ilk,*.iobj,*.ipdb,*.formaldev"; \
  Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}\{#AppName}.app\data"
Type: filesandordirs; Name: "{app}\{#AppName}.app\logs"
Type: filesandordirs; Name: "{app}\{#AppName}.app\crashes"

[Code]
// Verifică dacă GTA V (Legacy) e instalat; dacă nu, avertizează jucătorul.
function GtaFound(): Boolean;
var
  P: String;
begin
  Result := False;
  if RegQueryStringValue(HKLM64, 'SOFTWARE\WOW6432Node\Rockstar Games\Grand Theft Auto V', 'InstallFolder', P) then
    Result := FileExists(AddBackslash(P) + 'GTA5.exe');
  if (not Result) and RegQueryStringValue(HKLM64, 'SOFTWARE\WOW6432Node\Rockstar Games\GTAV', 'InstallFolderSteam', P) then
    Result := FileExists(AddBackslash(P) + 'GTA5.exe') or FileExists(AddBackslash(P) + 'GTAV\GTA5.exe');
  if (not Result) and RegQueryStringValue(HKLM64, 'SOFTWARE\WOW6432Node\Rockstar Games\GTAV', 'InstallFolderEpic', P) then
    Result := FileExists(AddBackslash(P) + 'GTA5.exe');
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  if not GtaFound() then
  begin
    if MsgBox('Nu am găsit Grand Theft Auto V pe acest calculator.' + #13#10 + #13#10 +
              'Basarabia RP are nevoie de GTA V (Steam, Epic Games sau Rockstar Games Launcher).' + #13#10 +
              'Puteți instala acum și alege folderul jocului la prima pornire.' + #13#10 + #13#10 +
              'Continuați instalarea?', mbConfirmation, MB_YESNO) = IDNO then
      Result := False;
  end;
end;
