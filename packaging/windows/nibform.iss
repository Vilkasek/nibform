; Windows installer for Nibform (Inno Setup 6). The release workflow builds it:
;   iscc /DAppVersion=0.5.0 /DSourceDir=<deployed app folder> packaging\windows\nibform.iss
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{8C1B6F4E-2F7A-4E35-9C55-6A0D2B9E4F17}
AppName=Nibform
AppVersion={#AppVersion}
AppPublisher=Nibform
AppPublisherURL=https://github.com/Vilkasek/nibform
DefaultDirName={autopf}\Nibform
DefaultGroupName=Nibform
UninstallDisplayIcon={app}\nibform.exe
LicenseFile={#SourceDir}\LICENSE.txt
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=dialog
ChangesAssociations=yes
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "pl"; MessagesFile: "compiler:Languages\Polish.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Nibform"; Filename: "{app}\nibform.exe"
Name: "{group}\{cm:UninstallProgram,Nibform}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Nibform"; Filename: "{app}\nibform.exe"; Tasks: desktopicon

[Registry]
; .nibform projects open in Nibform.
Root: HKA; Subkey: "Software\Classes\.nibform"; ValueType: string; ValueName: ""; ValueData: "Nibform.Project"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\Nibform.Project"; ValueType: string; ValueName: ""; ValueData: "Nibform project"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Nibform.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\nibform.exe,0"
Root: HKA; Subkey: "Software\Classes\Nibform.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\nibform.exe"" ""%1"""

[Run]
Filename: "{app}\nibform.exe"; Description: "{cm:LaunchProgram,Nibform}"; Flags: nowait postinstall skipifsilent
