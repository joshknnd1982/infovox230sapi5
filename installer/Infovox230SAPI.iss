; Inno Setup script for the Infovox 230 SAPI5 voices.
;
; Compiled directly rather than through CPack: the product registers two COM
; servers into two different registry views, has to stop a running worker before
; it can replace files, and must roll all of that back cleanly on uninstall --
; none of which a generated script expresses well.
;
; What gets installed is chosen language by language and voice by voice on the
; components page. The list itself is generated from the engine's own mode table
; into voices.iss, which is included below; see tools/gen_installer_voices.py.
;
; Accessibility notes, since most people installing a speech engine are using a
; screen reader to do it:
;   * every page is built from the wizard's own controls, which Inno Setup
;     gives accessible names and roles; nothing is drawn by this script;
;   * the summary page uses a read-only multi-line edit rather than a static
;     label, because an edit control takes focus and can be reviewed line by
;     line with the arrow keys, while a label on a custom page is often skipped;
;   * every voice on the components page is named in full -- "Danish Female",
;     not "Female" -- because a screen reader reads the line the cursor is on
;     and not the branch above it;
;   * the "Ready to install" page repeats the choice as a count of voices and
;     languages, in a memo that can be reviewed the same way, so the selection
;     can be confirmed without going back through the tree;
;   * choosing no voices at all, or a language with no voices in it, is caught
;     and explained rather than silently installed;
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
#define AppVersion "1.1.0"
#define AppPublisher "Infovox 230 SAPI5 project"
#define StageDir "..\output"

[Setup]
AppId={{7F1C9D42-3E85-4B0A-9C57-2D6A8E4F1B93}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
; Stamped into the setup program itself, so which build someone is running can
; be read from its properties without starting it.
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
DefaultDirName={autopf}\Infovox230
DefaultGroupName={#AppShortName}
OutputDir={#StageDir}
OutputBaseFilename=Infovox230SAPI_Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

; The components page is the point of this installer, so it is never hidden --
; not even when the chosen setup type accounts for everything on it.
AlwaysShowComponentsList=yes
ShowComponentSizes=yes
; Someone re-running setup is usually there to add or drop a language, so the
; page opens on what they chose last time rather than on the default type.
UsePreviousSetupType=yes

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

[Types]
; Four ways in, from "give me everything" to "I will choose". Full is first and
; so is the default, because most people want all of it and 23 MB is not what
; it was in 1995.
Name: "full";    Description: "All 60 voices, in all 12 languages"
Name: "english"; Description: "The English voices only -- American and British, 10 voices"
Name: "minimal"; Description: "American English Male and Female only"
Name: "custom";  Description: "Choose the languages and voices myself"; Flags: iscustom

[Components]
; The program itself: both speech interfaces, the engine runtime, the worker,
; the configuration utility and the diagnostics. Not optional -- without it
; there is nothing for a voice to speak through.
Name: "core"; Description: "The speech interfaces, the engine and the configuration utility"; \
    Types: full english minimal custom; Flags: fixed

; The twelve languages and the sixty voices inside them are in voices.iss,
; included at the end of this file.

[Files]
; --- the 32-bit SAPI5 engine, the worker that owns the voice engine, and tools
Source: "{#StageDir}\Infovox230SAPI.dll";     DestDir: "{app}"; Components: core; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#StageDir}\Infovox230Server.exe";   DestDir: "{app}"; Components: core; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#StageDir}\Infovox230Diag.exe";     DestDir: "{app}"; Components: core; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#StageDir}\Infovox230SapiTest.exe"; DestDir: "{app}"; Components: core; Flags: ignoreversion restartreplace uninsrestartdelete

; --- the configuration utility: define your own voices, and set everything the
;     engine can be told, without editing a file by hand
Source: "{#StageDir}\Infovox230Config.exe";   DestDir: "{app}"; Components: core; Flags: ignoreversion restartreplace uninsrestartdelete

; --- the speech engine runtime. The language rule files are not here: each one
;     belongs to its own component and is listed in voices.iss.
Source: "{#StageDir}\engine\Ivx230nt.dll"; DestDir: "{app}\engine"; Components: core; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#StageDir}\engine\Sx32w.dll";    DestDir: "{app}\engine"; Components: core; Flags: ignoreversion restartreplace uninsrestartdelete

; --- the 64-bit SAPI5 engine, for Narrator and other 64-bit hosts
Source: "{#StageDir}\x64\Infovox230SAPI.dll";     DestDir: "{app}\x64"; Components: core; Flags: ignoreversion restartreplace uninsrestartdelete; Check: Is64BitInstallMode
Source: "{#StageDir}\x64\Infovox230Diag.exe";     DestDir: "{app}\x64"; Components: core; Flags: ignoreversion restartreplace uninsrestartdelete; Check: Is64BitInstallMode
Source: "{#StageDir}\x64\Infovox230SapiTest.exe"; DestDir: "{app}\x64"; Components: core; Flags: ignoreversion restartreplace uninsrestartdelete; Check: Is64BitInstallMode

; --- documentation and the template for user-defined voices
Source: "..\docs\README.txt";         DestDir: "{app}"; Components: core; Flags: ignoreversion
Source: "..\docs\voices.example.ini"; DestDir: "{app}"; Components: core; Flags: ignoreversion

; --- the comment header of installed.ini. The [INI] entries in voices.iss then
;     append the languages and voices that were chosen to it, so the file that
;     records the choice also explains itself to whoever opens it.
Source: "installed.header.ini"; DestDir: "{app}"; DestName: "installed.ini"; Components: core; Flags: ignoreversion

[InstallDelete]
; Before anything is copied, so that a re-run which drops a language leaves no
; trace of it. installed.ini is rebuilt from scratch every time for the same
; reason: it must say what is installed now, not what has ever been installed.
Type: files; Name: "{app}\installed.ini"

[Tasks]
; Offered rather than assumed, and checked to begin with: the configuration
; utility is where someone goes to make the voices their own, and a desktop icon
; is the shortest route to it for anyone driving Windows from the keyboard.
Name: "desktopicon"; \
    Description: "Put an &icon for the Infovox 230 configuration utility on the desktop"; \
    GroupDescription: "Additional icons:"

[Icons]
Name: "{group}\Infovox 230 Configuration"; Filename: "{app}\Infovox230Config.exe"; Comment: "Define your own voices and change every engine setting"
Name: "{autodesktop}\Infovox 230 Configuration"; Filename: "{app}\Infovox230Config.exe"; Comment: "Define your own voices and change every engine setting"; Tasks: desktopicon
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

// The components page hands its selection back as one comma-separated list of
// component names. Every voice is a child component and every language is a
// parent, so a name with a backslash in it is a voice and a name without one is
// a language -- which is all the counting below needs to know, and it lets that
// counting work without a second generated table to keep in step.
function SelectedComponentNames: TArrayOfString;
var
  Rest, Item: String;
  Cut, Count: Integer;
begin
  SetArrayLength(Result, 0);
  Rest := WizardSelectedComponents(False);
  while Rest <> '' do
  begin
    Cut := Pos(',', Rest);
    if Cut = 0 then
    begin
      Item := Trim(Rest);
      Rest := '';
    end
    else
    begin
      Item := Trim(Copy(Rest, 1, Cut - 1));
      Rest := Copy(Rest, Cut + 1, Length(Rest));
    end;
    if Item <> '' then
    begin
      Count := GetArrayLength(Result);
      SetArrayLength(Result, Count + 1);
      Result[Count] := Item;
    end;
  end;
end;

procedure CountSelection(var Languages: Integer; var Voices: Integer);
var
  Names: TArrayOfString;
  I: Integer;
begin
  Languages := 0;
  Voices := 0;
  Names := SelectedComponentNames;
  for I := 0 to GetArrayLength(Names) - 1 do
  begin
    if Pos('\', Names[I]) > 0 then
      Voices := Voices + 1
    else if CompareText(Names[I], 'core') <> 0 then
      Languages := Languages + 1;
  end;
end;

// Languages that are ticked with none of their voices ticked. Read off the
// components list itself rather than the name list, so the answer can be given
// back in the words on the page: an entry at the top level with children under
// it is a language, and the program's own entry has none.
function LanguagesWithoutVoices: String;
var
  I, J, Children, Chosen: Integer;
begin
  Result := '';
  for I := 0 to WizardForm.ComponentsList.Items.Count - 1 do
  begin
    if (WizardForm.ComponentsList.ItemLevel[I] <> 0) or
       (not WizardForm.ComponentsList.Checked[I]) then
      Continue;

    Children := 0;
    Chosen := 0;
    J := I + 1;
    while (J < WizardForm.ComponentsList.Items.Count) and
          (WizardForm.ComponentsList.ItemLevel[J] > 0) do
    begin
      Children := Children + 1;
      if WizardForm.ComponentsList.Checked[J] then
        Chosen := Chosen + 1;
      J := J + 1;
    end;

    if (Children > 0) and (Chosen = 0) then
    begin
      if Result <> '' then
        Result := Result + ', ';
      Result := Result + WizardForm.ComponentsList.ItemCaption[I];
    end;
  end;
end;

// A read-only edit rather than a label: a screen reader can put the cursor in
// it and review the text line by line, which it cannot do with a static label
// on a custom page.
procedure CreateSummaryPage;
begin
  SummaryPage := CreateCustomPage(wpWelcome, 'About these voices',
    'What can be installed, and where');

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
  SummaryMemo.Lines.Add('This offers 60 voices in 12 languages: American English,');
  SummaryMemo.Lines.Add('British English, Danish, Dutch, Finnish, French, German,');
  SummaryMemo.Lines.Add('Icelandic, Italian, Norwegian, Castilian Spanish and Swedish.');
  SummaryMemo.Lines.Add('Each language has a Male, Female, Child, Giant and Zombie');
  SummaryMemo.Lines.Add('speaker.');
  SummaryMemo.Lines.Add('');
  SummaryMemo.Lines.Add('You choose which of them to install. On the "Select');
  SummaryMemo.Lines.Add('components" page, further on, each of the twelve languages');
  SummaryMemo.Lines.Add('can be ticked or left out, and inside each language each of');
  SummaryMemo.Lines.Add('its five voices can be ticked or left out on its own. Only');
  SummaryMemo.Lines.Add('the voices you tick are put in the Windows voice list, and');
  SummaryMemo.Lines.Add('only the languages you tick take up any room: a language');
  SummaryMemo.Lines.Add('carries its own pronunciation rules, from 37 KB for Spanish');
  SummaryMemo.Lines.Add('to 6.7 MB for Danish, and all 12 come to about 23 MB.');
  SummaryMemo.Lines.Add('');
  SummaryMemo.Lines.Add('There are ready-made choices on that page too -- everything,');
  SummaryMemo.Lines.Add('the English voices only, or just American English Male and');
  SummaryMemo.Lines.Add('Female -- and you can start from one of those and adjust it.');
  SummaryMemo.Lines.Add('');
  SummaryMemo.Lines.Add('You can run this installer again later to add a language or');
  SummaryMemo.Lines.Add('drop one; it starts from what you chose last time, and');
  SummaryMemo.Lines.Add('anything you clear is removed from the disk and from the');
  SummaryMemo.Lines.Add('voice list.');
  SummaryMemo.Lines.Add('');
  SummaryMemo.Lines.Add('The voices become available to every program that uses Windows');
  SummaryMemo.Lines.Add('speech, including NVDA, JAWS, Narrator, Balabolka and Word.');
  SummaryMemo.Lines.Add('');
  SummaryMemo.Lines.Add('A configuration utility is installed with them. It is where you');
  SummaryMemo.Lines.Add('define voices of your own -- as many as 256 alongside the sixty --');
  SummaryMemo.Lines.Add('by setting the pitch, loudness, breathiness and vocal tract shape');
  SummaryMemo.Lines.Add('the engine builds a voice from, and where every other engine');
  SummaryMemo.Lines.Add('setting can be changed. It can speak a voice back to you before');
  SummaryMemo.Lines.Add('you keep it. Every control in it can be reached with the Tab key');
  SummaryMemo.Lines.Add('and is named for a screen reader.');
  SummaryMemo.Lines.Add('');
  SummaryMemo.Lines.Add('You will be offered a desktop icon for it on the next page.');
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

// Nothing to speak with is not a working installation, so it is refused rather
// than installed and then explained by silence. A language with no voices in it
// is legal -- it puts the rules on the disk and nothing in the voice list -- so
// it is queried instead of refused.
function NextButtonClick(CurPageID: Integer): Boolean;
var
  Languages, Voices: Integer;
  Silent: String;
begin
  Result := True;
  if CurPageID <> wpSelectComponents then
    Exit;

  CountSelection(Languages, Voices);
  if Voices = 0 then
  begin
    MsgBox('No voices are ticked, so nothing would be added to the Windows voice list.' +
           Break2 +
           'Tick a language to take all five of its voices, or open a language and tick ' +
           'the voices you want from it.',
           mbError, MB_OK);
    Result := False;
    Exit;
  end;

  Silent := LanguagesWithoutVoices;
  if Silent <> '' then
    Result := MsgBox('These languages are ticked with none of their voices ticked: ' +
                     Silent + '.' +
                     Break2 +
                     'Their pronunciation rules will be installed and will take up room, ' +
                     'but they will add no voices to the Windows voice list.' +
                     Break2 +
                     'Install them anyway?',
                     mbConfirmation, MB_YESNO) = IDYES;
end;

// The "Ready to install" page is the last chance to check the choice, and it is
// a memo, so it can be read line by line. The count goes at the top because the
// components list below it is sixty lines long when everything is ticked.
function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
var
  Languages, Voices: Integer;
begin
  CountSelection(Languages, Voices);

  Result := 'Voices:' + NewLine + Space;
  if Voices = 1 then
    Result := Result + '1 voice'
  else
    Result := Result + IntToStr(Voices) + ' voices';
  if Languages = 1 then
    Result := Result + ' in 1 language'
  else
    Result := Result + ' in ' + IntToStr(Languages) + ' languages';
  Result := Result + ', published to Windows speech.' + NewLine + NewLine;

  if MemoUserInfoInfo <> '' then
    Result := Result + MemoUserInfoInfo + NewLine + NewLine;
  if MemoDirInfo <> '' then
    Result := Result + MemoDirInfo + NewLine + NewLine;
  if MemoTypeInfo <> '' then
    Result := Result + MemoTypeInfo + NewLine + NewLine;
  if MemoComponentsInfo <> '' then
    Result := Result + MemoComponentsInfo + NewLine + NewLine;
  if MemoGroupInfo <> '' then
    Result := Result + MemoGroupInfo + NewLine + NewLine;
  if MemoTasksInfo <> '' then
    Result := Result + MemoTasksInfo + NewLine + NewLine;
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
  // The configuration utility holds its own file open while it is running, and
  // someone reinstalling is quite likely to have left it open.
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM Infovox230Config.exe',
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
    // between versions -- or a language cleared on the components page this
    // time round -- cannot leave an orphaned entry behind.
    Log('Infovox: removing any previous registration');
    RegisterServer(ExpandConstant('{app}\Infovox230SAPI.dll'), False, True);
    if Is64BitInstallMode then
      RegisterServer(ExpandConstant('{app}\x64\Infovox230SAPI.dll'), True, True);
  end
  else if CurStep = ssPostInstall then
  begin
    // Registering happens after installed.ini has been written, which is what
    // decides how many voices the interface publishes.
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

; Left unticked: it opens a window, and someone who only wanted the voices
; should not have one appear without asking.
Filename: "{app}\Infovox230Config.exe"; \
    Description: "Open the Infovox 230 configuration utility to make voices of my own"; \
    Flags: postinstall nowait skipifsilent unchecked

[UninstallDelete]
; The engine writes nothing outside the install folder, but the logs and any
; user voices live in the per-user data folder and are left alone deliberately:
; a reinstall should keep the voices someone has defined.
Type: files; Name: "{app}\voices.ini.bak"
; Written by the [INI] entries rather than copied, so it is not tracked as an
; installed file and has to be named here.
Type: files; Name: "{app}\installed.ini"

; The twelve languages, the sixty voices, the rule files each language needs,
; and the record of what was chosen. Generated from the engine's own mode table
; by tools/gen_installer_voices.py, so the installer and the engine cannot come
; to disagree about what a voice is called.
#include "voices.iss"
