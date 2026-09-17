#ifndef StageDir
  #define StageDir "..\dist\accent-messenger-sapi-0.4.0"
#endif
[Setup]
AppId={{B86EE667-B99D-4F51-8B2D-353477AF92E4}
AppName=Accent Messenger SAPI
AppVersion=0.4.0
AppPublisher=Tamas Geczy
DefaultDirName={autopf}\Accent Messenger SAPI
PrivilegesRequired=admin
MinVersion=6.1sp1
Compression=lzma2
SolidCompression=yes
OutputDir={#StageDir}\..
OutputBaseFilename=accent-messenger-sapi-0.4.0-setup
DisableProgramGroupPage=yes
UninstallDisplayName=Accent Messenger SAPI
[Files]
Source: "{#StageDir}\x86\messenger_sapi.dll"; DestDir: "{app}\x86"; Flags: regserver 32bit
Source: "{#StageDir}\x64\messenger_sapi.dll"; DestDir: "{app}\x64"; Flags: regserver 64bit; Check: IsWin64
Source: "{#StageDir}\messenger_settings.exe"; DestDir: "{app}"
Source: "{#StageDir}\data\SPKMIC.TSR"; DestDir: "{app}\data"
Source: "{#StageDir}\LICENSE"; DestDir: "{app}"
Source: "{#StageDir}\THIRD-PARTY-NOTICES.md"; DestDir: "{app}"
Source: "{#StageDir}\README.md"; DestDir: "{app}"
Source: "{#StageDir}\licenses\*"; DestDir: "{app}\licenses"
[Icons]
Name: "{commonprograms}\Accent Messenger settings"; Filename: "{app}\messenger_settings.exe"
[Dirs]
; Shared preferences only; executable code and speech data stay in Program Files.
Name: "{commonappdata}\Accent Messenger"; Permissions: users-modify; Flags: uninsneveruninstall
[Run]
Filename: "{app}\messenger_settings.exe"; Description: "Open Accent Messenger settings"; Flags: postinstall nowait skipifsilent
