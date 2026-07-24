#ifndef MyAppVersion
  #define MyAppVersion "0.2.0"
#endif
#ifndef SourceExecutable
  #define SourceExecutable "..\..\build\release-windows\Release\modra.exe"
#endif
#ifndef OutputDirectory
  #define OutputDirectory "..\..\dist"
#endif

#define MyAppName "MODRA"
#define MyAppPublisher "PricKG"
#define MyAppURL "https://github.com/PricKG/MODRA"

[Setup]
AppId={{A9E4C67C-A370-47E4-82F4-7DB4C479DF70}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases/latest
DefaultDirName={localappdata}\Programs\MODRA
DefaultGroupName=MODRA
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDirectory}
OutputBaseFilename=MODRA-Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupLogging=yes
ChangesEnvironment=yes
CloseApplications=yes
RestartApplications=no
UninstallDisplayName=MODRA
UninstallDisplayIcon={app}\modra.exe
VersionInfoVersion={#MyAppVersion}.0
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription=Instalador de MODRA
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}
VersionInfoCopyright=Copyright (C) 2026 {#MyAppPublisher}

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "addtopath"; Description: "Agregar MODRA al PATH del usuario"; GroupDescription: "Integración con Windows:"; Flags: checkedonce
Name: "desktopicon"; Description: "Crear un acceso directo en el escritorio"; GroupDescription: "Accesos directos:"; Flags: unchecked

[Files]
Source: "{#SourceExecutable}"; DestDir: "{app}"; DestName: "modra.exe"; Flags: ignoreversion

[Icons]
Name: "{group}\MODRA"; Filename: "{cmd}"; Parameters: "/k ""{app}\modra.exe"""; WorkingDir: "{%USERPROFILE}"; IconFilename: "{app}\modra.exe"
Name: "{autodesktop}\MODRA"; Filename: "{cmd}"; Parameters: "/k ""{app}\modra.exe"""; WorkingDir: "{%USERPROFILE}"; IconFilename: "{app}\modra.exe"; Tasks: desktopicon

[Run]
Filename: "{cmd}"; Parameters: "/k ""{app}\modra.exe --version && echo. && echo Ejecuta modra para iniciar MODRA."""; Description: "Abrir MODRA"; Flags: postinstall nowait skipifsilent

[Code]
function NormalizedPath(const Value: String): String;
begin
  Result := RemoveBackslashUnlessRoot(ExpandFileName(Trim(Value)));
end;

procedure UpdateUserPath(const AddPath: Boolean);
var
  CurrentPath: String;
  UpdatedPath: String;
  AppPath: String;
  Entry: String;
  Remaining: String;
  Separator: Integer;
  Found: Boolean;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', CurrentPath) then
    CurrentPath := '';

  AppPath := NormalizedPath(ExpandConstant('{app}'));
  Remaining := CurrentPath;
  UpdatedPath := '';
  Found := False;

  while Remaining <> '' do
  begin
    Separator := Pos(';', Remaining);
    if Separator = 0 then
    begin
      Entry := Remaining;
      Remaining := '';
    end
    else
    begin
      Entry := Copy(Remaining, 1, Separator - 1);
      Delete(Remaining, 1, Separator);
    end;
    Entry := Trim(Entry);
    if Entry = '' then
      Continue;
    if CompareText(NormalizedPath(Entry), AppPath) = 0 then
    begin
      Found := True;
      if not AddPath then
        Continue;
    end;
    if UpdatedPath <> '' then
      UpdatedPath := UpdatedPath + ';';
    UpdatedPath := UpdatedPath + Entry;
  end;

  if AddPath and not Found then
  begin
    if UpdatedPath <> '' then
      UpdatedPath := UpdatedPath + ';';
    UpdatedPath := UpdatedPath + ExpandConstant('{app}');
  end;

  if UpdatedPath <> CurrentPath then
    RegWriteExpandStringValue(HKCU, 'Environment', 'Path', UpdatedPath);
end;

procedure CurStepChanged(const CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    RegDeleteKeyIncludingSubkeys(
      HKCU,
      'Software\Microsoft\Windows\CurrentVersion\Uninstall\MODRA'
    )
  else if (CurStep = ssPostInstall) and WizardIsTaskSelected('addtopath') then
    UpdateUserPath(True);
end;

procedure CurUninstallStepChanged(const CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    UpdateUserPath(False);
end;
