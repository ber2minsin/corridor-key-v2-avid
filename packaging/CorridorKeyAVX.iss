; Inno Setup 6 — Setup-CorridorKeyAVX.exe (slim; models downloaded at install time)
; Build: scripts\build_installer.ps1

#define MyAppName "CorridorKey for Avid"
#define MyAppVersion "1.11.0"
#define MyAppPublisher "CorridorKey Community"
#define MyAppURL "https://corridorkey.com/"

[Setup]
AppId={{A5EA0728-5C64-3BF3-9E55-1B76A814352C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
DefaultDirName={autopf}\CorridorKeyAVX
DefaultGroupName=CorridorKey for Avid
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=dist
OutputBaseFilename=Setup-CorridorKeyAVX
Compression=lzma2
SolidCompression=yes
DiskSpanning=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
SetupLogging=yes
UninstallDisplayName={#MyAppName}
CloseApplications=no
DisableWelcomePage=no
InfoBeforeFile=setup_info_before.txt
WizardImageStretch=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "downloadmodels"; Description: "Download CorridorKey 2.0 models and CUDA PyTorch now (~6–8 GB, recommended)"; Flags: checkedonce
Name: "desktopicon"; Description: "Create a desktop shortcut to the install folder"; Flags: unchecked

[Files]
Source: "payload\CorridorKey.avx"; DestDir: "{commonpf}\Avid\AVX2_Plug-ins"; Flags: ignoreversion
Source: "payload\CorridorKey.avx"; DestDir: "{app}"; Flags: ignoreversion
Source: "payload\python\*"; DestDir: "{app}\python"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "payload\runtime\*"; DestDir: "{app}\runtime"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "payload\CorridorKey2.0\*"; DestDir: "{app}\CorridorKey2.0"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "payload\post_install.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "payload\config.json"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Uninstall CorridorKey for Avid"; Filename: "{uninstallexe}"
Name: "{group}\CorridorKeyAVX install folder"; Filename: "{app}"
Name: "{autodesktop}\CorridorKeyAVX"; Filename: "{app}"; Tasks: desktopicon

[Registry]
Root: HKLM; Subkey: "Software\CorridorKeyAVX"; ValueType: string; ValueName: "InstallRoot"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\CorridorKeyAVX"; ValueType: string; ValueName: "Version"; ValueData: "{#MyAppVersion}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; ValueType: expandsz; ValueName: "CORRIDORKEY_AVX_ROOT"; ValueData: "{app}"; Flags: uninsdeletevalue

[Code]
var
  DownloadModels: Boolean;

function JsonEscapePath(const Path: String): String;
var
  S: String;
begin
  S := Path;
  StringChangeEx(S, '\', '\\', True);
  Result := S;
end;

procedure WriteConfigJson;
var
  AppDir, AvxPath, Cfg: String;
begin
  AppDir := ExpandConstant('{app}');
  AvxPath := ExpandConstant('{commonpf}\Avid\AVX2_Plug-ins\CorridorKey.avx');
  Cfg :=
    '{' + #13#10 +
    '  "install_root": "' + JsonEscapePath(AppDir) + '",' + #13#10 +
    '  "ck_root": "' + JsonEscapePath(AppDir + '\CorridorKey2.0') + '",' + #13#10 +
    '  "python_exe": "' + JsonEscapePath(AppDir + '\python\python.exe') + '",' + #13#10 +
    '  "server_py": "' + JsonEscapePath(AppDir + '\runtime\server.py') + '",' + #13#10 +
    '  "avx_path": "' + JsonEscapePath(AvxPath) + '",' + #13#10 +
    '  "version": "{#MyAppVersion}"' + #13#10 +
    '}';
  SaveStringToFile(AppDir + '\config.json', Cfg, False);
end;

procedure CopyAvxBesideMediaComposer;
var
  McAvxDir, Src: String;
begin
  McAvxDir := ExpandConstant('{commonpf}\Avid\Avid Media Composer\AVX2_Plug-Ins');
  Src := ExpandConstant('{app}\CorridorKey.avx');
  if DirExists(ExpandConstant('{commonpf}\Avid\Avid Media Composer')) then
  begin
    ForceDirectories(McAvxDir);
    FileCopy(Src, McAvxDir + '\CorridorKey.avx', False);
  end;
end;

function RunPostInstall(const SkipWeights: Boolean): Boolean;
var
  AppDir, Ps1, Args, Cmd: String;
  ResultCode: Integer;
begin
  AppDir := ExpandConstant('{app}');
  Ps1 := AppDir + '\post_install.ps1';
  Args := '-NoProfile -ExecutionPolicy Bypass -File "' + Ps1 + '" -InstallRoot "' + AppDir + '"';
  if SkipWeights then
    Args := Args + ' -SkipWeights';
  Cmd := 'powershell.exe';
  WizardForm.StatusLabel.Caption := 'Downloading models / installing PyTorch (this can take a while)...';
  WizardForm.FilenameLabel.Caption := 'Network required. Log: %LOCALAPPDATA%\CorridorKeyAVX\logs\';
  Result := Exec(Cmd, Args, AppDir, SW_SHOW, ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
  if not Result then
    MsgBox('Post-install setup failed (exit ' + IntToStr(ResultCode) + ').' #13#10 +
           'You can re-run:' #13#10 +
           '  powershell -File "' + Ps1 + '" -InstallRoot "' + AppDir + '"' #13#10 +
           'Logs: %LOCALAPPDATA%\CorridorKeyAVX\logs\',
           mbError, MB_OK);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  AvxDir: String;
begin
  if CurStep = ssPostInstall then
  begin
    AvxDir := ExpandConstant('{commonpf}\Avid\AVX2_Plug-ins');
    if not DirExists(AvxDir) then
      ForceDirectories(AvxDir);
    WriteConfigJson;
    CopyAvxBesideMediaComposer;
    DownloadModels := WizardIsTaskSelected('downloadmodels');
    if DownloadModels then
      RunPostInstall(False)
    else
      RunPostInstall(True);
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpReady then
  begin
    MsgBox('After Setup finishes:' #13#10 +
           '1. Fully quit and restart Avid Media Composer.' #13#10 +
           '2. Effect Palette -> Image -> CorridorKey.' #13#10 +
           '3. First use may still download assets if you skipped that task.' #13#10#13#10 +
           'Requires an NVIDIA GPU and internet for the model download.',
           mbInformation, MB_OK);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  P: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    P := ExpandConstant('{commonpf}\Avid\AVX2_Plug-ins\CorridorKey.avx');
    if FileExists(P) then DeleteFile(P);
    P := ExpandConstant('{commonpf}\Avid\Avid Media Composer\AVX2_Plug-Ins\CorridorKey.avx');
    if FileExists(P) then DeleteFile(P);
  end;
end;

[UninstallDelete]
Type: filesandordirs; Name: "{app}"
