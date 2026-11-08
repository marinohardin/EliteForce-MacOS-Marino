; Inno Setup script for Elite Force (Marino's Build), Windows x64.
;   iscc /DAppVersion=1.4.0 /DSourceDir=dist\EliteForce-Marino-1.4.0-windows-x64 windows\installer.iss
; windows/make-release.sh runs it when iscc is available. Per-user install by default (no admin).
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #error SourceDir must point at the staged dist\EliteForce-Marino-<ver>-windows-x64 folder
#endif

[Setup]
AppId={{6F0B7B3E-5D0C-4C57-9B0E-EF5A0C2B7A41}
AppName=Elite Force (Marino's Build)
AppVersion={#AppVersion}
AppPublisher=Marino Hardin
AppPublisherURL=https://github.com/marinohardin/EliteForce-MacOS-Marino
DefaultDirName={autopf}\EliteForce-Marino
DefaultGroupName=Elite Force
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=EliteForce-Marino-{#AppVersion}-windows-x64-setup
SetupIconFile={#SourcePath}\icon.ico
UninstallDisplayIcon={app}\EliteForce.exe
UninstallDisplayName=Elite Force (Marino's Build)
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile={#SourceDir}\licenses\LICENSE

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Elite Force\Elite Force (Single Player)"; Filename: "{app}\EliteForce.exe"; WorkingDir: "{app}"
Name: "{autoprograms}\Elite Force\Elite Force Holomatch"; Filename: "{app}\EliteForce.exe"; Parameters: "+set com_holomatch 1"; WorkingDir: "{app}"
Name: "{autoprograms}\Elite Force\Elite Force Dedicated Server"; Filename: "{app}\EliteForce.exe"; Parameters: "+set com_holomatch 1 +set dedicated 2"; WorkingDir: "{app}"
Name: "{autoprograms}\Elite Force\Read Me"; Filename: "{app}\Read Me.txt"
Name: "{autoprograms}\Elite Force\Uninstall Elite Force"; Filename: "{uninstallexe}"

[Registry]
; stvef:// links open a Holomatch server (HKCU: no admin needed)
Root: HKCU; Subkey: "Software\Classes\stvef"; ValueType: string; ValueName: ""; ValueData: "URL:Elite Force Holomatch"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\stvef"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\stvef\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\EliteForce.exe"",0"
Root: HKCU; Subkey: "Software\Classes\stvef\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\EliteForce.exe"" +set com_holomatch 1 --uri ""%1"""

[Run]
Filename: "{app}\EliteForce.exe"; Description: "Start Elite Force"; Flags: nowait postinstall skipifsilent unchecked
