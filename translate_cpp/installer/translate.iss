; =============================================================================
;  Inno Setup script for Translate (translate_text + GUI).
;
;  Build with make_installer.bat (it runs build_static.bat first if needed),
;  or:  "C:\Program Files (x86)\Inno Setup 7\ISCC.exe" installer\translate.iss
;
;  Packages the dist\ folder produced by build_static.bat:
;    - translate_text.exe          (llama.cpp + ggml CPU/Vulkan/CUDA linked in)
;    - translate_gui.exe           (+ Qt DLLs and plugins from windeployqt)
;    - cublas64_*.dll              (optional component: CUDA on NVIDIA GPUs)
;  Models are NOT bundled; they download on first use (4 - 22 GB each).
; =============================================================================

#define AppName      "Translate"
#define AppVersion   "1.0.0"
#define AppPublisher "David Vickers"
#define AppExe       "translate_gui.exe"
#define Dist         SourcePath + "..\dist"

#if !FileExists(Dist + "\translate_text.exe") || !FileExists(Dist + "\" + AppExe)
#error "dist\ is incomplete - run build_static.bat first"
#endif
#define HaveCublas  FileExists(Dist + "\cublas64_12.dll") || FileExists(Dist + "\cublas64_13.dll")

[Setup]
; Keep this AppId for all future versions so upgrades replace the old install.
AppId={{A4D7E9B2-3C61-4E8F-B5A0-71F2C8D94E36}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=output
OutputBaseFilename=Translate-{#AppVersion}-setup
SetupIconFile=..\src\gui\translate.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName} {#AppVersion}
InfoBeforeFile=license.txt
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
LZMAUseSeparateProcess=yes
CloseApplications=yes
RestartApplications=no
UsedUserAreasWarning=no
ChangesEnvironment=yes

[Types]
Name: "full";    Description: "Full installation"
Name: "compact"; Description: "Compact installation (no CUDA libraries; uses Vulkan or CPU)"
Name: "custom";  Description: "Custom installation"; Flags: iscustom

[Components]
Name: "main"; Description: "Translate program (CPU and Vulkan GPU support)"; Types: full compact custom; Flags: fixed
#if HaveCublas
Name: "cuda"; Description: "NVIDIA CUDA support (cuBLAS libraries, about 800 MB) - fastest on NVIDIA GPUs"; Types: full
#endif

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "contextmenu"; Description: "Add ""Translate"" to the right-click menu of .txt, .md, .srt and .docx files"; GroupDescription: "Explorer:"
Name: "addtopath";   Description: "Add the command-line program (translate_text) to PATH"; GroupDescription: "Command line:"; Flags: unchecked

[Files]
Source: "{#Dist}\*"; DestDir: "{app}"; Excludes: "cublas*.dll"; Components: main; Flags: ignoreversion recursesubdirs createallsubdirs
#if HaveCublas
Source: "{#Dist}\cublas*.dll"; DestDir: "{app}"; Components: cuda; Flags: ignoreversion
#endif
Source: "readme.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; Comment: "Translate text and documents on this computer"
Name: "{autoprograms}\{#AppName} - read me"; Filename: "{app}\readme.txt"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.txt\shell\Translate"; ValueType: string; ValueData: "Translate..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.txt\shell\Translate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#AppExe}"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.txt\shell\Translate\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.md\shell\Translate"; ValueType: string; ValueData: "Translate..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.md\shell\Translate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#AppExe}"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.md\shell\Translate\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\Translate"; ValueType: string; ValueData: "Translate..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\Translate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#AppExe}"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\Translate\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.docx\shell\Translate"; ValueType: string; ValueData: "Translate..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.docx\shell\Translate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#AppExe}"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.docx\shell\Translate\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: contextmenu
; Window settings saved by the program; removed on uninstall.
Root: HKCU; Subkey: "Software\DavidV"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\DavidV\TranslateCpp"; Flags: uninsdeletekey

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Downloaded models in %LOCALAPPDATA%\Translate\models are deliberately kept.
Type: filesandordirs; Name: "{app}\licenses"

[Code]
const
  EnvKeyUser   = 'Environment';
  EnvKeyAll    = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';

function HasNvidiaDriver(): Boolean;
begin
  { nvcuda.dll is installed by every NVIDIA display driver }
  Result := FileExists(ExpandConstant('{sys}\nvcuda.dll'));
end;

procedure InitializeWizard();
begin
#if HaveCublas
  if not HasNvidiaDriver() then
    WizardSelectComponents('!cuda');
#endif
end;

function PathRootKey(): Integer;
begin
  if IsAdminInstallMode() then Result := HKEY_LOCAL_MACHINE else Result := HKEY_CURRENT_USER;
end;

function PathKey(): String;
begin
  if IsAdminInstallMode() then Result := EnvKeyAll else Result := EnvKeyUser;
end;

procedure AddToPath(Dir: String);
var
  Paths: String;
begin
  if not RegQueryStringValue(PathRootKey(), PathKey(), 'Path', Paths) then Paths := '';
  if Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Paths) + ';') > 0 then exit;
  if (Paths <> '') and (Copy(Paths, Length(Paths), 1) <> ';') then Paths := Paths + ';';
  RegWriteExpandStringValue(PathRootKey(), PathKey(), 'Path', Paths + Dir);
end;

procedure RemoveFromPath(Dir: String);
var
  Paths: String;
  P: Integer;
begin
  if not RegQueryStringValue(PathRootKey(), PathKey(), 'Path', Paths) then exit;
  P := Pos(';' + Uppercase(Dir) + ';', ';' + Uppercase(Paths) + ';');
  if P = 0 then exit;
  if P = 1 then
    Delete(Paths, 1, Length(Dir) + 1)
  else
    Delete(Paths, P - 1, Length(Dir) + 1);
  RegWriteExpandStringValue(PathRootKey(), PathKey(), 'Path', Paths);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('addtopath') then
    AddToPath(ExpandConstant('{app}'));
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveFromPath(ExpandConstant('{app}'));
end;
