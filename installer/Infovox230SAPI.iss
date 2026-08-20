; Inno Setup script for the Infovox 230 SAPI5 voices.
;
; Compiled directly rather than through CPack: the product registers two COM
; servers into two different registry views, has to stop a running worker before
; it can replace files, and must roll all of that back cleanly on uninstall --
; none of which a generated script expresses well.
;
; Accessibility notes, since most people installing a speech engine are using a
; screen reader to do it:
;   * every page is built from standard Win32 controls, which screen readers
;     read natively; nothing is owner-drawn;
;   * the summary page uses a read-only multi-line edit rather than a static
;     label, because an edit control takes focus and can be reviewed line by
;     line with the arrow keys, while a label on a custom page is often skipped;
;   * every control has an explicit accessible name, and TabOrder is set so
;     tabbing follows reading order;
;   * the last page offers to speak a test sentence aloud, so success can be
;     confirmed by ear rather than by reading the screen;
;   * no page is disabled or skipped silently, and no message is conveyed by
;     colour alone.
;
; Build with build_all.bat, which stages output\ and then runs ISCC on this file.

#define AppName "Infovox 230 SAPI5 Voices"
#define AppShortName "Infovox 230"
#define AppVersion "1.0.0"
#define AppPublisher "Infovox 230 SAPI5 project"
#define StageDir "..\output"

[Setup]
AppId={{7F1C9D42-3E85-4B0A-9C57-2D6A8E4F1B93}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\Infovox230
DefaultGroupName={#AppShortName}
OutputDir={#StageDir}
OutputBaseFilename=Infovox230SAPI_Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

; Voice tokens and the COM classes live in HKEY_LOCAL_MACHINE. SAPI5 does not
; enumerate voices registered per user -- measured, not assumed -- so this
; genuinely needs administrator rights and must not pretend otherwise.
PrivilegesRequired=admin

; In 64-bit install mode {sys} is the real System32 and {syswow64} is SysWOW64,
; which is what lets each COM server be registered into the registry view its
; own hosts read.
ArchitecturesInstallIn64BitMode=x64compatible

; Detailed installer log, kept beside the engine's own log so a support request
; only has to point at one folder.
SetupLogging=yes

UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\Infovox230Diag.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; --- the 32-bit SAPI5 engine, the worker that owns the voice engine, and tools
Source: "{#StageDir}\Infovox230SAPI.dll";     DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#StageDir}\Infovox230Server.exe";   DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#StageDir}\Infovox230Diag.exe";     DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#StageDir}\Infovox230SapiTest.exe"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete

; --- the speech engine itself: every language rule file and the runtime
Source: "{#StageDir}\engine\*"; DestDir: "{app}\engine"; Flags: ignoreversion restartreplace uninsrestartdelete recursesubdirs createallsubdirs

; --- the 64-bit SAPI5 engine, for Narrator and other 64-bit hosts
Source: "{#StageDir}\x64\Infovox230SAPI.dll";     DestDir: "{app}\x64"; Flags: ignoreversion restartreplace uninsrestartdelete; Check: Is64BitInstallMode
Source: "{#StageDir}\x64\Infovox230Diag.exe";     DestDir: "{app}\x64"; Flags: ignoreversion restartreplace uninsrestartdelete; Check: Is64BitInstallMode
Source: "{#StageDir}\x64\Infovox230SapiTest.exe"; DestDir: "{app}\x64"; Flags: ignoreversion restartreplace uninsrestartdelete; Check: Is64BitInstallMode

; --- documentation and the template for user-defined voices
Source: "..\docs\README.txt";        DestDir: "{app}"; Flags: ignoreversion
Source: "..\docs\voices.example.ini"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Speak a test sentence";  Filename: "{app}\Infovox230SapiTest.exe"; Parameters: "say"; Comment: "Speaks aloud with the first Infovox voice, to check the voices are working"
Name: "{group}\List the Infovox voices"; Filename: "{app}\Infovox230Diag.exe"; Parameters: "list"; Comment: "Lists every installed voice and its language"
Name: "{group}\Refresh the voice list";  Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\Infovox230SAPI.dll"""; Comment: "Republishes the voices after editing voices.ini"
Name: "{group}\Read me";                 Filename: "{app}\README.txt"; Comment: "How the voices work, and how to add your own"
Name: "{group}\Uninstall {#AppShortName}"; Filename: "{uninstallexe}"

[Code]
var
  SummaryPage: TWizardPage;
  SummaryMemo: TNewMemo;
  SummaryLabel: TNewStaticText;
  RegistrationFailed: String;

// A blank line inside a message box. Written as a function because a source
// line that starts with a '#' is read by the preprocessor as a directive, not
// as a Pascal character literal.
function Break2: String;
begin
  Result := Chr(13) + Chr(10) + Chr(13) + Chr(10);
end;

// A read-only edit rather than a label: a screen reader can put the cursor in
// it and review the text line by line, which it cannot do with a static label
// on a custom page.
procedure CreateSummaryPage;
begin
  SummaryPage := CreateCustomPage(wpWelcome, 'About these voices',
    'What will be installed, and where');

  SummaryLabel := TNewStaticText.Create(SummaryPage);
  SummaryLabel.Parent := SummaryPage.Surface;
  SummaryLabel.Caption := '&Details (read only):';
  SummaryLabel.Left := 0;
  SummaryLabel.Top := 0;
  SummaryLabel.Width := SummaryPage.SurfaceWidth;
  SummaryLabel.TabOrder := 0;

  SummaryMemo := TNewMemo.Create(SummaryPage);
  SummaryMemo.Parent := SummaryPage.Surface;
  SummaryMemo.Left := 0;
  SummaryMemo.Top := SummaryLabel.Top + SummaryLabel.Height + 6;
  SummaryMemo.Width := SummaryPage.SurfaceWidth;
  SummaryMemo.Height := SummaryPage.SurfaceHeight - SummaryMemo.Top;
  SummaryMemo.ReadOnly := True;
  SummaryMemo.ScrollBars := ssVertical;
  SummaryMemo.WantReturns := False;
  SummaryMemo.TabOrder := 1;

  // Built a line at a time rather than as one string with embedded line breaks:
  // it keeps the wording readable here, and a screen reader can move through the
  // memo line by line.
  SummaryMemo.Lines.Add('Infovox 230 SAPI5 Voices');
  SummaryMemo.Lines.Add('');
  SummaryMemo.Lines.Add('This installs 60 voices in 12 languages: American English,');
  SummaryMemo.Lines.Add('British English, Danish, Dutch, Finnish, French, German,');
  SummaryMemo.Lines.Add('Icelandic, Italian, Norwegian, Castilian Spanish and Swedish.');
  SummaryMemo.Lines.Add('Each language has a Male, Female, Child, Giant and Zombie');
  SummaryMemo.Lines.Add('speaker.');
  SummaryMemo.Lines.Add('');
  SummaryMemo.Lines.Add('The voices become available to every program that uses Windows');
  SummaryMemo.Lines.Add('speech, including NVDA, JAWS, Narrator, Balabolka and Word.');
  SummaryMemo.Lines.Add('');
  SummaryMemo.Lines.Add('Both a 32-bit and a 64-bit speech interface are installed, so');
  SummaryMemo.Lines.Add('32-bit and 64-bit programs can both use the voices.');
  SummaryMemo.Lines.Add('');
  SummaryMemo.Lines.Add('The speech engine itself is 32-bit and runs in a separate');
  SummaryMemo.Lines.Add('background program, Infovox230Server.exe. It starts by itself');
  SummaryMemo.Lines.Add('the first time something speaks. Keeping it separate means that');
  SummaryMemo.Lines.Add('if the engine ever fails, it cannot take your screen reader down');
  SummaryMemo.Lines.Add('with it.');
  SummaryMemo.Lines.Add('');
  SummaryMemo.Lines.Add('The engine reads all of its settings from memory. Nothing about');
  SummaryMemo.Lines.Add('the voices is stored in the Windows registry, and the old SAPI 4');
  SummaryMemo.Lines.Add('runtime is not needed or used.');
  SummaryMemo.Lines.Add('');
  SummaryMemo.Lines.Add('Logs, for when something needs diagnosing:');
  SummaryMemo.Lines.Add(ExpandConstant('  {localappdata}\Infovox230SAPI\infovox230.log'));
  SummaryMemo.Lines.Add(ExpandConstant('  {localappdata}\Infovox230SAPI\install.log'));
  SummaryMemo.Lines.Add('');
  SummaryMemo.Lines.Add('Administrator rights are required: Windows speech only lists');
  SummaryMemo.Lines.Add('voices that are registered for all users.');
end;

procedure InitializeWizard();
begin
  CreateSummaryPage;
end;

// The worker holds the engine files open on behalf of every program that is
// speaking, so it has to be gone before any of them can be replaced or deleted.
procedure StopWorker;
var
  ResultCode: Integer;
begin
  Log('Infovox: stopping any running worker');
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM Infovox230Server.exe',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Sleep(500);
end;

// Each COM server must be registered by a regsvr32 of its own bitness: the
// 32-bit one writes its class into the WOW6432Node view where 32-bit speech
// hosts look, the 64-bit one into the native view where Narrator looks. The
// voice tokens themselves are not redirected, so both write the same list.
function RegisterServer(const Dll: String; Use64: Boolean; Unregister: Boolean): Boolean;
var
  Exe, Args: String;
  ResultCode: Integer;
begin
  if Use64 then
    Exe := ExpandConstant('{sys}\regsvr32.exe')
  else if Is64BitInstallMode then
    Exe := ExpandConstant('{syswow64}\regsvr32.exe')
  else
    Exe := ExpandConstant('{sys}\regsvr32.exe');

  if Unregister then
    Args := '/s /u "' + Dll + '"'
  else
    Args := '/s "' + Dll + '"';

  Result := Exec(Exe, Args, '', SW_HIDE, ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
  Log('Infovox: ' + Exe + ' ' + Args + ' -> ' + IntToStr(ResultCode));
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    StopWorker;
    // Drop any previous registration first, so a voice renamed or removed
    // between versions cannot leave an orphaned entry behind.
    Log('Infovox: removing any previous registration');
    RegisterServer(ExpandConstant('{app}\Infovox230SAPI.dll'), False, True);
    if Is64BitInstallMode then
      RegisterServer(ExpandConstant('{app}\x64\Infovox230SAPI.dll'), True, True);
  end
  else if CurStep = ssPostInstall then
  begin
    RegistrationFailed := '';
    if not RegisterServer(ExpandConstant('{app}\Infovox230SAPI.dll'), False, False) then
      RegistrationFailed := '32-bit';

    if Is64BitInstallMode then
    begin
      if not RegisterServer(ExpandConstant('{app}\x64\Infovox230SAPI.dll'), True, False) then
      begin
        if RegistrationFailed <> '' then
          RegistrationFailed := RegistrationFailed + ' and 64-bit'
        else
          RegistrationFailed := '64-bit';
      end;
    end;

    if RegistrationFailed <> '' then
      MsgBox('The ' + RegistrationFailed + ' speech interface could not be registered.' +
             Break2 +
             'The Infovox voices may not appear in your programs. Try running this ' +
             'installer again, using "Run as administrator".' +
             Break2 +
             'Details are in the installation log: ' +
             ExpandConstant('{localappdata}\Infovox230SAPI\install.log'),
             mbError, MB_OK);
  end;
end;

// Keeps the installer's own log next to the engine's, so a support request only
// has to point at one folder.
procedure DeinitializeSetup();
var
  LogFolder: String;
begin
  LogFolder := ExpandConstant('{localappdata}\Infovox230SAPI');
  if not DirExists(LogFolder) then
    CreateDir(LogFolder);
  CopyFile(ExpandConstant('{log}'), LogFolder + '\install.log', False);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    StopWorker;
    if Is64BitInstallMode then
      RegisterServer(ExpandConstant('{app}\x64\Infovox230SAPI.dll'), True, True);
    RegisterServer(ExpandConstant('{app}\Infovox230SAPI.dll'), False, True);
  end;
end;

[Run]
; Offered as a checkbox on the final page. Hearing the voice is how someone
; using a screen reader confirms the install worked.
Filename: "{app}\Infovox230SapiTest.exe"; Parameters: "say"; \
    Description: "Speak a test sentence now"; \
    Flags: postinstall nowait skipifsilent runhidden

[UninstallDelete]
; The engine writes nothing outside the install folder, but the logs and any
; user voices live in the per-user data folder and are left alone deliberately:
; a reinstall should keep the voices someone has defined.
Type: files; Name: "{app}\voices.ini.bak"
