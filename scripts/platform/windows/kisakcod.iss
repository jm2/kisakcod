; KisakCOD Inno Setup script (ADR-0012), after jm2/balun's build-aux/inno/balun.iss.
; scripts/ci/package-windows.ps1 -Installer compiles it over the folder it
; staged for the zip, so the installer and the zip hold the same files.
;
; The install folder defaults to the Steam Call of Duty 4 folder, beside
; iw3mp.exe, and otherwise to {autopf}\KisakCOD. Uninstalling removes only the
; files this installer copied: the game's files and %APPDATA%\KisakCOD stay.
;
; Defines, all required (iscc /D):
;   AppVersion          the release version, e.g. "0.1.0"
;   AppNumericVersion   its four-part form for the version resource, e.g. "0.1.0.0"
;   SourceDir           the staged package folder
;   OutputDir           where the installer goes
;   OutputBaseFilename  e.g. "kisakcod-0.1.0-windows-amd64-setup"
;   TargetArch          "x64" or "arm64"

#if !Defined(AppVersion) || !Defined(AppNumericVersion) || !Defined(SourceDir) || !Defined(OutputDir) || !Defined(OutputBaseFilename) || !Defined(TargetArch)
  #error Pass AppVersion, AppNumericVersion, SourceDir, OutputDir, OutputBaseFilename and TargetArch with /D
#endif

[Setup]
AppName=KisakCOD
AppVersion={#AppVersion}
AppVerName=KisakCOD {#AppVersion}
; Deterministic application GUID: UUID version 5 of the URL namespace and
; https://github.com/jm2/kisakcod/io.github.jm2.KisakCOD. It must never change.
AppId={{D9EC4207-97F5-5FF0-A12C-5B57B91933B0}
VersionInfoVersion={#AppNumericVersion}
VersionInfoProductVersion={#AppNumericVersion}
VersionInfoTextVersion={#AppVersion}
VersionInfoProductTextVersion={#AppVersion}
VersionInfoProductName=KisakCOD
VersionInfoDescription=KisakCOD Setup
VersionInfoCopyright=Copyright (C) 2026 KisakCOD contributors
AppPublisher=KisakCOD contributors
AppPublisherURL=https://github.com/jm2/kisakcod
AppSupportURL=https://github.com/jm2/kisakcod/issues
AppUpdatesURL=https://github.com/jm2/kisakcod/releases
DefaultDirName={code:DefaultGameDir}
; The default can be the game's own folder, so don't warn that it exists or
; append "KisakCOD" to a folder the player browses to.
DirExistsWarning=no
AppendDefaultDirName=no
DefaultGroupName=KisakCOD
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\KisakCOD-mp.exe
UninstallDisplayName=KisakCOD {#AppVersion}
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
Compression=lzma2/ultra64
SolidCompression=yes
LicenseFile={#SourceDir}\KisakCOD-LICENSE.txt
WizardStyle=modern
SetupIconFile={#SourcePath}..\..\packaging\icons\kisakcod.ico
MinVersion=10.0
; The Steam folder is under Program Files. /CURRENTUSER installs for one user
; into %LOCALAPPDATA%\Programs\KisakCOD instead.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
; Silent installs: /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- [/LOG=<file>]
CloseApplications=yes
CloseApplicationsFilter=KisakCOD-mp.exe,KisakCOD-dedi.exe
SetupLogging=yes
; File times are stored in UTC, so they install unshifted on a host in another
; time zone from the compiling runner's (#947).
TimeStampsInUTC=yes
#if TargetArch == "arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#elif TargetArch == "x64"
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#else
  #error Unsupported TargetArch; expected x64 or arm64
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Only the staged files are copied, and only they are logged for the uninstaller.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\KisakCOD"; Filename: "{app}\KisakCOD-mp.exe"; WorkingDir: "{app}"
Name: "{group}\Uninstall KisakCOD"; Filename: "{uninstallexe}"
Name: "{autodesktop}\KisakCOD"; Filename: "{app}\KisakCOD-mp.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\KisakCOD-mp.exe"; WorkingDir: "{app}"; Description: "Launch KisakCOD"; Flags: nowait postinstall skipifsilent unchecked

[Code]
// True, with Dir set, when the registry value names a folder holding iw3mp.exe.
function GameDirFrom(const RootKey: Integer; const SubKey, ValueName: String; var Dir: String): Boolean;
var
  Path: String;
begin
  Result := False;
  if RegQueryStringValue(RootKey, SubKey, ValueName, Path) then begin
    StringChangeEx(Path, '/', '\', True);
    Path := RemoveBackslashUnlessRoot(Path);
    if (Path <> '') and FileExists(AddBackslash(Path) + 'iw3mp.exe') then begin
      Dir := Path;
      Result := True;
    end;
  end;
end;

// The Steam Call of Duty 4 folder (app 7940), else {autopf}\KisakCOD.
function DefaultGameDir(Param: String): String;
var
  Dir: String;
begin
  if GameDirFrom(HKLM32, 'SOFTWARE\Activision\Call of Duty 4', 'InstallPath', Dir) or
     GameDirFrom(HKLM32, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 7940', 'InstallLocation', Dir) or
     GameDirFrom(HKLM64, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 7940', 'InstallLocation', Dir) then
    Result := Dir
  else if RegQueryStringValue(HKCU, 'Software\Valve\Steam', 'SteamPath', Dir) and
          FileExists(AddBackslash(Dir) + 'steamapps\common\Call of Duty 4\iw3mp.exe') then begin
    StringChangeEx(Dir, '/', '\', True);
    Result := AddBackslash(Dir) + 'steamapps\common\Call of Duty 4';
  end else
    Result := ExpandConstant('{autopf}\KisakCOD');
end;
