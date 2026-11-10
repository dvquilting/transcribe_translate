; =============================================================================
;  Inno Setup script for Transcribe & Translate: one program
;  (transcribe_translate.exe) with tabs for speech to text, translation and
;  speech translation, and its engine (tt_engine.exe + tt_translate.dll).
;
;  Build with make_installer.bat (runs build_all.bat first if needed).
;  Models are NOT bundled; each downloads on first use.
; =============================================================================

#define AppName      "Transcribe & Translate"
#define AppVersion   "1.1.0"
#define AppPublisher "David Vickers"
#define Dist         SourcePath + "..\dist"

#if !FileExists(Dist + "\transcribe_translate.exe") || !FileExists(Dist + "\tt_engine.exe") || !FileExists(Dist + "\tt_translate.dll")
#error "dist\ is incomplete - run build_all.bat first"
#endif
#define HaveCublas  FileExists(Dist + "\cublas64_12.dll") || FileExists(Dist + "\cublas64_13.dll")
#define HaveFfmpeg  FileExists(Dist + "\ffmpeg.exe")

[Setup]
; Keep this AppId for all future versions so upgrades replace the old install.
AppId={{6C3E91B2-7A4D-4F18-9E25-B8D04A6F1C37}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\Transcribe and Translate
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=output
OutputBaseFilename=TranscribeTranslate-{#AppVersion}-setup
SetupIconFile=..\src\gui\transcribe_translate.ico
UninstallDisplayIcon={app}\transcribe_translate.exe
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
Name: "main"; Description: "The program (CPU and Vulkan GPU support)"; Types: full compact custom; Flags: fixed
#if HaveCublas
Name: "cuda"; Description: "NVIDIA CUDA support (cuBLAS libraries, about 800 MB) - fastest on NVIDIA GPUs"; Types: full
#endif
#if HaveFfmpeg
Name: "ffmpeg"; Description: "FFmpeg (needed for video files, M4A/AAC audio and dubbing)"; Types: full compact
#endif

[Tasks]
Name: "desktopicon"; Description: "Desktop shortcut"; GroupDescription: "{cm:AdditionalIcons}"
Name: "contextmenu"; Description: "Add the program to the right-click menu of audio, video and text files"; GroupDescription: "Explorer:"
Name: "addtopath";   Description: "Add the command-line engine (tt_engine) to PATH"; GroupDescription: "Command line:"; Flags: unchecked

[Files]
Source: "{#Dist}\*"; DestDir: "{app}"; Excludes: "cublas*.dll,ffmpeg.exe"; Components: main; Flags: ignoreversion recursesubdirs createallsubdirs
#if HaveCublas
Source: "{#Dist}\cublas*.dll"; DestDir: "{app}"; Components: cuda; Flags: ignoreversion
#endif
#if HaveFfmpeg
Source: "{#Dist}\ffmpeg.exe"; DestDir: "{app}"; Components: ffmpeg; Flags: ignoreversion
#endif
Source: "readme.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\transcribe_translate.exe"; Comment: "Transcribe, translate and translate speech - on this computer"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\transcribe_translate.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp3\shell\TranscribeTranslate"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp3\shell\TranscribeTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp3\shell\TranscribeTranslate\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --transcribe ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp3\shell\TranscribeTranslateSpeech"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp3\shell\TranscribeTranslateSpeech"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp3\shell\TranscribeTranslateSpeech\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --speech ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wav\shell\TranscribeTranslate"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wav\shell\TranscribeTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wav\shell\TranscribeTranslate\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --transcribe ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wav\shell\TranscribeTranslateSpeech"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wav\shell\TranscribeTranslateSpeech"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wav\shell\TranscribeTranslateSpeech\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --speech ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.m4a\shell\TranscribeTranslate"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.m4a\shell\TranscribeTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.m4a\shell\TranscribeTranslate\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --transcribe ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.m4a\shell\TranscribeTranslateSpeech"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.m4a\shell\TranscribeTranslateSpeech"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.m4a\shell\TranscribeTranslateSpeech\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --speech ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.flac\shell\TranscribeTranslate"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.flac\shell\TranscribeTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.flac\shell\TranscribeTranslate\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --transcribe ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.flac\shell\TranscribeTranslateSpeech"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.flac\shell\TranscribeTranslateSpeech"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.flac\shell\TranscribeTranslateSpeech\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --speech ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.ogg\shell\TranscribeTranslate"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.ogg\shell\TranscribeTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.ogg\shell\TranscribeTranslate\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --transcribe ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.ogg\shell\TranscribeTranslateSpeech"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.ogg\shell\TranscribeTranslateSpeech"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.ogg\shell\TranscribeTranslateSpeech\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --speech ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.opus\shell\TranscribeTranslate"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.opus\shell\TranscribeTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.opus\shell\TranscribeTranslate\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --transcribe ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.opus\shell\TranscribeTranslateSpeech"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.opus\shell\TranscribeTranslateSpeech"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.opus\shell\TranscribeTranslateSpeech\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --speech ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.aac\shell\TranscribeTranslate"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.aac\shell\TranscribeTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.aac\shell\TranscribeTranslate\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --transcribe ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.aac\shell\TranscribeTranslateSpeech"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.aac\shell\TranscribeTranslateSpeech"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.aac\shell\TranscribeTranslateSpeech\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --speech ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wma\shell\TranscribeTranslate"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wma\shell\TranscribeTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wma\shell\TranscribeTranslate\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --transcribe ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wma\shell\TranscribeTranslateSpeech"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wma\shell\TranscribeTranslateSpeech"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wma\shell\TranscribeTranslateSpeech\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --speech ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp4\shell\TranscribeTranslate"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp4\shell\TranscribeTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp4\shell\TranscribeTranslate\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --transcribe ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp4\shell\TranscribeTranslateSpeech"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp4\shell\TranscribeTranslateSpeech"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp4\shell\TranscribeTranslateSpeech\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --speech ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mkv\shell\TranscribeTranslate"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mkv\shell\TranscribeTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mkv\shell\TranscribeTranslate\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --transcribe ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mkv\shell\TranscribeTranslateSpeech"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mkv\shell\TranscribeTranslateSpeech"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mkv\shell\TranscribeTranslateSpeech\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --speech ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mov\shell\TranscribeTranslate"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mov\shell\TranscribeTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mov\shell\TranscribeTranslate\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --transcribe ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mov\shell\TranscribeTranslateSpeech"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mov\shell\TranscribeTranslateSpeech"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mov\shell\TranscribeTranslateSpeech\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --speech ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.avi\shell\TranscribeTranslate"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.avi\shell\TranscribeTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.avi\shell\TranscribeTranslate\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --transcribe ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.avi\shell\TranscribeTranslateSpeech"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.avi\shell\TranscribeTranslateSpeech"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.avi\shell\TranscribeTranslateSpeech\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --speech ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.webm\shell\TranscribeTranslate"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.webm\shell\TranscribeTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.webm\shell\TranscribeTranslate\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --transcribe ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.webm\shell\TranscribeTranslateSpeech"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.webm\shell\TranscribeTranslateSpeech"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.webm\shell\TranscribeTranslateSpeech\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --speech ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.txt\shell\TranscribeTranslateText"; ValueType: string; ValueData: "Translate..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.txt\shell\TranscribeTranslateText"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.txt\shell\TranscribeTranslateText\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --translate ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.md\shell\TranscribeTranslateText"; ValueType: string; ValueData: "Translate..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.md\shell\TranscribeTranslateText"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.md\shell\TranscribeTranslateText\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --translate ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\TranscribeTranslateText"; ValueType: string; ValueData: "Translate..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\TranscribeTranslateText"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\TranscribeTranslateText\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --translate ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.docx\shell\TranscribeTranslateText"; ValueType: string; ValueData: "Translate..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.docx\shell\TranscribeTranslateText"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.docx\shell\TranscribeTranslateText\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --translate ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.pdf\shell\TranscribeTranslateText"; ValueType: string; ValueData: "Translate..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.pdf\shell\TranscribeTranslateText"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_translate.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.pdf\shell\TranscribeTranslateText\command"; ValueType: string; ValueData: """{app}\transcribe_translate.exe"" --translate ""%1"""; Tasks: contextmenu
; Window settings saved by the program; removed on uninstall.
Root: HKCU; Subkey: "Software\DavidV"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\DavidV\TranscribeTranslate"; Flags: uninsdeletekey

[Run]
Filename: "{app}\transcribe_translate.exe"; Description: "{cm:LaunchProgram,Transcribe && Translate}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Downloaded models (%USERPROFILE%\.cache\crispasr and %LOCALAPPDATA%\Translate\models) are kept,
; and so is the job history (%APPDATA%\DavidV\TranscribeTranslate).
Type: filesandordirs; Name: "{app}\licenses"
[Code]
const
  EnvKeyUser   = 'Environment';
  EnvKeyAll    = 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';

function HasNvidiaDriver(): Boolean;
begin
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
