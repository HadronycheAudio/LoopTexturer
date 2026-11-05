; The Windows installer, built with Inno Setup. CI passes the version:
;   ISCC /DAppVersion=1.0.0 installer\LoopTexturer.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
; Identifies the product for upgrades and uninstalling, so it must never change.
AppId={{92682BB4-DCCE-4E24-9877-CBAA11B11B1C}
AppName=LoopTexturer
AppVersion={#AppVersion}
AppVerName=LoopTexturer {#AppVersion}
AppPublisher=Hadronyche
AppPublisherURL=https://github.com/HadronycheAudio/LoopTexturer
AppCopyright=Copyright (c) 2026 Hadronyche
LicenseFile=..\LICENSE
MinVersion=10.0

; The standard location for VST 3 plug-ins, which hosts scan by default, so there's nothing to choose.
DefaultDirName={commoncf64}\VST3
DirExistsWarning=no
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; Keeps the uninstaller out of the folder that hosts scan.
UninstallFilesDir={commonpf64}\Hadronyche\LoopTexturer
UninstallDisplayName=LoopTexturer

OutputDir=..\build\installer
OutputBaseFilename=LoopTexturer-{#AppVersion}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Dirs]
; Makes Windows show the bundle's icon, which desktop.ini sets.
Name: "{app}\LoopTexturer.vst3"; Attribs: system

[Files]
Source: "..\build\VST3\Release\LoopTexturer.vst3\*"; DestDir: "{app}\LoopTexturer.vst3"; Flags: recursesubdirs ignoreversion
Source: "..\LICENSE"; DestDir: "{app}\LoopTexturer.vst3\Contents\Resources\Licenses"; DestName: "LoopTexturer-LICENSE.txt"; Flags: ignoreversion
Source: "..\external\vst3sdk\LICENSE.txt"; DestDir: "{app}\LoopTexturer.vst3\Contents\Resources\Licenses"; DestName: "VST3-SDK-LICENSE.txt"; Flags: ignoreversion
Source: "..\external\vst3sdk\vstgui4\LICENSE"; DestDir: "{app}\LoopTexturer.vst3\Contents\Resources\Licenses"; DestName: "VSTGUI-LICENSE.txt"; Flags: ignoreversion
Source: "NOTICE.txt"; DestDir: "{app}\LoopTexturer.vst3\Contents\Resources\Licenses"; Flags: ignoreversion
