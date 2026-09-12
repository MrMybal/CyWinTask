#define AppName "CyWinTask"
#ifndef AppVersion
  #define AppVersion "0.5.3"
#endif
#ifndef PublishDir
  #error PublishDir must point to the self-contained publish directory.
#endif
#ifndef ReleaseDir
  #error ReleaseDir must point to the release output directory.
#endif

[Setup]
AppId={{253A4C7D-4DC2-41B1-A02B-F4234F61C73D}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=CyWinTask
AppPublisherURL=https://github.com/MrMybal/CyWinTask
DefaultDirName={localappdata}\Programs\CyWinTask
DefaultGroupName=CyWinTask
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile={#PublishDir}\LICENSE.txt
OutputDir={#ReleaseDir}
OutputBaseFilename=CyWinTask-{#AppVersion}-win-x64-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=..\Assets\cywintask.ico
UninstallDisplayIcon={app}\CyWinTask.exe
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Créer un raccourci sur le bureau"; GroupDescription: "Raccourcis :"; Flags: unchecked

[Files]
Source: "{#PublishDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\CyWinTask"; Filename: "{app}\CyWinTask.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\CyWinTask"; Filename: "{app}\CyWinTask.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\CyWinTask.exe"; Description: "Lancer CyWinTask"; Flags: nowait postinstall skipifsilent
