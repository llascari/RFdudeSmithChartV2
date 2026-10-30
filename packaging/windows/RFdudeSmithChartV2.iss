#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

#ifndef AppStage
  #error AppStage must point to the deployed application directory
#endif

[Setup]
AppId={{6A32E39D-9F4B-4A4D-A71C-2E450A94DD2A}
AppName=RFdude Smith Chart V2
AppVersion={#AppVersion}
AppPublisher=RFdude
AppPublisherURL=https://www.RFdude.com
DefaultDirName={autopf}\RFdude Smith Chart V2
DefaultGroupName=RFdude Smith Chart V2
UninstallDisplayIcon={app}\bin\RFdudeSmithChartV2.exe
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
OutputDir=..\..\packages
OutputBaseFilename=RFdudeSmithChartV2-{#AppVersion}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"

[Files]
Source: "{#AppStage}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\RFdude Smith Chart V2"; Filename: "{app}\bin\RFdudeSmithChartV2.exe"
Name: "{autodesktop}\RFdude Smith Chart V2"; Filename: "{app}\bin\RFdudeSmithChartV2.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\RFdudeSmithChartV2.exe"; Description: "Launch RFdude Smith Chart V2"; Flags: postinstall nowait skipifsilent
