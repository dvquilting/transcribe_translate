; =============================================================================
;  Inno Setup script for Transcribe (transcribe_multi + GUI).
;
;  Build with make_installer.bat (it runs build_static.bat first if needed),
;  or:  "C:\Program Files (x86)\Inno Setup 7\ISCC.exe" installer\transcribe.iss
;
;  Packages the dist\ folder produced by build_static.bat:
;    - transcribe_multi.exe        (CrispASR + ggml CPU/Vulkan/CUDA linked in)
;    - transcribe_multi_gui.exe    (+ Qt DLLs and plugins from windeployqt)
;    - cublas64_*.dll              (optional component: CUDA on NVIDIA GPUs)
;    - ffmpeg.exe                  (optional component, if installer\redist\ffmpeg.exe exists)
;  Speech models are NOT bundled; they download on first use (0.4 - 4.3 GB each).
; =============================================================================

#define AppName      "Transcribe"
#define AppVersion   "1.0.0"
#define AppPublisher "David Vickers"
#define AppExe       "transcribe_multi_gui.exe"
#define Dist         SourcePath + "..\dist"
#define Redist       SourcePath + "redist"

#if !FileExists(Dist + "\transcribe_multi.exe") || !FileExists(Dist + "\" + AppExe)
#error "dist\ is incomplete - run build_static.bat first"
#endif
#define HaveCublas  FileExists(Dist + "\cublas64_12.dll") || FileExists(Dist + "\cublas64_13.dll")
#define HaveFfmpeg  FileExists(Redist + "\ffmpeg.exe")

[Setup]
; Keep this AppId for all future versions so upgrades replace the old install.
AppId={{6B3F2C1E-8D4A-4F7B-9E21-5C0A7D93B4E8}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Per-user or all-users install, chosen by the person installing.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=output
OutputBaseFilename=Transcribe-{#AppVersion}-setup
SetupIconFile=..\src\gui\transcribe.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName} {#AppVersion}
InfoBeforeFile=license.txt
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
LZMAUseSeparateProcess=yes
; Replace files of a running copy only after asking the user to close it.
CloseApplications=yes
RestartApplications=no
; The settings key in HKCU is intentional (it is per user either way).
UsedUserAreasWarning=no
; Tell Windows about the PATH change (optional "addtopath" task).
ChangesEnvironment=yes

[Types]
Name: "full";    Description: "Full installation"
Name: "compact"; Description: "Compact installation (no CUDA libraries; uses Vulkan or CPU)"
Name: "custom";  Description: "Custom installation"; Flags: iscustom

[Components]
Name: "main"; Description: "Transcribe program (CPU and Vulkan GPU support)"; Types: full compact custom; Flags: fixed
#if HaveCublas
Name: "cuda"; Description: "NVIDIA CUDA support (cuBLAS libraries, about 800 MB) - fastest on NVIDIA GPUs"; Types: full
#endif
#if HaveFfmpeg
Name: "ffmpeg"; Description: "FFmpeg (needed for video files and M4A/AAC audio)"; Types: full compact
#endif

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "addtopath";   Description: "Add the command-line program (transcribe_multi) to PATH"; GroupDescription: "Command line:"; Flags: unchecked

[Files]
; Everything in dist\ except the optional cuBLAS DLLs.
Source: "{#Dist}\*"; DestDir: "{app}"; Excludes: "cublas*.dll"; Components: main; Flags: ignoreversion recursesubdirs createallsubdirs
#if HaveCublas
Source: "{#Dist}\cublas*.dll"; DestDir: "{app}"; Components: cuda; Flags: ignoreversion
#endif
#if HaveFfmpeg
Source: "{#Redist}\ffmpeg.exe"; DestDir: "{app}"; Components: ffmpeg; Flags: ignoreversion
#if FileExists(Redist + "\ffmpeg-LICENSE.txt")
Source: "{#Redist}\ffmpeg-LICENSE.txt"; DestDir: "{app}\licenses"; Components: ffmpeg; Flags: ignoreversion
#endif
#endif
Source: "readme.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; Comment: "Transcribe audio and video files"
Name: "{autoprograms}\{#AppName} - read me"; Filename: "{app}\readme.txt"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; "Transcribe (speech to text)" on the right-click menu of audio/video files
; (HKA = current user or all users, matching the install mode).
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp3\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp3\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#AppExe}"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp3\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wav\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wav\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#AppExe}"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wav\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.m4a\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.m4a\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#AppExe}"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.m4a\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.flac\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.flac\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#AppExe}"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.flac\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.ogg\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.ogg\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#AppExe}"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.ogg\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.opus\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.opus\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#AppExe}"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.opus\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.aac\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.aac\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#AppExe}"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.aac\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wma\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wma\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#AppExe}"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wma\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp4\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp4\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#AppExe}"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp4\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mkv\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mkv\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#AppExe}"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mkv\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mov\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mov\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#AppExe}"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mov\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.avi\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.avi\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#AppExe}"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.avi\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.webm\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.webm\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#AppExe}"""
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.webm\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""
; Window settings saved by the program; removed on uninstall.
Root: HKCU; Subkey: "Software\DavidV"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\DavidV\TranscribeMultiCpp"; Flags: uninsdeletekey

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Downloaded models in %USERPROFILE%\.cache\crispasr are deliberately kept.
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
  { Pre-select CUDA support only on PCs with an NVIDIA driver. }
  if not HasNvidiaDriver() then
    WizardSelectComponents('!cuda');
#endif
end;

{ ---- PATH handling for the optional "addtopath" task ---- }
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
  { P is where Dir starts in Paths; remove it together with one separator }
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
