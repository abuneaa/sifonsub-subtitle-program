[Setup]
AppName=Sifonsub
AppVersion=1.0
DefaultDirName={autopf}\Sifonsub
DefaultGroupName=Sifonsub
UninstallDisplayIcon={app}\qt_test.exe
OutputDir=installer_output
OutputBaseFilename=SifonsubSetup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=src\app_icon.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"

[Files]
Source: "dist\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Sifonsub"; Filename: "{app}\qt_test.exe"
Name: "{group}\Uninstall Sifonsub"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Sifonsub"; Filename: "{app}\qt_test.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Run]
Filename: "{app}\qt_test.exe"; Description: "{cm:LaunchProgram,Sifonsub}"; Flags: nowait postinstall skipifsilent