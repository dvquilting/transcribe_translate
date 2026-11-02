; =============================================================================
;  Inno Setup script for Speech Translate - one installer for three programs:
;    Speech Translate  (speech_translate_gui.exe)  audio/video -> translated subtitles
;    Transcribe        (transcribe_multi_gui.exe)  speech to text
;    Translate         (translate_gui.exe)         text and document translation
;  plus their command-line programs (transcribe_multi.exe, translate_text.exe).
;
;  Build with make_installer.bat (runs build_all.bat first if needed).
;  Models are NOT bundled; each downloads on first use.
; =============================================================================

#define AppName      "Speech Translate"
#define AppVersion   "1.0.0"
#define AppPublisher "David Vickers"
#define Dist         SourcePath + "..\dist"
#define Redist       SourcePath + "..\..\transcribe_cpp\installer\redist"

#if !FileExists(Dist + "\speech_translate_gui.exe") || !FileExists(Dist + "\transcribe_multi.exe") || !FileExists(Dist + "\translate_text.exe")
#error "dist\ is incomplete - run build_all.bat first"
#endif
#define HaveCublas  FileExists(Dist + "\cublas64_12.dll") || FileExists(Dist + "\cublas64_13.dll")
#define HaveFfmpeg  FileExists(Redist + "\ffmpeg.exe")

[Setup]
; Keep this AppId for all future versions so upgrades replace the old install.
AppId={{D2B8F4A7-5E13-4C9A-8F60-3A7C1E29B5D4}
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
OutputBaseFilename=SpeechTranslate-{#AppVersion}-setup
SetupIconFile=..\src\speech_translate.ico
UninstallDisplayIcon={app}\speech_translate_gui.exe
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
Name: "main"; Description: "Speech Translate, Transcribe and Translate (CPU and Vulkan GPU support)"; Types: full compact custom; Flags: fixed
#if HaveCublas
Name: "cuda"; Description: "NVIDIA CUDA support (cuBLAS libraries, about 800 MB) - fastest on NVIDIA GPUs"; Types: full
#endif
#if HaveFfmpeg
Name: "ffmpeg"; Description: "FFmpeg (needed for video files, M4A/AAC audio and dubbing)"; Types: full compact
#endif

[Tasks]
Name: "desktopicon"; Description: "Desktop shortcut for Speech Translate"; GroupDescription: "{cm:AdditionalIcons}"
Name: "contextmenu"; Description: "Add the programs to the right-click menu of audio, video and text files"; GroupDescription: "Explorer:"
Name: "addtopath";   Description: "Add the command-line programs (transcribe_multi, translate_text) to PATH"; GroupDescription: "Command line:"; Flags: unchecked

[Files]
Source: "{#Dist}\*"; DestDir: "{app}"; Excludes: "cublas*.dll,ffmpeg.exe"; Components: main; Flags: ignoreversion recursesubdirs createallsubdirs
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
Name: "{autoprograms}\{#AppName}\Speech Translate"; Filename: "{app}\speech_translate_gui.exe"; Comment: "Translate the speech in audio and video files into subtitles"
Name: "{autoprograms}\{#AppName}\Transcribe"; Filename: "{app}\transcribe_multi_gui.exe"; Comment: "Transcribe audio and video files"
Name: "{autoprograms}\{#AppName}\Translate"; Filename: "{app}\translate_gui.exe"; Comment: "Translate text and documents"
Name: "{autoprograms}\{#AppName}\Read me"; Filename: "{app}\readme.txt"
Name: "{autodesktop}\Speech Translate"; Filename: "{app}\speech_translate_gui.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp3\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp3\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_multi_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp3\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\transcribe_multi_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp3\shell\SpeechTranslate"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp3\shell\SpeechTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\speech_translate_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp3\shell\SpeechTranslate\command"; ValueType: string; ValueData: """{app}\speech_translate_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wav\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wav\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_multi_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wav\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\transcribe_multi_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wav\shell\SpeechTranslate"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wav\shell\SpeechTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\speech_translate_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wav\shell\SpeechTranslate\command"; ValueType: string; ValueData: """{app}\speech_translate_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.m4a\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.m4a\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_multi_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.m4a\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\transcribe_multi_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.m4a\shell\SpeechTranslate"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.m4a\shell\SpeechTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\speech_translate_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.m4a\shell\SpeechTranslate\command"; ValueType: string; ValueData: """{app}\speech_translate_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.flac\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.flac\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_multi_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.flac\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\transcribe_multi_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.flac\shell\SpeechTranslate"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.flac\shell\SpeechTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\speech_translate_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.flac\shell\SpeechTranslate\command"; ValueType: string; ValueData: """{app}\speech_translate_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.ogg\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.ogg\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_multi_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.ogg\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\transcribe_multi_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.ogg\shell\SpeechTranslate"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.ogg\shell\SpeechTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\speech_translate_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.ogg\shell\SpeechTranslate\command"; ValueType: string; ValueData: """{app}\speech_translate_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.opus\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.opus\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_multi_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.opus\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\transcribe_multi_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.opus\shell\SpeechTranslate"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.opus\shell\SpeechTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\speech_translate_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.opus\shell\SpeechTranslate\command"; ValueType: string; ValueData: """{app}\speech_translate_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.aac\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.aac\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_multi_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.aac\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\transcribe_multi_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.aac\shell\SpeechTranslate"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.aac\shell\SpeechTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\speech_translate_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.aac\shell\SpeechTranslate\command"; ValueType: string; ValueData: """{app}\speech_translate_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wma\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wma\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_multi_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wma\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\transcribe_multi_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wma\shell\SpeechTranslate"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wma\shell\SpeechTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\speech_translate_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.wma\shell\SpeechTranslate\command"; ValueType: string; ValueData: """{app}\speech_translate_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp4\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp4\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_multi_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp4\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\transcribe_multi_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp4\shell\SpeechTranslate"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp4\shell\SpeechTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\speech_translate_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mp4\shell\SpeechTranslate\command"; ValueType: string; ValueData: """{app}\speech_translate_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mkv\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mkv\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_multi_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mkv\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\transcribe_multi_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mkv\shell\SpeechTranslate"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mkv\shell\SpeechTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\speech_translate_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mkv\shell\SpeechTranslate\command"; ValueType: string; ValueData: """{app}\speech_translate_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mov\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mov\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_multi_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mov\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\transcribe_multi_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mov\shell\SpeechTranslate"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mov\shell\SpeechTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\speech_translate_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.mov\shell\SpeechTranslate\command"; ValueType: string; ValueData: """{app}\speech_translate_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.avi\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.avi\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_multi_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.avi\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\transcribe_multi_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.avi\shell\SpeechTranslate"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.avi\shell\SpeechTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\speech_translate_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.avi\shell\SpeechTranslate\command"; ValueType: string; ValueData: """{app}\speech_translate_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.webm\shell\Transcribe"; ValueType: string; ValueData: "Transcribe (speech to text)"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.webm\shell\Transcribe"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\transcribe_multi_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.webm\shell\Transcribe\command"; ValueType: string; ValueData: """{app}\transcribe_multi_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.webm\shell\SpeechTranslate"; ValueType: string; ValueData: "Translate speech to subtitles..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.webm\shell\SpeechTranslate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\speech_translate_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.webm\shell\SpeechTranslate\command"; ValueType: string; ValueData: """{app}\speech_translate_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.txt\shell\Translate"; ValueType: string; ValueData: "Translate..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.txt\shell\Translate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\translate_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.txt\shell\Translate\command"; ValueType: string; ValueData: """{app}\translate_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.md\shell\Translate"; ValueType: string; ValueData: "Translate..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.md\shell\Translate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\translate_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.md\shell\Translate\command"; ValueType: string; ValueData: """{app}\translate_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\Translate"; ValueType: string; ValueData: "Translate..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\Translate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\translate_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.srt\shell\Translate\command"; ValueType: string; ValueData: """{app}\translate_gui.exe"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.docx\shell\Translate"; ValueType: string; ValueData: "Translate..."; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.docx\shell\Translate"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\translate_gui.exe"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.docx\shell\Translate\command"; ValueType: string; ValueData: """{app}\translate_gui.exe"" ""%1"""; Tasks: contextmenu
; Window settings saved by the programs; removed on uninstall.
Root: HKCU; Subkey: "Software\DavidV"; Flags: uninsdeletekeyifempty
Root: HKCU; Subkey: "Software\DavidV\SpeechTranslateCpp"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\DavidV\TranscribeMultiCpp"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\DavidV\TranslateCpp"; Flags: uninsdeletekey

[Run]
Filename: "{app}\speech_translate_gui.exe"; Description: "{cm:LaunchProgram,Speech Translate}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Downloaded models (%USERPROFILE%\.cache\crispasr and %LOCALAPPDATA%\Translate\models) are kept.
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
