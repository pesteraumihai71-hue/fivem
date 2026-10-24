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

[Dirs]
; Folderul intern al motorului (FiveM.app) e ascuns in Explorer - jucatorul vede doar Basarabia RP
Name: "{app}\FiveM.app"; Attribs: hidden

[UninstallDelete]
Type: filesandordirs; Name: "{app}\FiveM.app\data"
Type: filesandordirs; Name: "{app}\FiveM.app\logs"
Type: filesandordirs; Name: "{app}\FiveM.app\crashes"

[Code]
// Inchide launcherul / jocul pornit, ca installerul sa poata inlocui fisierele.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /T /IM BasarabiaRP.exe', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /T /FI "IMAGENAME eq FiveM_*"', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Sleep(1000);
  Result := '';
end;

// In timpul instalarii nu afisam numele fisierelor copiate (doar bara de progres).
procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpInstalling then
    WizardForm.FilenameLabel.Visible := False;
end;

// Caută GTA V (Legacy sau Enhanced) în toate locurile cunoscute.
function HasGtaExe(Dir: String): Boolean;
begin
  Result := False;
  if Dir = '' then Exit;
  Dir := RemoveBackslashUnlessRoot(Dir);
  Result := FileExists(Dir + '\GTA5.exe') or FileExists(Dir + '\GTA5_Enhanced.exe') or
            FileExists(ExtractFileDir(Dir) + '\GTA5.exe') or FileExists(ExtractFileDir(Dir) + '\GTA5_Enhanced.exe') or
            FileExists(Dir + '\GTAV\GTA5.exe');
end;

function RegDir(Root: Integer; Key, Value: String): Boolean;
var
  P: String;
begin
  Result := RegQueryStringValue(Root, Key, Value, P) and HasGtaExe(P);
end;

function CheckRoot(Root: Integer): Boolean;
begin
  Result :=
    RegDir(Root, 'SOFTWARE\WOW6432Node\Rockstar Games\Grand Theft Auto V', 'InstallFolder') or
    RegDir(Root, 'SOFTWARE\WOW6432Node\Rockstar Games\Grand Theft Auto V', 'InstallFolderEpic') or
    RegDir(Root, 'SOFTWARE\Rockstar Games\Grand Theft Auto V', 'InstallFolderEpic') or
    RegDir(Root, 'SOFTWARE\WOW6432Node\Rockstar Games\Grand Theft Auto V', 'InstallFolderXboxPc') or
    RegDir(Root, 'SOFTWARE\Rockstar Games\Grand Theft Auto V', 'InstallFolder') or
    RegDir(Root, 'SOFTWARE\WOW6432Node\Rockstar Games\Grand Theft Auto V Enhanced', 'InstallFolder') or
    RegDir(Root, 'SOFTWARE\WOW6432Node\Rockstar Games\GTAV', 'InstallFolderSteam') or
    RegDir(Root, 'SOFTWARE\WOW6432Node\Rockstar Games\GTAV', 'InstallFolderEpic') or
    RegDir(Root, 'SOFTWARE\WOW6432Node\Rockstar Games\GTA V Enhanced', 'InstallFolderSteam') or
    RegDir(Root, 'SOFTWARE\WOW6432Node\Rockstar Games\GTA V Enhanced', 'InstallFolderEpic') or
    RegDir(Root, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 271590', 'InstallLocation') or
    RegDir(Root, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 3240220', 'InstallLocation') or
    RegDir(Root, 'SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 271590', 'InstallLocation') or
    RegDir(Root, 'SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 3240220', 'InstallLocation');
end;

function GtaFound(): Boolean;
var
  Drives: String;
  I: Integer;
  D: String;
begin
  Result := CheckRoot(HKLM64) or CheckRoot(HKLM32) or CheckRoot(HKCU);
  if Result then Exit;
  // Locuri obișnuite pe fiecare disc (C: ... H:)
  Drives := 'CDEFGH';
  for I := 1 to Length(Drives) do
  begin
    D := Drives[I] + ':';
    if HasGtaExe(D + '\Program Files\Rockstar Games\Grand Theft Auto V') or
       HasGtaExe(D + '\Program Files (x86)\Steam\steamapps\common\Grand Theft Auto V') or
       HasGtaExe(D + '\SteamLibrary\steamapps\common\Grand Theft Auto V') or
       HasGtaExe(D + '\Program Files\Epic Games\GTAV') or
       HasGtaExe(D + '\Epic Games\GTAV') or
       HasGtaExe(D + '\Games\Grand Theft Auto V') then
    begin
      Result := True;
      Exit;
    end;
  end;
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  if not GtaFound() then
  begin
    if MsgBox('Nu am găsit automat Grand Theft Auto V pe acest calculator.' + #13#10 +
              'Dacă îl ai instalat, apasă Da: la prima pornire vei alege folderul jocului.' + #13#10 + #13#10 +
              'Basarabia RP are nevoie de GTA V (Steam, Epic Games sau Rockstar Games Launcher).' + #13#10 + #13#10 +
              'Continuați instalarea?', mbConfirmation, MB_YESNO) = IDNO then
      Result := False;
  end;
end;
