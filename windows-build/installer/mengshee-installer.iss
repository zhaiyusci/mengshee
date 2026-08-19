#define AppName "Mengshee"
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
#define SourceDir GetEnv("MENGSHEE_STAGE")
#if SourceDir == ""
#define SourceDir "..\..\..\dist\mengshee-pdf\app"
#endif
#define OutputDir GetEnv("MENGSHEE_OUTPUT")
#if OutputDir == ""
#define OutputDir "..\..\..\dist"
#endif
#define StemTeXSupportUrl GetEnv("MENGSHEE_STEMTEX_SUPPORT_URL")
#if StemTeXSupportUrl == ""
#define StemTeXSupportUrl "https://github.com/zhaiyusci/mengshee/releases/download/v" + AppVersion + "/Mengshee-" + AppVersion + "-StemTeX-Support.exe"
#endif

[Setup]
AppId={{06A28C09-9BB5-47D0-8F43-24BC9019C8E4}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Yu Zhai
VersionInfoVersion={#FileVersion}
VersionInfoProductVersion={#FileVersion}
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} Setup
VersionInfoOriginalFileName=Mengshee-{#AppVersion}-Setup.exe
DefaultDirName={autopf}\Mengshee
DefaultGroupName=Mengshee
DisableDirPage=no
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=Mengshee-{#AppVersion}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile={#AppIconFile}
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
UninstallDisplayIcon={app}\bin\mengshee.exe
ChangesAssociations=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "associatepdf"; Description: "Associate .pdf files with Mengshee"; GroupDescription: "File associations:"
Name: "stemtexsupport"; Description: "Install bundled StemTeX TeX tree support package"; GroupDescription: "Optional downloads:"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Mengshee"; Filename: "{app}\bin\mengshee.exe"; IconFilename: "{app}\bin\mengshee.ico"
Name: "{autodesktop}\Mengshee"; Filename: "{app}\bin\mengshee.exe"; IconFilename: "{app}\bin\mengshee.ico"; Tasks: desktopicon

[Registry]
Root: HKCR; Subkey: ".pdf"; ValueType: string; ValueName: ""; ValueData: "Mengshee.Document"; Flags: uninsdeletevalue; Tasks: associatepdf
Root: HKCR; Subkey: "Mengshee.Document"; ValueType: string; ValueName: ""; ValueData: "PDF Document"; Flags: uninsdeletekey; Tasks: associatepdf
Root: HKCR; Subkey: "Mengshee.Document\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\bin\application-pdf.ico"; Tasks: associatepdf
Root: HKCR; Subkey: "Mengshee.Document\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\mengshee.exe"" ""%1"""; Tasks: associatepdf

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ Runtime..."; Flags: waituntilterminated runhidden; Check: NeedsMsvcRuntime
Filename: "{tmp}\Mengshee-StemTeX-Support.exe"; Parameters: "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /DIR=""{app}"""; StatusMsg: "Installing StemTeX support package..."; Flags: waituntilterminated runhidden; Check: ShouldInstallStemTeXSupport
Filename: "{app}\bin\mengshee.exe"; Description: "Launch Mengshee"; Flags: nowait postinstall skipifsilent

[Code]
var
  DownloadPage: TDownloadWizardPage;

function NeedsMsvcRuntime: Boolean;
var
  Installed: Cardinal;
begin
  Result := not (RegQueryDWordValue(HKLM64, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Installed', Installed) and (Installed = 1));
end;

function ShouldInstallStemTeXSupport: Boolean;
begin
  Result := WizardIsTaskSelected('stemtexsupport');
end;

procedure InitializeWizard;
begin
  DownloadPage := CreateDownloadPage(SetupMessage(msgWizardPreparing), SetupMessage(msgPreparingDesc), nil);
  DownloadPage.ShowBaseNameInsteadOfUrl := True;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Error: String;
begin
  Result := True;
  if (CurPageID = wpReady) and (NeedsMsvcRuntime or ShouldInstallStemTeXSupport) then begin
    DownloadPage.Clear;
    if NeedsMsvcRuntime then
      DownloadPage.Add('https://aka.ms/vs/17/release/vc_redist.x64.exe', 'vc_redist.x64.exe', '');
    if ShouldInstallStemTeXSupport then
      DownloadPage.Add('{#StemTeXSupportUrl}', 'Mengshee-StemTeX-Support.exe', '');
    DownloadPage.Show;
    try
      try
        DownloadPage.Download;
      except
        if DownloadPage.AbortedByUser then
          Log('MSVC runtime download was aborted by user.')
        else begin
          Error := Format('%s: %s', [DownloadPage.LastBaseNameOrUrl, GetExceptionMessage]);
          SuppressibleMsgBox(AddPeriod(Error), mbCriticalError, MB_OK, IDOK);
        end;
        Result := False;
      end;
    finally
      DownloadPage.Hide;
    end;
  end;
end;
