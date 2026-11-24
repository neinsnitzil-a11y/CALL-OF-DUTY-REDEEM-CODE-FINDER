; Inno Setup 6 script - builds dist\installer\COD-Redemption-Hunter-Setup-<version>.exe
; Built by build_windows.bat (which passes /DAppVersion=...). Per-user install by default (no admin needed).
; Uses the same AppId as the Python edition, so installing this replaces it in place; your data in
; %LOCALAPPDATA%\COD Redemption Hunter is kept and read by both.
#define MyAppName "COD Redemption Hunter"
#define MyAppExe "CODRedemptionHunter.exe"
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{8C5E2B1A-4F7D-4B8E-9A61-3D2C7E5F9B10}
AppName={#MyAppName}
AppVersion={#AppVersion}
AppVerName={#MyAppName} {#AppVersion}
AppPublisher=COD Redemption Hunter (open source)
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir=..\dist\installer
OutputBaseFilename=COD-Redemption-Hunter-Setup-{#AppVersion}
SetupIconFile=..\assets\app.ico
UninstallDisplayIcon={app}\{#MyAppExe}
UninstallDisplayName={#MyAppName}
WizardStyle=modern
WizardImageFile=..\assets\wizard_large.bmp
WizardSmallImageFile=..\assets\wizard_small.bmp
LicenseFile=..\LICENSE
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no
ShowLanguageDialog=auto

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "autostart"; Description: "Start with Windows - scan for new codes 24/7 from the tray"; GroupDescription: "Background scanning:"

[InstallDelete]
; leftovers of the Python edition
Type: filesandordirs; Name: "{app}\_internal"
Type: files; Name: "{app}\COD Redemption Hunter.exe"
Type: files; Name: "{app}\codhunter.exe"
Type: files; Name: "{autoprograms}\{#MyAppName} - check codes (console).lnk"

[Files]
Source: "..\dist\CODRedemptionHunter\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExe}"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#MyAppName}"; ValueData: """{app}\{#MyAppExe}"" --minimized"; Flags: uninsdeletevalue; Tasks: autostart

[Run]
; the Python edition scanned from a scheduled task; this edition runs from the tray instead
Filename: "{sys}\schtasks.exe"; Parameters: "/Delete /TN ""CODM Redemption Hunter Service"" /F"; Flags: runhidden; StatusMsg: "Removing the old background task..."
Filename: "{app}\{#MyAppExe}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: files; Name: "{app}\*.log"

[Code]
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var DataDir: String;
begin
  if CurUninstallStep = usUninstall then
    RegDeleteValue(HKEY_CURRENT_USER, 'Software\Microsoft\Windows\CurrentVersion\Run', '{#MyAppName}');
  if CurUninstallStep = usPostUninstall then
  begin
    DataDir := ExpandConstant('{localappdata}\COD Redemption Hunter');
    if DirExists(DataDir) then
      if MsgBox('Also delete your codes database and settings?' + #13#10 + DataDir,
                mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
        DelTree(DataDir, True, True, True);
  end;
end;
