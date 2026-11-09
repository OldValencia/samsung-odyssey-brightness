; Inno Setup script for the Samsung Brightness installer.
; GitHub Actions builds it (see .github/workflows/build.yml). To build it locally:
;   build.cmd 1.2.0
;   ISCC /DAppVersion=1.2.0 installer\SamsungBrightness.iss
;
; The installer works per user: no administrator rights, files go to %LOCALAPPDATA%\Programs.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

#define AppName "Samsung Brightness"
#define AppExe "SamsungBrightness.exe"
#define RepoUrl "https://github.com/OldValencia/samsung-odyssey-brightness"

[Setup]
; Never change AppId: Windows uses it to recognize upgrades of the installed app.
AppId={{6F1B7C2E-4D7A-4E8B-9C35-2B8F0D6A9E41}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=OldValencia
AppPublisherURL={#RepoUrl}
AppSupportURL={#RepoUrl}/issues
VersionInfoVersion={#AppVersion}
DefaultDirName={localappdata}\Programs\SamsungBrightness
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile=..\LICENSE
SetupIconFile=..\resources\app.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
; The running app is closed by PrepareToInstall below.
CloseApplications=no
OutputDir=..\dist
OutputBaseFilename=SamsungBrightness-{#AppVersion}-setup

[Tasks]
Name: "autostart"; Description: "Start with Windows"

[Files]
Source: "..\build\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"

[Registry]
; Same entry as "Start with Windows" in the tray menu, so both stay in sync.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "SamsungBrightness"; \
    ValueType: string; ValueData: """{app}\{#AppExe}"""; Tasks: autostart
; Removes the entry on uninstall even if autostart was turned on later from the tray menu.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "SamsungBrightness"; \
    ValueType: none; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#AppExe}"; Description: "Start {#AppName} now"; Flags: postinstall nowait skipifsilent

[UninstallRun]
Filename: "{app}\{#AppExe}"; Parameters: "exit"; Flags: runhidden waituntilterminated; RunOnceId: "StopApp"

[UninstallDelete]
; Settings with the access token and the optional log are created by the app, not by the installer.
Type: files; Name: "{app}\.env"
Type: files; Name: "{app}\samsung-brightness.log"

[Code]
// Closes a running copy before an upgrade replaces its files. Settings (.env) are kept.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ExePath: String;
  ResultCode: Integer;
begin
  ExePath := ExpandConstant('{app}\{#AppExe}');
  if FileExists(ExePath) then
  begin
    Exec(ExePath, 'exit', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Sleep(1000);
  end;
  Result := '';
end;
