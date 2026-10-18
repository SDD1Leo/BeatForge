; BeatForge installer (Inno Setup 6). Built by the Windows CI job:
;   iscc /DAppVersion=1.0.0 /DAppExe=<path to BeatForge.exe> /DMcpDir=<staged MCP server> packaging\windows\BeatForge.iss
; Installs for the current user by default (no admin prompt); the first page
; offers "all users" instead. Unsigned: Windows SmartScreen asks once.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef AppExe
  #define AppExe "..\..\build\windows\app\BeatForge_artefacts\Release\BeatForge.exe"
#endif

[Setup]
AppId={{979FB3AD-79D5-407E-B450-D818EB416D7C}
AppName=BeatForge
AppVersion={#AppVersion}
AppVerName=BeatForge {#AppVersion}
AppPublisher=BeatForge
DefaultDirName={autopf}\BeatForge
DefaultGroupName=BeatForge
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
SetupIconFile=BeatForge.ico
UninstallDisplayIcon={app}\BeatForge.exe
UninstallDisplayName=BeatForge {#AppVersion}
ChangesAssociations=yes
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
OutputDir=..\..\build\windows\installer
OutputBaseFilename=BeatForge-{#AppVersion}-Setup

[Types]
Name: "full"; Description: "BeatForge and the AI assistant bridge"
Name: "app"; Description: "BeatForge only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "app"; Description: "BeatForge"; Types: full app custom; Flags: fixed
#ifdef McpDir
Name: "mcp"; Description: "MCP server for AI assistants such as Claude Code (needs Node.js 20+)"; Types: full
#endif

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked
Name: "association"; Description: "Open .beatforge projects with BeatForge"; GroupDescription: "Files:"

[Files]
Source: "{#AppExe}"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "..\..\app\Resources\fonts\*-OFL.txt"; DestDir: "{app}\licences"; Components: app
Source: "..\..\USER_GUIDE.md"; DestDir: "{app}"; Components: app
#ifdef McpDir
Source: "{#McpDir}\*"; DestDir: "{app}\mcp"; Components: mcp; Flags: recursesubdirs createallsubdirs ignoreversion
#endif

[Icons]
Name: "{autoprograms}\BeatForge"; Filename: "{app}\BeatForge.exe"
Name: "{autodesktop}\BeatForge"; Filename: "{app}\BeatForge.exe"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.beatforge"; ValueType: string; ValueName: ""; ValueData: "BeatForge.Project"; Flags: uninsdeletevalue; Tasks: association
Root: HKA; Subkey: "Software\Classes\BeatForge.Project"; ValueType: string; ValueName: ""; ValueData: "BeatForge project"; Flags: uninsdeletekey; Tasks: association
Root: HKA; Subkey: "Software\Classes\BeatForge.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\BeatForge.exe,0"; Tasks: association
Root: HKA; Subkey: "Software\Classes\BeatForge.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\BeatForge.exe"" ""%1"""; Tasks: association

[Run]
Filename: "{app}\BeatForge.exe"; Description: "Start BeatForge"; Flags: nowait postinstall skipifsilent
