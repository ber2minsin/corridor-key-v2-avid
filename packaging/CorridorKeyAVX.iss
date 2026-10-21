; Inno Setup 6 — produces Setup-CorridorKeyAVX.exe
; Build: scripts\build_installer.ps1
; End users: double-click the Setup exe. No Python or terminal steps.

#define MyAppName "CorridorKey for Avid"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "CorridorKey Community"
#define MyAppURL "https://github.com/nikopueringer/CorridorKey2.0"

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
; One single Setup EXE (no disk spanning). Payload is ~9GB; lzma2 keeps one file.
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

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; Plugin into global Avid AVX2 folder
Source: "payload\CorridorKey.avx"; DestDir: "{commonpf}\Avid\AVX2_Plug-ins"; Flags: ignoreversion
; Copy under install tree
Source: "payload\CorridorKey.avx"; DestDir: "{app}"; Flags: ignoreversion
; Bundled runtime
Source: "payload\python\*"; DestDir: "{app}\python"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "payload\runtime\*"; DestDir: "{app}\runtime"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "payload\CorridorKey2.0\*"; DestDir: "{app}\CorridorKey2.0"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "payload\config.json"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Uninstall CorridorKey for Avid"; Filename: "{uninstallexe}"

[Registry]
Root: HKLM; Subkey: "Software\CorridorKeyAVX"; ValueType: string; ValueName: "InstallRoot"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; ValueType: expandsz; ValueName: "CORRIDORKEY_AVX_ROOT"; ValueData: "{app}"; Flags: uninsdeletevalue

[Code]
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
    '  "avx_path": "' + JsonEscapePath(AvxPath) + '"' + #13#10 +
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
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpReady then
  begin
    MsgBox('After installation, fully quit and restart Avid Media Composer.' #13#10 +
           'Find CorridorKey under Effect Palette -> Key.' #13#10 +
           'The first time you use it, the AI may take up to a minute to load -- that is normal.',
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
