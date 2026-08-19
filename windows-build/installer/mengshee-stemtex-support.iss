#define AppName "Mengshee StemTeX Support"
#define AppIconFile AddBackslash(SourcePath) + "..\..\icons\mengshee.ico"
#define MengsheeVersionFile AddBackslash(SourcePath) + "..\..\VERSION.txt"
#define MengsheeVersionHandle FileOpen(MengsheeVersionFile)
#define MengsheeVersionFromFile Trim(FileRead(MengsheeVersionHandle))
#expr FileClose(MengsheeVersionHandle)
#define AppVersion GetEnv("MENGSHEE_VERSION")
#if AppVersion == ""
#define AppVersion MengsheeVersionFromFile
#endif
#define FileVersion GetEnv("MENGSHEE_FILE_VERSION")
#if FileVersion == ""
#define FileVersion MengsheeVersionFromFile
#endif
#define SourceDir GetEnv("MENGSHEE_SUPPORT_STAGE")
#if SourceDir == ""
#define SourceDir "..\..\..\dist\mengshee-stemtex-support\app"
#endif
#define OutputDir GetEnv("MENGSHEE_OUTPUT")
#if OutputDir == ""
#define OutputDir "..\..\..\dist"
#endif

[Setup]
AppId={{71E9EF5B-AE3D-48E0-A524-F11BC3FB29B7}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Yu Zhai
VersionInfoVersion={#FileVersion}
VersionInfoProductVersion={#FileVersion}
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} Setup
VersionInfoOriginalFileName=Mengshee-{#AppVersion}-StemTeX-Support.exe
DefaultDirName={autopf}\Mengshee
DefaultGroupName=Mengshee
DisableDirPage=no
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=Mengshee-{#AppVersion}-StemTeX-Support
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile={#AppIconFile}
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
UninstallDisplayIcon={app}\bin\mengshee.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
