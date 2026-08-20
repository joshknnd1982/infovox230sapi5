; Inno Setup script for the BestSpeech SAPI5 voices.
;
; Compiled directly rather than through CPack: the engine has to register two COM
; servers into two different registry views, stop a running worker process first, and
; roll all of that back cleanly on uninstall, none of which the generated script
; expressed well.
;
; Build with tools\build_all.bat, which stages output\ and then invokes ISCC on this file.

#define AppName "BestSpeech SAPI5 Voices"
#define AppVersion "2.0.0"
#define AppPublisher "Gozaltech"
#define AppURL "http://gozaltech.org"
#define OutputDir "..\output"

[Setup]
AppId={{8B5E2A41-6C3D-4F17-9E28-1A7B4D9C5E30}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
DefaultDirName={autopf}\BestSpeech
DefaultGroupName=BestSpeech
DisableProgramGroupPage=yes
DisableDirPage=no
OutputDir={#OutputDir}
OutputBaseFilename=BestSpeechSAPI_Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

; Voice tokens and COM classes live in HKLM, so the installer needs elevation.
PrivilegesRequired=admin

; In 64-bit install mode {sys} is the real System32 and {syswow64} is SysWOW64, which is
; what lets each COM server be registered into the registry view its own hosts read.
ArchitecturesInstallIn64BitMode=x64compatible

; Every engine dll is 32-bit, so the package itself is architecture neutral and installs
; on 32-bit Windows too, just without the 64-bit bridge.
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\BestspeechSAPI.dll

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; --- 32-bit SAPI engine, the worker process, and the engine shim ---
Source: "..\output\BestspeechSAPI.dll";   DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "..\output\BestspeechServer.exe"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "..\output\b32_wrapper.dll";      DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "..\output\b32_helper.exe";       DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "..\output\BestSpeechDiagnostics.exe"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete

; --- the speech engines themselves ---
Source: "..\output\b32_tts.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "..\output\dll_*.dll";   DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete

; --- 64-bit SAPI engine, for Narrator and other 64-bit hosts ---
Source: "..\output\x64\BestspeechSAPI.dll"; DestDir: "{app}\x64"; \
    Flags: ignoreversion restartreplace uninsrestartdelete; Check: Is64BitInstallMode
Source: "..\output\x64\BestSpeechDiagnostics.exe"; DestDir: "{app}\x64"; \
    Flags: ignoreversion restartreplace uninsrestartdelete; Check: Is64BitInstallMode

[Icons]
Name: "{group}\Check BestSpeech voices"; Filename: "{app}\BestSpeechDiagnostics.exe"
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"

[Code]
var
  DonatePage: TWizardPage;
  DonateLabel: TNewStaticText;
  DonateLabel2: TNewStaticText;
  PayPalButton: TNewButton;

procedure PayPalButtonClick(Sender: TObject);
var
  ErrorCode: Integer;
begin
  ShellExec('open', 'https://paypal.me/gozaltech', '', '', SW_SHOWNORMAL, ewNoWait, ErrorCode);
end;

procedure CreateDonatePage;
begin
  DonatePage := CreateCustomPage(wpWelcome, 'Support Development', 'Help improve BestSpeech SAPI');

  DonateLabel := TNewStaticText.Create(DonatePage);
  DonateLabel.Parent := DonatePage.Surface;
  DonateLabel.Caption := 'Thank you for installing BestSpeech SAPI!';
  DonateLabel.Left := 0;
  DonateLabel.Top := 0;
  DonateLabel.Width := DonatePage.SurfaceWidth;
  DonateLabel.Height := 40;
  DonateLabel.Font.Size := 12;
  DonateLabel.Font.Style := [fsBold];

  DonateLabel2 := TNewStaticText.Create(DonatePage);
  DonateLabel2.Parent := DonatePage.Surface;
  DonateLabel2.Caption :=
    'This software is provided free of charge. If you find it useful, please consider ' +
    'supporting development with a donation.' + #13#10 + #13#10 +
    'Your support helps us continue improving this project.';
  DonateLabel2.Left := 0;
  DonateLabel2.Top := 50;
  DonateLabel2.Width := DonatePage.SurfaceWidth;
  DonateLabel2.Height := 80;
  DonateLabel2.AutoSize := False;
  DonateLabel2.WordWrap := True;

  PayPalButton := TNewButton.Create(DonatePage);
  PayPalButton.Parent := DonatePage.Surface;
  PayPalButton.Caption := 'Donate via PayPal';
  PayPalButton.Left := (DonatePage.SurfaceWidth - 150) div 2;
  PayPalButton.Top := 140;
  PayPalButton.Width := 150;
  PayPalButton.Height := 30;
  PayPalButton.OnClick := @PayPalButtonClick;
end;

procedure InitializeWizard();
begin
  CreateDonatePage;
end;

// The 32-bit worker keeps the engine dlls open on behalf of 64-bit hosts, so it has to
// be gone before any of them can be replaced or deleted.
procedure StopWorker;
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM BestspeechServer.exe',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Sleep(500);
end;

// Each COM server must be registered by a regsvr32 of its own bitness: the 32-bit one
// writes the voice tokens under WOW6432Node where 32-bit SAPI hosts look, and the
// 64-bit one writes them to the native view where Narrator and other 64-bit hosts look.
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
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Failed: String;
begin
  if CurStep = ssInstall then
  begin
    StopWorker;
    // Drop any previous registration first so a rename or removal of a voice between
    // versions cannot leave an orphaned token pointing at this engine.
    RegisterServer(ExpandConstant('{app}\BestspeechSAPI.dll'), False, True);
    if Is64BitInstallMode then
      RegisterServer(ExpandConstant('{app}\x64\BestspeechSAPI.dll'), True, True);
  end
  else if CurStep = ssPostInstall then
  begin
    Failed := '';
    if not RegisterServer(ExpandConstant('{app}\BestspeechSAPI.dll'), False, False) then
      Failed := '32-bit';

    if Is64BitInstallMode then
    begin
      if not RegisterServer(ExpandConstant('{app}\x64\BestspeechSAPI.dll'), True, False) then
      begin
        if Failed <> '' then
          Failed := Failed + ' and 64-bit'
        else
          Failed := '64-bit';
      end;
    end;

    if Failed <> '' then
      MsgBox('The ' + Failed + ' speech engine could not be registered. ' +
             'The BestSpeech voices may not appear in your applications.' + #13#10 + #13#10 +
             'Try running the installer again as an administrator.', mbError, MB_OK);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    StopWorker;
    if Is64BitInstallMode then
      RegisterServer(ExpandConstant('{app}\x64\BestspeechSAPI.dll'), True, True);
    RegisterServer(ExpandConstant('{app}\BestspeechSAPI.dll'), False, True);
  end;
end;
