// Infovox230Config -- the configuration utility.
//
// It exists so that defining a voice does not mean editing an ini file by hand.
// Everything the Infovox 230 engine can be told, per voice and for the engine
// as a whole, is here: the four numbers that shape a voice, the language, the
// names and data files, and the settings that govern trimming, position
// reporting, timeouts and how far the rate and pitch controls reach.
//
// Accessibility is the point of the design, not a coat of paint on it. Most
// people configuring a speech engine cannot see the screen while they do it, so:
//
//   * every control is a standard Win32 control on a dialog template, which
//     screen readers read natively; nothing is owner-drawn;
//   * every control can be reached with Tab, and every one of them is named by
//     the static text immediately before it in the template, which is how MSAA
//     works out what to announce;
//   * anything the utility wants to tell you appears in a read-only edit box
//     rather than a label, because an edit box takes focus and can be reviewed
//     line by line with the arrow keys;
//   * a voice can be heard before it is kept: Preview speaks the settings as
//     they stand, including ones not written to the file yet;
//   * nothing is conveyed by colour, position or shape alone.
//
// Where things are written: voices.ini, either beside the dll for everybody or
// in %LOCALAPPDATA%\Infovox230SAPI for one user. The utility edits it through
// the Windows profile API, so a file someone has commented by hand keeps its
// comments and anything in it this utility does not understand.

#include <windows.h>

#include <commctrl.h>
#include <mmsystem.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ivx_client.h"
#include "ivx_config_model.h"
#include "ivx_config_res.h"
#include "ivx_log.h"
#include "ivx_paths.h"
#include "ivx_protocol.h"
#include "ivx_settings.h"

// Version 6 of the common controls: the themed ones, and the ones every current
// screen reader is tested against.
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

using namespace ivx;
using namespace ivx::config;

// The preview thread has finished; wParam is 1 for success, 0 for failure, and
// the message to show is in g_preview_message.
const UINT WM_APP_PREVIEW_DONE = WM_APP + 1;

// The section a preview is spoken from. It is written to the personal
// voices.ini, spoken, and removed again, so a voice can be heard before it is
// kept -- and so previewing never disturbs the voice being edited. Anything
// left behind by a crash is cleared away at start-up.
const wchar_t kPreviewSection[] = L"Infovox 230 preview (temporary)";

HINSTANCE g_instance = nullptr;
VoiceFile g_file;
int g_log_level = 3;
bool g_publish_pending = false;  // voices have been saved but not yet published

// One preview at a time, so a single set of globals is enough. Written by the
// preview thread, read by the dialog after WM_APP_PREVIEW_DONE.
HANDLE g_preview_thread = nullptr;
volatile LONG g_preview_stop = 0;
std::wstring g_preview_message;
HWND g_preview_owner = nullptr;

// --- small helpers ---------------------------------------------------------

std::wstring text_of(HWND dlg, int id)
{
    HWND control = GetDlgItem(dlg, id);
    if (!control) {
        return std::wstring();
    }
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) {
        return std::wstring();
    }
    std::wstring out(static_cast<size_t>(length) + 1, L'\0');
    const int got = GetWindowTextW(control, &out[0], length + 1);
    out.resize(static_cast<size_t>(got < 0 ? 0 : got));
    return out;
}

std::wstring trim(const std::wstring& s)
{
    const size_t b = s.find_first_not_of(L" \t\r\n");
    if (b == std::wstring::npos) {
        return std::wstring();
    }
    const size_t e = s.find_last_not_of(L" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::wstring number(int value)
{
    wchar_t buf[32];
    _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%d", value);
    return buf;
}

int to_int(const std::wstring& text, int fallback)
{
    if (trim(text).empty()) {
        return fallback;
    }
    return static_cast<int>(wcstol(text.c_str(), nullptr, 10));
}

int clamp(int value, int lo, int hi)
{
    return value < lo ? lo : (value > hi ? hi : value);
}

void set_text(HWND dlg, int id, const std::wstring& text)
{
    SetDlgItemTextW(dlg, id, text.c_str());
}

void set_status(HWND dlg, const std::wstring& text)
{
    SetDlgItemTextW(dlg, IDC_STATUS, text.c_str());
    IVX_INFO("config: %S", text.c_str());
}

void say_problem(HWND owner, const std::wstring& text)
{
    MessageBoxW(owner, text.c_str(), L"Infovox 230 Configuration", MB_OK | MB_ICONEXCLAMATION);
}

std::wstring lines(const std::vector<std::wstring>& items)
{
    std::wstring out;
    for (const std::wstring& item : items) {
        out += item;
        out += L"\r\n";
    }
    return out;
}

bool file_exists(const std::wstring& path)
{
    return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// --- the log level, which lives in a file of its own -----------------------

std::wstring log_level_path()
{
    return user_data_dir() + L"\\loglevel.txt";
}

int read_log_level()
{
    const std::wstring path = log_level_path();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return 3;  // the built-in default
    }
    char buf[16] = {};
    DWORD got = 0;
    ReadFile(h, buf, sizeof(buf) - 1, &got, nullptr);
    CloseHandle(h);
    const int level = atoi(buf);
    return clamp(level, 0, 5);
}

bool write_log_level(int level)
{
    const std::wstring path = log_level_path();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    char text[8];
    _snprintf_s(text, sizeof(text), _TRUNCATE, "%d\r\n", clamp(level, 0, 5));
    DWORD written = 0;
    WriteFile(h, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
    CloseHandle(h);
    return true;
}

// --- the list of voices ----------------------------------------------------

struct ListRow {
    std::wstring name;
    bool in_file = false;     // the file being edited has a section for it
    bool builtin = false;     // one of the sixty the product ships with
    bool other_file = false;  // defined in the voices.ini that is not being edited
};

std::vector<ListRow> g_rows;

std::wstring row_label(const ListRow& row)
{
    if (row.in_file && row.builtin) {
        return row.name + L"  (built-in voice, changed here)";
    }
    if (row.in_file) {
        return row.name + L"  (your voice)";
    }
    if (row.other_file) {
        return row.name + L"  (defined in the other voices file)";
    }
    return row.name + L"  (built-in voice)";
}

// The voice under the cursor, or -1.
int selected_row(HWND dlg)
{
    const LRESULT index = SendDlgItemMessageW(dlg, IDC_VOICE_LIST, LB_GETCURSEL, 0, 0);
    if (index == LB_ERR || index < 0 || static_cast<size_t>(index) >= g_rows.size()) {
        return -1;
    }
    return static_cast<int>(index);
}

// The edit for a row: what the file says, or an empty one carrying the name.
VoiceEdit edit_for(const ListRow& row)
{
    const int index = g_file.find(row.name);
    if (index >= 0) {
        return g_file.entries()[static_cast<size_t>(index)];
    }
    VoiceEdit edit;
    edit.name = row.name;
    edit.builtin = row.builtin;
    return edit;
}

std::wstring effective(const VoiceEdit& edit, int key)
{
    return edit.has(key) ? edit.get(key) : inherited_value(edit, key);
}

void rebuild_list(HWND dlg, const std::wstring& select_name)
{
    g_rows.clear();

    for (const VoiceEdit& edit : g_file.entries()) {
        if (_wcsicmp(edit.name.c_str(), kPreviewSection) == 0) {
            continue;
        }
        ListRow row;
        row.name = edit.name;
        row.in_file = true;
        row.builtin = edit.builtin;
        g_rows.push_back(row);
    }

    for (const Voice& voice : catalog().voices()) {
        const std::wstring name = widen(voice.display_name);
        if (_wcsicmp(name.c_str(), kPreviewSection) == 0) {
            continue;
        }
        bool already = false;
        for (const ListRow& row : g_rows) {
            if (_wcsicmp(row.name.c_str(), name.c_str()) == 0) {
                already = true;
                break;
            }
        }
        if (already) {
            continue;
        }
        ListRow row;
        row.name = name;
        row.builtin = !voice.user_defined;
        row.other_file = voice.user_defined;
        g_rows.push_back(row);
    }

    HWND list = GetDlgItem(dlg, IDC_VOICE_LIST);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    int select = 0;
    for (size_t i = 0; i < g_rows.size(); ++i) {
        const std::wstring label = row_label(g_rows[i]);
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        if (!select_name.empty() && _wcsicmp(g_rows[i].name.c_str(), select_name.c_str()) == 0) {
            select = static_cast<int>(i);
        }
    }
    SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(select), 0);
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
}

std::wstring gender_word(const std::wstring& value)
{
    if (value == L"1") {
        return L"female";
    }
    if (value == L"2") {
        return L"male";
    }
    return L"not stated";
}

std::wstring language_name_for(const std::wstring& rule_file)
{
    for (const Language& language : languages()) {
        if (_wcsicmp(language.rule_file.c_str(), rule_file.c_str()) == 0) {
            return language.name;
        }
    }
    return L"unknown";
}

void update_details(HWND dlg)
{
    const int row = selected_row(dlg);
    if (row < 0) {
        set_text(dlg, IDC_DETAILS, L"No voice is selected.");
        return;
    }
    const ListRow& info = g_rows[static_cast<size_t>(row)];
    const VoiceEdit edit = edit_for(info);

    const std::wstring rule_file = effective(edit, KEY_LANGUAGE_FILE);
    const int pitch = to_int(effective(edit, KEY_PITCH), 50);

    std::vector<std::wstring> text;
    text.push_back(L"Name: " + info.name);
    if (info.in_file && info.builtin) {
        text.push_back(L"Kind: one of the built-in voices, changed by you");
    } else if (info.in_file) {
        text.push_back(L"Kind: a voice you have defined");
    } else if (info.other_file) {
        text.push_back(L"Kind: a voice defined in the other voices file");
    } else {
        text.push_back(L"Kind: one of the built-in voices, unchanged");
    }
    text.push_back(L"");
    text.push_back(L"Language: " + language_name_for(rule_file));
    text.push_back(L"Language rule file: " + rule_file);
    text.push_back(L"Language number: " + effective(edit, KEY_LANGUAGE_ID));
    text.push_back(L"Windows language code: " + effective(edit, KEY_LCID));
    text.push_back(L"");
    text.push_back(L"Pitch: " + number(pitch) + L", which is about " +
                   number(pitch_to_hertz(pitch)) + L" hertz");
    text.push_back(L"Loudness (Dynamic): " + effective(edit, KEY_DYNAMIC));
    text.push_back(L"Breathiness (Aspiration): " + effective(edit, KEY_ASPIRATION));
    text.push_back(L"Vocal tract shape: " +
                   std::wstring(formant_description(to_int(effective(edit, KEY_FORMANT_NO), 0))));
    text.push_back(L"Gender reported: " + gender_word(effective(edit, KEY_GENDER)));
    text.push_back(L"Age reported: " + effective(edit, KEY_AGE) + L" years");
    text.push_back(L"");
    if (edit.has(KEY_BASED_ON)) {
        text.push_back(L"Based on: " + edit.get(KEY_BASED_ON));
    }
    text.push_back(L"Speaker name given to the engine: " + effective(edit, KEY_SPEAKER_NAME));
    for (int key = KEY_SPEAKER_STYLE; key <= KEY_MAPPING_FILE; ++key) {
        const std::wstring value = effective(edit, key);
        if (!value.empty()) {
            text.push_back(std::wstring(voice_key_name(key)) + L": " + value);
        }
    }
    text.push_back(L"");
    if (info.in_file) {
        text.push_back(L"Written in: " + g_file.path());
    } else if (info.other_file) {
        text.push_back(L"Written in the other voices file. Changing it here will "
                       L"put your version in " + g_file.path() + L".");
    } else {
        text.push_back(L"Not in any file: these are the settings this voice was built with.");
    }

    set_text(dlg, IDC_DETAILS, lines(text));
}

// --- speaking a voice ------------------------------------------------------

// Writes the settings as they stand into a section of their own, so they can be
// heard without being kept. Returns false if the personal voices.ini cannot be
// written, which would be unusual -- it is under the user's own profile.
bool write_preview_section(const VoiceEdit& edit, std::wstring* error)
{
    const std::wstring path = ini_path_for(SCOPE_USER);
    if (!file_exists(path)) {
        // Nothing to write into yet; make it the same way a save would.
        VoiceFile scratch;
        scratch.load(SCOPE_USER);
        scratch.mark_dirty();
        if (!scratch.save(error)) {
            return false;
        }
    }

    // BasedOn gives the preview the same starting point the real voice has, so
    // that anything not named explicitly below still sounds right.
    std::wstring based_on = edit.get(KEY_BASED_ON);
    if (based_on.empty() && edit.builtin) {
        based_on = edit.name;
    }
    if (!based_on.empty()) {
        WritePrivateProfileStringW(kPreviewSection, voice_key_name(KEY_BASED_ON), based_on.c_str(),
                                   path.c_str());
    }
    for (int key = 0; key < KEY_COUNT; ++key) {
        if (key == KEY_BASED_ON || key == KEY_SPEAKER_NAME) {
            continue;  // the engine names a voice it does not know for itself
        }
        const std::wstring value = effective(edit, key);
        WritePrivateProfileStringW(kPreviewSection, voice_key_name(key),
                                   value.empty() ? nullptr : value.c_str(), path.c_str());
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    return true;
}

void remove_preview_section()
{
    const std::wstring path = ini_path_for(SCOPE_USER);
    if (file_exists(path)) {
        WritePrivateProfileStringW(kPreviewSection, nullptr, nullptr, path.c_str());
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    }
}

bool play_pcm(const std::vector<BYTE>& audio, const FormatResponse& format)
{
    if (audio.empty()) {
        return false;
    }

    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = format.channels ? format.channels : 1;
    wfx.nSamplesPerSec = format.samples_per_sec ? format.samples_per_sec : 16000;
    wfx.wBitsPerSample = format.bits ? format.bits : 16;
    wfx.nBlockAlign = static_cast<WORD>(wfx.nChannels * (wfx.wBitsPerSample / 8));
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HWAVEOUT device = nullptr;
    if (waveOutOpen(&device, WAVE_MAPPER, &wfx, reinterpret_cast<DWORD_PTR>(done), 0,
                    CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(done);
        return false;
    }

    WAVEHDR header = {};
    header.lpData = const_cast<LPSTR>(reinterpret_cast<LPCSTR>(audio.data()));
    header.dwBufferLength = static_cast<DWORD>(audio.size());
    waveOutPrepareHeader(device, &header, sizeof(header));
    if (waveOutWrite(device, &header, sizeof(header)) != MMSYSERR_NOERROR) {
        waveOutUnprepareHeader(device, &header, sizeof(header));
        waveOutClose(device);
        CloseHandle(done);
        return false;
    }

    while (!(header.dwFlags & WHDR_DONE)) {
        WaitForSingleObject(done, 100);
        if (InterlockedCompareExchange(&g_preview_stop, 0, 0)) {
            waveOutReset(device);
            break;
        }
    }

    waveOutUnprepareHeader(device, &header, sizeof(header));
    waveOutClose(device);
    CloseHandle(done);
    return true;
}

DWORD WINAPI preview_thread(LPVOID parameter)
{
    std::wstring* sentence = static_cast<std::wstring*>(parameter);
    const std::wstring text = *sentence;
    delete sentence;

    bool ok = false;
    std::wstring message;

    {
        WorkerClient client;

        // The engine reads voices.ini when it starts, so the copy that is
        // already running knows nothing about a voice written a moment ago.
        // Stopping it makes the next connection start a fresh one. A screen
        // reader speaking through these voices goes quiet for about a second
        // while that happens, which is why it is only done to preview.
        client.shutdown_worker();

        std::vector<VoiceRecord> voices;
        if (!client.voices(&voices)) {
            message = L"The speech engine could not be started. See the log in " +
                      std::wstring(log_path()) + L".";
        } else {
            const VoiceRecord* found = nullptr;
            for (const VoiceRecord& record : voices) {
                if (_wcsicmp(record.display_name, kPreviewSection) == 0) {
                    found = &record;
                    break;
                }
            }
            if (!found) {
                message = L"The engine would not accept these settings, so there is nothing "
                          L"to hear. Check the language and try again.";
            } else {
                std::vector<BYTE> audio;
                FormatResponse format = {};
                format.samples_per_sec = 16000;
                format.avg_bytes_per_sec = 32000;
                format.channels = 1;
                format.bits = 16;

                SpeakRequest request = {};
                strncpy_s(request.mode_guid, found->mode_guid, _TRUNCATE);
                request.rate_step = 0;
                request.pitch_step = 0;
                request.volume_pct = 100;
                request.timeout_ms = static_cast<uint32_t>(30000 + text.size() * 200);

                DoneResponse done_response = {};
                const bool spoke = client.speak(
                    request, text,
                    [&audio](const void* data, unsigned long bytes) -> bool {
                        const BYTE* p = static_cast<const BYTE*>(data);
                        audio.insert(audio.end(), p, p + bytes);
                        return InterlockedCompareExchange(&g_preview_stop, 0, 0) == 0;
                    },
                    nullptr,
                    [&format](const FormatResponse& fmt) {
                        if (fmt.samples_per_sec) {
                            format = fmt;
                        }
                    },
                    &done_response);

                if (!spoke || audio.empty()) {
                    message = L"The engine produced no sound for this voice. See the log in " +
                              std::wstring(log_path()) + L".";
                } else if (InterlockedCompareExchange(&g_preview_stop, 0, 0)) {
                    ok = true;
                    message = L"Preview stopped.";
                } else {
                    play_pcm(audio, format);
                    ok = true;
                    message = InterlockedCompareExchange(&g_preview_stop, 0, 0)
                                  ? L"Preview stopped."
                                  : L"Preview finished.";
                }
            }
        }
    }

    remove_preview_section();
    g_preview_message = message;
    PostMessageW(g_preview_owner, WM_APP_PREVIEW_DONE, ok ? 1 : 0, 0);
    return 0;
}

bool preview_running()
{
    if (!g_preview_thread) {
        return false;
    }
    if (WaitForSingleObject(g_preview_thread, 0) == WAIT_OBJECT_0) {
        CloseHandle(g_preview_thread);
        g_preview_thread = nullptr;
        return false;
    }
    return true;
}

// `preview_button` and `stop_button` are the two controls on whichever dialog
// asked, so focus can be moved to something that is still usable.
void start_preview(HWND dlg, const VoiceEdit& edit, int preview_button, int stop_button)
{
    if (preview_running()) {
        return;
    }

    std::wstring error;
    if (!write_preview_section(edit, &error)) {
        say_problem(dlg, L"This voice could not be prepared for preview.\r\n\r\n" + error);
        return;
    }

    std::wstring sentence = g_file.settings().preview_text;
    if (trim(sentence).empty()) {
        sentence = kDefaultPreviewText;
    }

    InterlockedExchange(&g_preview_stop, 0);
    g_preview_owner = dlg;
    g_preview_thread = CreateThread(nullptr, 0, preview_thread, new std::wstring(sentence), 0,
                                    nullptr);
    if (!g_preview_thread) {
        remove_preview_section();
        say_problem(dlg, L"The preview could not be started.");
        return;
    }

    if (stop_button) {
        EnableWindow(GetDlgItem(dlg, stop_button), TRUE);
        SetFocus(GetDlgItem(dlg, stop_button));
    }
    EnableWindow(GetDlgItem(dlg, preview_button), FALSE);
}

void finish_preview(HWND dlg, int preview_button, int stop_button)
{
    HWND preview = GetDlgItem(dlg, preview_button);
    EnableWindow(preview, TRUE);
    if (stop_button) {
        HWND stop = GetDlgItem(dlg, stop_button);
        if (GetFocus() == stop) {
            SetFocus(preview);
        }
        EnableWindow(stop, FALSE);
    }
    preview_running();  // reaps the thread handle
}

// --- the voice editor ------------------------------------------------------

struct VoiceDialogData {
    VoiceEdit edit;
    bool creating = false;
    std::wstring original_name;
};

const wchar_t kVoiceHint[] =
    L"Pitch is the engine's own number, not hertz: the engine works out the pitch as "
    L"three times Pitch, minus 49, and keeps the result between 30 and 250 hertz. The "
    L"male voices use 50, the female voices 73, the child voices 90.\r\n"
    L"\r\n"
    L"Loudness (Dynamic) makes the delivery more forceful and more strongly stressed as it "
    L"rises; breathiness (Aspiration) makes it more whispery. The vocal tract shape changes "
    L"the character of a voice more than anything else here.\r\n"
    L"\r\n"
    L"Based on copies every setting from an existing voice first, which is the easy way to "
    L"get the language right; the settings above are then applied on top of it.\r\n"
    L"\r\n"
    L"Gender and age change only what programs report about the voice, not how it sounds.\r\n"
    L"\r\n"
    L"The advanced boxes are the engine's own file names and identifiers. Leave them alone "
    L"unless you have a reason. The speaker name is ignored for a voice you add: the engine "
    L"only accepts a name beginning with a language it knows, so one is generated for you, "
    L"and the name you choose is what Windows shows.\r\n"
    L"\r\n"
    L"Preview speaks these settings without keeping them. It restarts the speech engine, so "
    L"a screen reader using an Infovox voice will pause for a moment.";

void fill_basedon_combo(HWND dlg, const std::wstring& select)
{
    HWND combo = GetDlgItem(dlg, IDC_V_BASEDON);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"(nothing -- use the settings below only)"));
    int chosen = 0;
    for (const Voice& voice : catalog().voices()) {
        const std::wstring name = widen(voice.display_name);
        if (_wcsicmp(name.c_str(), kPreviewSection) == 0) {
            continue;
        }
        const LRESULT index =
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        if (!select.empty() && _wcsicmp(name.c_str(), select.c_str()) == 0) {
            chosen = static_cast<int>(index);
        }
    }
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(chosen), 0);
}

void fill_language_combo(HWND dlg, const std::wstring& rule_file)
{
    HWND combo = GetDlgItem(dlg, IDC_V_LANGUAGE);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int chosen = -1;
    const std::vector<Language>& list = languages();
    for (size_t i = 0; i < list.size(); ++i) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(list[i].name.c_str()));
        if (_wcsicmp(list[i].rule_file.c_str(), rule_file.c_str()) == 0) {
            chosen = static_cast<int>(i);
        }
    }
    if (chosen < 0) {
        // A rule file this build does not know about: kept, and shown as it is,
        // rather than quietly replaced with one that is known.
        const std::wstring label = L"Other: " + rule_file;
        chosen = static_cast<int>(
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())));
    }
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(chosen), 0);
}

void fill_formant_combo(HWND dlg, const std::wstring& value)
{
    HWND combo = GetDlgItem(dlg, IDC_V_FORMANT);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i <= 4; ++i) {
        SendMessageW(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(formant_description(i)));
    }
    const int chosen = to_int(value, 0);
    if (chosen >= 0 && chosen <= 4 && !value.empty()) {
        SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(chosen), 0);
    } else if (value.empty()) {
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    } else {
        const std::wstring label = value + L" -- not one the engine documents";
        const LRESULT index =
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
    }
}

void fill_gender_combo(HWND dlg, const std::wstring& value)
{
    HWND combo = GetDlgItem(dlg, IDC_V_GENDER);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Not stated"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Female"));
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Male"));
    const int chosen = (value == L"1") ? 1 : (value == L"2" ? 2 : 0);
    SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(chosen), 0);
}

void update_pitch_readout(HWND dlg)
{
    const int pitch = to_int(text_of(dlg, IDC_V_PITCH), 50);
    set_text(dlg, IDC_V_PITCH_HZ, number(pitch_to_hertz(pitch)));
}

void set_spin(HWND dlg, int spin_id, int lo, int hi, int value)
{
    SendDlgItemMessageW(dlg, spin_id, UDM_SETRANGE32, static_cast<WPARAM>(lo),
                        static_cast<LPARAM>(hi));
    SendDlgItemMessageW(dlg, spin_id, UDM_SETPOS32, 0, static_cast<LPARAM>(clamp(value, lo, hi)));
}

// An up-down control has no text of its own, so without this it would be
// announced as an unnamed spin button. The name it is given here says which
// value it changes, the way its edit box's label does.
void name_spin(HWND dlg, int spin_id, const wchar_t* name)
{
    SetDlgItemTextW(dlg, spin_id, name);
}

// Puts the sound and language boxes back to what a template voice has, which is
// what choosing a different "Based on" is for.
void apply_template(HWND dlg, const std::wstring& based_on)
{
    VoiceEdit probe;
    probe.name = based_on;
    probe.set(KEY_BASED_ON, based_on);

    set_spin(dlg, IDC_V_PITCH_SPIN, 27, 99, to_int(inherited_value(probe, KEY_PITCH), 50));
    set_spin(dlg, IDC_V_DYNAMIC_SPIN, 0, 100, to_int(inherited_value(probe, KEY_DYNAMIC), 50));
    set_spin(dlg, IDC_V_ASPIRATION_SPIN, 0, 100,
             to_int(inherited_value(probe, KEY_ASPIRATION), 0));
    set_spin(dlg, IDC_V_AGE_SPIN, 1, 120, to_int(inherited_value(probe, KEY_AGE), 30));
    fill_formant_combo(dlg, inherited_value(probe, KEY_FORMANT_NO));
    fill_gender_combo(dlg, inherited_value(probe, KEY_GENDER));
    fill_language_combo(dlg, inherited_value(probe, KEY_LANGUAGE_FILE));
    set_text(dlg, IDC_V_LANGUAGE_FILE, inherited_value(probe, KEY_LANGUAGE_FILE));
    set_text(dlg, IDC_V_LANGUAGE_ID, inherited_value(probe, KEY_LANGUAGE_ID));
    set_text(dlg, IDC_V_LCID, inherited_value(probe, KEY_LCID));
    update_pitch_readout(dlg);
}

// Everything on the dialog, gathered back into an edit.
VoiceEdit harvest(HWND dlg, const VoiceDialogData& data)
{
    VoiceEdit edit = data.edit;
    edit.name = trim(text_of(dlg, IDC_V_NAME));

    const LRESULT base = SendDlgItemMessageW(dlg, IDC_V_BASEDON, CB_GETCURSEL, 0, 0);
    if (edit.builtin || base <= 0) {
        // A section that changes a built-in voice is applied to that voice, so
        // BasedOn would say nothing; the catalogue ignores it there.
        edit.set(KEY_BASED_ON, L"");
    } else {
        wchar_t name[256] = L"";
        SendDlgItemMessageW(dlg, IDC_V_BASEDON, CB_GETLBTEXT, static_cast<WPARAM>(base),
                            reinterpret_cast<LPARAM>(name));
        edit.set(KEY_BASED_ON, name);
    }

    edit.set(KEY_LANGUAGE_FILE, trim(text_of(dlg, IDC_V_LANGUAGE_FILE)));
    edit.set(KEY_LANGUAGE_ID, trim(text_of(dlg, IDC_V_LANGUAGE_ID)));
    edit.set(KEY_LCID, trim(text_of(dlg, IDC_V_LCID)));

    edit.set(KEY_PITCH, number(clamp(to_int(text_of(dlg, IDC_V_PITCH), 50), 1, 999)));
    edit.set(KEY_DYNAMIC, number(clamp(to_int(text_of(dlg, IDC_V_DYNAMIC), 50), 0, 100)));
    edit.set(KEY_ASPIRATION, number(clamp(to_int(text_of(dlg, IDC_V_ASPIRATION), 0), 0, 100)));
    edit.set(KEY_AGE, number(clamp(to_int(text_of(dlg, IDC_V_AGE), 30), 1, 120)));

    const LRESULT formant = SendDlgItemMessageW(dlg, IDC_V_FORMANT, CB_GETCURSEL, 0, 0);
    if (formant >= 0 && formant <= 4) {
        edit.set(KEY_FORMANT_NO, number(static_cast<int>(formant)));
    }  // anything else is a value this build did not recognise; it is left as it was

    const LRESULT gender = SendDlgItemMessageW(dlg, IDC_V_GENDER, CB_GETCURSEL, 0, 0);
    edit.set(KEY_GENDER, gender == 1 ? L"1" : (gender == 2 ? L"2" : L""));

    edit.set(KEY_SPEAKER_NAME, trim(text_of(dlg, IDC_V_SPEAKER_NAME)));
    edit.set(KEY_SPEAKER_STYLE, trim(text_of(dlg, IDC_V_SPEAKER_STYLE)));
    edit.set(KEY_LIBRARY_FILE, trim(text_of(dlg, IDC_V_LIBRARY_FILE)));
    edit.set(KEY_PHSYM_FILE, trim(text_of(dlg, IDC_V_PHSYM_FILE)));
    edit.set(KEY_DIPHONE_FILE, trim(text_of(dlg, IDC_V_DIPHONE_FILE)));
    edit.set(KEY_MAPPING_FILE, trim(text_of(dlg, IDC_V_MAPPING_FILE)));

    // The engine works this out for a voice it does not already know, so
    // writing it would be writing a value that is thrown away.
    if (!edit.builtin && _wcsicmp(edit.get(KEY_SPEAKER_NAME).c_str(),
                                  inherited_value(edit, KEY_SPEAKER_NAME).c_str()) == 0) {
        edit.set(KEY_SPEAKER_NAME, L"");
    }
    return edit;
}

bool name_is_free(const std::wstring& name, const std::wstring& original)
{
    if (_wcsicmp(name.c_str(), original.c_str()) == 0) {
        return true;
    }
    if (_wcsicmp(name.c_str(), kPreviewSection) == 0) {
        return false;
    }
    if (g_file.find(name) >= 0) {
        return false;
    }
    return catalog().find_by_name(name) < 0;
}

INT_PTR CALLBACK voice_proc(HWND dlg, UINT message, WPARAM wparam, LPARAM lparam)
{
    VoiceDialogData* data =
        reinterpret_cast<VoiceDialogData*>(GetWindowLongPtrW(dlg, GWLP_USERDATA));

    switch (message) {
        case WM_INITDIALOG: {
            data = reinterpret_cast<VoiceDialogData*>(lparam);
            SetWindowLongPtrW(dlg, GWLP_USERDATA, lparam);

            SetWindowTextW(dlg, data->creating ? L"New voice" : L"Voice settings");
            set_text(dlg, IDC_V_NAME, data->edit.name);
            fill_basedon_combo(dlg, data->edit.get(KEY_BASED_ON));
            fill_language_combo(dlg, effective(data->edit, KEY_LANGUAGE_FILE));
            fill_formant_combo(dlg, effective(data->edit, KEY_FORMANT_NO));
            fill_gender_combo(dlg, effective(data->edit, KEY_GENDER));

            set_spin(dlg, IDC_V_PITCH_SPIN, 27, 99,
                     to_int(effective(data->edit, KEY_PITCH), 50));
            set_spin(dlg, IDC_V_DYNAMIC_SPIN, 0, 100,
                     to_int(effective(data->edit, KEY_DYNAMIC), 50));
            set_spin(dlg, IDC_V_ASPIRATION_SPIN, 0, 100,
                     to_int(effective(data->edit, KEY_ASPIRATION), 0));
            set_spin(dlg, IDC_V_AGE_SPIN, 1, 120, to_int(effective(data->edit, KEY_AGE), 30));

            name_spin(dlg, IDC_V_PITCH_SPIN, L"Pitch");
            name_spin(dlg, IDC_V_DYNAMIC_SPIN, L"Loudness, Dynamic");
            name_spin(dlg, IDC_V_ASPIRATION_SPIN, L"Breathiness, Aspiration");
            name_spin(dlg, IDC_V_AGE_SPIN, L"Age in years");

            set_text(dlg, IDC_V_SPEAKER_NAME, effective(data->edit, KEY_SPEAKER_NAME));
            set_text(dlg, IDC_V_SPEAKER_STYLE, effective(data->edit, KEY_SPEAKER_STYLE));
            set_text(dlg, IDC_V_LANGUAGE_FILE, effective(data->edit, KEY_LANGUAGE_FILE));
            set_text(dlg, IDC_V_LANGUAGE_ID, effective(data->edit, KEY_LANGUAGE_ID));
            set_text(dlg, IDC_V_LCID, effective(data->edit, KEY_LCID));
            set_text(dlg, IDC_V_LIBRARY_FILE, effective(data->edit, KEY_LIBRARY_FILE));
            set_text(dlg, IDC_V_PHSYM_FILE, effective(data->edit, KEY_PHSYM_FILE));
            set_text(dlg, IDC_V_DIPHONE_FILE, effective(data->edit, KEY_DIPHONE_FILE));
            set_text(dlg, IDC_V_MAPPING_FILE, effective(data->edit, KEY_MAPPING_FILE));
            set_text(dlg, IDC_V_HINT, kVoiceHint);
            update_pitch_readout(dlg);

            // A voice that changes a built-in cannot be based on something
            // else: the catalogue applies its settings to the voice of that
            // name, and has nothing to copy from.
            if (data->edit.builtin) {
                EnableWindow(GetDlgItem(dlg, IDC_V_BASEDON), FALSE);
            }
            SetFocus(GetDlgItem(dlg, IDC_V_NAME));
            return FALSE;  // focus has been placed deliberately
        }

        case WM_APP_PREVIEW_DONE:
            finish_preview(dlg, IDC_V_PREVIEW, 0);
            if (!wparam) {
                say_problem(dlg, g_preview_message);
            }
            return TRUE;

        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case IDC_V_PITCH:
                    if (HIWORD(wparam) == EN_CHANGE) {
                        update_pitch_readout(dlg);
                    }
                    return TRUE;

                case IDC_V_BASEDON:
                    if (HIWORD(wparam) == CBN_SELCHANGE) {
                        const LRESULT index =
                            SendDlgItemMessageW(dlg, IDC_V_BASEDON, CB_GETCURSEL, 0, 0);
                        if (index > 0) {
                            wchar_t name[256] = L"";
                            SendDlgItemMessageW(dlg, IDC_V_BASEDON, CB_GETLBTEXT,
                                                static_cast<WPARAM>(index),
                                                reinterpret_cast<LPARAM>(name));
                            apply_template(dlg, name);
                        }
                    }
                    return TRUE;

                case IDC_V_LANGUAGE:
                    if (HIWORD(wparam) == CBN_SELCHANGE) {
                        const LRESULT index =
                            SendDlgItemMessageW(dlg, IDC_V_LANGUAGE, CB_GETCURSEL, 0, 0);
                        const std::vector<Language>& list = languages();
                        if (index >= 0 && static_cast<size_t>(index) < list.size()) {
                            const Language& language = list[static_cast<size_t>(index)];
                            set_text(dlg, IDC_V_LANGUAGE_FILE, language.rule_file);
                            set_text(dlg, IDC_V_LANGUAGE_ID, language.language_id);
                            wchar_t code[16];
                            _snwprintf_s(code, _countof(code), _TRUNCATE, L"%04x",
                                         static_cast<unsigned>(language.lcid));
                            set_text(dlg, IDC_V_LCID, code);
                        }
                    }
                    return TRUE;

                case IDC_V_PREVIEW: {
                    const std::wstring name = trim(text_of(dlg, IDC_V_NAME));
                    if (name.empty()) {
                        say_problem(dlg, L"Give the voice a name first.");
                        SetFocus(GetDlgItem(dlg, IDC_V_NAME));
                        return TRUE;
                    }
                    start_preview(dlg, harvest(dlg, *data), IDC_V_PREVIEW, 0);
                    return TRUE;
                }

                case IDOK: {
                    const std::wstring name = trim(text_of(dlg, IDC_V_NAME));
                    if (name.empty()) {
                        say_problem(dlg, L"Give the voice a name. It is the name Windows will "
                                         L"show in the voice list.");
                        SetFocus(GetDlgItem(dlg, IDC_V_NAME));
                        return TRUE;
                    }
                    if (name.find_first_of(L"[]=;") != std::wstring::npos) {
                        say_problem(dlg, L"A voice name cannot contain a square bracket, an "
                                         L"equals sign or a semicolon.");
                        SetFocus(GetDlgItem(dlg, IDC_V_NAME));
                        return TRUE;
                    }
                    if (name.size() > 90) {
                        say_problem(dlg, L"That name is too long. Use 90 characters or fewer.");
                        SetFocus(GetDlgItem(dlg, IDC_V_NAME));
                        return TRUE;
                    }
                    if (!name_is_free(name, data->original_name)) {
                        say_problem(dlg, L"There is already a voice called that. Choose another "
                                         L"name.");
                        SetFocus(GetDlgItem(dlg, IDC_V_NAME));
                        return TRUE;
                    }
                    data->edit = harvest(dlg, *data);
                    EndDialog(dlg, IDOK);
                    return TRUE;
                }

                case IDCANCEL:
                    if (preview_running()) {
                        InterlockedExchange(&g_preview_stop, 1);
                    }
                    EndDialog(dlg, IDCANCEL);
                    return TRUE;

                default:
                    break;
            }
            return FALSE;

        case WM_CLOSE:
            EndDialog(dlg, IDCANCEL);
            return TRUE;

        default:
            break;
    }
    return FALSE;
}

bool edit_voice(HWND owner, VoiceDialogData* data)
{
    return DialogBoxParamW(g_instance, MAKEINTRESOURCEW(IDD_VOICE), owner, voice_proc,
                           reinterpret_cast<LPARAM>(data)) == IDOK;
}

// --- engine settings -------------------------------------------------------

const wchar_t kEngineHint[] =
    L"Trimming removes the eight tenths of a second of inaudible padding the engine adds to "
    L"the end of every utterance. With it off you hear exactly what the engine produced, "
    L"including that silence after everything your screen reader says.\r\n"
    L"\r\n"
    L"Word and sentence positions are what a program uses to highlight the word being "
    L"spoken. Turning them off is worth trying if a program's highlighting is more trouble "
    L"than it is worth.\r\n"
    L"\r\n"
    L"The rate and pitch limits decide what the ends of a program's speed and pitch controls "
    L"reach. Zero means the engine is asked, which is what it normally does: about 15 to 500 "
    L"words a minute, and 30 to 250 hertz. Setting a lower fastest rate spreads the "
    L"remaining range over the same number of steps, which makes the control finer.\r\n"
    L"\r\n"
    L"Log detail is written to the log file in your profile. Level 5 is the one to use when "
    L"reporting a problem. A program reads the setting when it starts speaking, so restart "
    L"the speaking program after changing it.";

struct EngineDialogData {
    EngineSettings settings;
    int log_level = 3;
};

void put_engine_values(HWND dlg, const EngineSettings& settings, int log_level)
{
    CheckDlgButton(dlg, IDC_E_TRIM, settings.trim_trailing_silence ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dlg, IDC_E_WORDS, settings.word_events ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dlg, IDC_E_SENTENCES, settings.sentence_events ? BST_CHECKED : BST_UNCHECKED);
    set_spin(dlg, IDC_E_THRESHOLD_SPIN, 0, 1000, settings.silence_threshold);
    set_text(dlg, IDC_E_TIMEOUT, number(settings.timeout_base_ms));
    set_text(dlg, IDC_E_TIMEOUT_PER_CHAR, number(settings.timeout_per_char_ms));
    set_text(dlg, IDC_E_RATE_MIN, number(settings.rate_min));
    set_text(dlg, IDC_E_RATE_MAX, number(settings.rate_max));
    set_text(dlg, IDC_E_RATE_DEFAULT, number(settings.rate_default));
    set_text(dlg, IDC_E_PITCH_MIN, number(settings.pitch_min));
    set_text(dlg, IDC_E_PITCH_MAX, number(settings.pitch_max));
    set_text(dlg, IDC_E_PITCH_DEFAULT, number(settings.pitch_default));
    set_text(dlg, IDC_E_PREVIEW_TEXT, settings.preview_text);
    SendDlgItemMessageW(dlg, IDC_E_LOGLEVEL, CB_SETCURSEL, static_cast<WPARAM>(clamp(log_level, 0, 5)),
                        0);
}

INT_PTR CALLBACK engine_proc(HWND dlg, UINT message, WPARAM wparam, LPARAM lparam)
{
    EngineDialogData* data =
        reinterpret_cast<EngineDialogData*>(GetWindowLongPtrW(dlg, GWLP_USERDATA));

    switch (message) {
        case WM_INITDIALOG: {
            data = reinterpret_cast<EngineDialogData*>(lparam);
            SetWindowLongPtrW(dlg, GWLP_USERDATA, lparam);

            HWND log = GetDlgItem(dlg, IDC_E_LOGLEVEL);
            const wchar_t* const levels[] = {
                L"0 -- nothing",
                L"1 -- errors only",
                L"2 -- errors and warnings",
                L"3 -- normal (the default)",
                L"4 -- detailed: every utterance",
                L"5 -- everything, for reporting a problem",
            };
            for (const wchar_t* level : levels) {
                SendMessageW(log, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(level));
            }

            put_engine_values(dlg, data->settings, data->log_level);
            name_spin(dlg, IDC_E_THRESHOLD_SPIN, L"Level counted as silence");
            set_text(dlg, IDC_E_HINT, kEngineHint);
            return TRUE;
        }

        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case IDC_E_DEFAULTS: {
                    EngineSettings fresh;
                    fresh.preview_text = kDefaultPreviewText;
                    put_engine_values(dlg, fresh, 3);
                    return TRUE;
                }

                case IDOK: {
                    EngineSettings& s = data->settings;
                    s.trim_trailing_silence = IsDlgButtonChecked(dlg, IDC_E_TRIM) == BST_CHECKED;
                    s.word_events = IsDlgButtonChecked(dlg, IDC_E_WORDS) == BST_CHECKED;
                    s.sentence_events = IsDlgButtonChecked(dlg, IDC_E_SENTENCES) == BST_CHECKED;
                    s.silence_threshold =
                        clamp(to_int(text_of(dlg, IDC_E_THRESHOLD), 16), 0, 32767);
                    s.timeout_base_ms =
                        (std::max)(1000, to_int(text_of(dlg, IDC_E_TIMEOUT), 30000));
                    s.timeout_per_char_ms =
                        (std::max)(0, to_int(text_of(dlg, IDC_E_TIMEOUT_PER_CHAR), 200));
                    s.rate_min = (std::max)(0, to_int(text_of(dlg, IDC_E_RATE_MIN), 0));
                    s.rate_max = (std::max)(0, to_int(text_of(dlg, IDC_E_RATE_MAX), 0));
                    s.rate_default = (std::max)(0, to_int(text_of(dlg, IDC_E_RATE_DEFAULT), 0));
                    s.pitch_min = (std::max)(0, to_int(text_of(dlg, IDC_E_PITCH_MIN), 0));
                    s.pitch_max = (std::max)(0, to_int(text_of(dlg, IDC_E_PITCH_MAX), 0));
                    s.pitch_default = (std::max)(0, to_int(text_of(dlg, IDC_E_PITCH_DEFAULT), 0));
                    s.preview_text = trim(text_of(dlg, IDC_E_PREVIEW_TEXT));
                    if (s.preview_text.empty()) {
                        s.preview_text = kDefaultPreviewText;
                    }
                    const LRESULT level =
                        SendDlgItemMessageW(dlg, IDC_E_LOGLEVEL, CB_GETCURSEL, 0, 0);
                    data->log_level = (level == CB_ERR) ? 3 : static_cast<int>(level);
                    EndDialog(dlg, IDOK);
                    return TRUE;
                }

                case IDCANCEL:
                    EndDialog(dlg, IDCANCEL);
                    return TRUE;

                default:
                    break;
            }
            return FALSE;

        case WM_CLOSE:
            EndDialog(dlg, IDCANCEL);
            return TRUE;

        default:
            break;
    }
    return FALSE;
}

// --- publishing to Windows -------------------------------------------------

std::wstring system_root()
{
    wchar_t buf[MAX_PATH] = L"";
    GetWindowsDirectoryW(buf, MAX_PATH);
    return buf;
}

// This utility is 32-bit, so a plain System32 path is redirected to SysWOW64.
// Sysnative is the way back to the real System32, and is the only way to run
// the 64-bit regsvr32 that the 64-bit speech interface needs.
std::wstring regsvr32_path(bool native)
{
    BOOL wow64 = FALSE;
    IsWow64Process(GetCurrentProcess(), &wow64);
    const std::wstring root = system_root();
    if (native) {
        return wow64 ? root + L"\\Sysnative\\regsvr32.exe" : root + L"\\System32\\regsvr32.exe";
    }
    const std::wstring wow = root + L"\\SysWOW64\\regsvr32.exe";
    return file_exists(wow) ? wow : root + L"\\System32\\regsvr32.exe";
}

// Registration writes to HKEY_LOCAL_MACHINE, which needs administrator rights;
// Windows asks for them rather than this utility having to run elevated for
// everything else it does.
bool run_elevated(const std::wstring& exe, const std::wstring& args, DWORD* exit_code)
{
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"runas";
    info.lpFile = exe.c_str();
    info.lpParameters = args.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info) || !info.hProcess) {
        return false;
    }
    WaitForSingleObject(info.hProcess, 120000);
    DWORD code = 1;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    if (exit_code) {
        *exit_code = code;
    }
    return true;
}

bool publish_voices(HWND dlg)
{
    const std::wstring dll32 = install_dir() + L"\\Infovox230SAPI.dll";
    const std::wstring dll64 = install_dir() + L"\\x64\\Infovox230SAPI.dll";
    if (!file_exists(dll32)) {
        say_problem(dlg, L"The speech interface was not found next to this utility, so the "
                         L"voices cannot be published. Reinstall Infovox 230.");
        return false;
    }

    DWORD code = 0;
    if (!run_elevated(regsvr32_path(false), L"/s \"" + dll32 + L"\"", &code)) {
        set_status(dlg, L"Publishing was cancelled, so the voice list in Windows has not "
                        L"changed.");
        return false;
    }
    if (code != 0) {
        say_problem(dlg, L"The 32-bit speech interface could not be registered. Details are in "
                         L"the log:\r\n\r\n" +
                             std::wstring(log_path()));
        return false;
    }

    if (file_exists(dll64)) {
        DWORD code64 = 0;
        if (run_elevated(regsvr32_path(true), L"/s \"" + dll64 + L"\"", &code64) && code64 != 0) {
            say_problem(dlg, L"The 64-bit speech interface could not be registered, so 64-bit "
                             L"programs such as Narrator may not see your voices.");
        }
    }

    g_publish_pending = false;
    set_status(dlg, L"The voices are published. A program that is already running will not "
                    L"see the new list until it is restarted.");
    return true;
}

// --- the main window -------------------------------------------------------

void update_path_box(HWND dlg)
{
    set_text(dlg, IDC_INIPATH, g_file.path());
}

bool save_now(HWND dlg)
{
    const int row = selected_row(dlg);
    const std::wstring keep = row >= 0 ? g_rows[static_cast<size_t>(row)].name : std::wstring();

    std::wstring error;
    if (g_file.save(&error)) {
        write_log_level(g_log_level);
        g_publish_pending = true;
        rebuild_list(dlg, keep);
        update_details(dlg);
        set_status(dlg, L"Saved to " + g_file.path() +
                            L". Choose Publish voices to Windows to put them in the Windows "
                            L"voice list.");
        return true;
    }
    say_problem(dlg, L"Your voices could not be saved.\r\n\r\n" + error +
                         L"\r\n\r\nFile: " + g_file.path());
    return false;
}

void reload_file(HWND dlg, Scope scope, const std::wstring& select_name)
{
    reload_catalog();
    g_file.load(scope);
    rebuild_list(dlg, select_name);
    update_details(dlg);
    update_path_box(dlg);
}

// Yes, No or Cancel on the way out, so nothing is lost silently and nothing is
// written that was not asked for.
bool confirm_close(HWND dlg)
{
    if (g_file.dirty()) {
        const int answer =
            MessageBoxW(dlg, L"Save your changes before closing?", L"Infovox 230 Configuration",
                        MB_YESNOCANCEL | MB_ICONQUESTION);
        if (answer == IDCANCEL) {
            return false;
        }
        if (answer == IDYES && !save_now(dlg)) {
            return false;
        }
    }
    if (g_publish_pending) {
        const int answer = MessageBoxW(
            dlg,
            L"Your voices have been saved but are not in the Windows voice list yet.\r\n\r\n"
            L"Publish them now? Windows will ask for administrator permission.",
            L"Infovox 230 Configuration", MB_YESNO | MB_ICONQUESTION);
        if (answer == IDYES) {
            publish_voices(dlg);
        }
    }
    if (preview_running()) {
        InterlockedExchange(&g_preview_stop, 1);
        WaitForSingleObject(g_preview_thread, 5000);
    }
    return true;
}

void on_new_voice(HWND dlg, bool duplicate)
{
    VoiceDialogData data;
    data.creating = true;

    if (duplicate) {
        const int row = selected_row(dlg);
        if (row < 0) {
            say_problem(dlg, L"Choose a voice to copy first.");
            return;
        }
        const ListRow& info = g_rows[static_cast<size_t>(row)];
        data.edit = edit_for(info);
        data.edit.builtin = false;
        data.edit.from_file = false;
        // Every setting is written out, so the copy does not change when the
        // voice it was copied from does.
        for (int key = 0; key < KEY_COUNT; ++key) {
            if (key == KEY_BASED_ON) {
                continue;
            }
            data.edit.set(key, effective(data.edit, key));
        }
        data.edit.set(KEY_BASED_ON, info.name);
        data.edit.name = info.name + L" copy";
        for (int n = 2; !name_is_free(data.edit.name, L""); ++n) {
            data.edit.name = info.name + L" copy " + number(n);
        }
    } else {
        // A new voice starts from the first built-in of the current selection's
        // language, which is the friendliest thing to open with.
        const int row = selected_row(dlg);
        const std::wstring base = row >= 0 ? g_rows[static_cast<size_t>(row)].name
                                           : widen(catalog().voices()[0].display_name);
        data.edit.name = L"My Infovox voice";
        for (int n = 2; !name_is_free(data.edit.name, L""); ++n) {
            data.edit.name = L"My Infovox voice " + number(n);
        }
        data.edit.set(KEY_BASED_ON, base);
        for (int key = KEY_LANGUAGE_FILE; key < KEY_COUNT; ++key) {
            data.edit.set(key, inherited_value(data.edit, key));
        }
        data.edit.set(KEY_SPEAKER_NAME, L"");
    }

    if (!edit_voice(dlg, &data)) {
        return;
    }
    g_file.put(data.edit);
    rebuild_list(dlg, data.edit.name);
    update_details(dlg);
    set_status(dlg, L"\"" + data.edit.name +
                        L"\" has been added. Choose Save to keep it, then Publish voices to "
                        L"Windows.");
}

void on_change_voice(HWND dlg)
{
    const int row = selected_row(dlg);
    if (row < 0) {
        say_problem(dlg, L"Choose a voice from the list first.");
        return;
    }
    const ListRow info = g_rows[static_cast<size_t>(row)];

    VoiceDialogData data;
    data.edit = edit_for(info);
    data.edit.builtin = info.builtin;
    data.original_name = info.name;
    if (!edit_voice(dlg, &data)) {
        return;
    }
    if (_wcsicmp(data.edit.name.c_str(), info.name.c_str()) != 0) {
        g_file.remove(info.name);
    }
    g_file.put(data.edit);
    rebuild_list(dlg, data.edit.name);
    update_details(dlg);
    set_status(dlg, L"\"" + data.edit.name + L"\" has been changed. Choose Save to keep it.");
}

// The rename box: a small dialog would need a template of its own, so the name
// is changed in the voice editor, which already validates it. This asks the
// editor to open with the name box focused.
void on_rename(HWND dlg)
{
    const int row = selected_row(dlg);
    if (row < 0) {
        say_problem(dlg, L"Choose a voice from the list first.");
        return;
    }
    const ListRow info = g_rows[static_cast<size_t>(row)];
    if (info.builtin) {
        say_problem(dlg, L"A built-in voice keeps its name. To have it under a name of your "
                         L"own, choose Duplicate and name the copy.");
        return;
    }
    on_change_voice(dlg);
}

void on_delete(HWND dlg)
{
    const int row = selected_row(dlg);
    if (row < 0) {
        say_problem(dlg, L"Choose a voice from the list first.");
        return;
    }
    const ListRow info = g_rows[static_cast<size_t>(row)];
    if (!info.in_file) {
        say_problem(dlg, info.builtin
                             ? L"This is a built-in voice and is not written in your file, so "
                               L"there is nothing to remove. Built-in voices cannot be deleted."
                             : L"This voice is defined in the other voices file. Change the "
                               L"setting under \"Save my voices for\" to edit it.");
        return;
    }

    const std::wstring question =
        info.builtin ? L"Put \"" + info.name + L"\" back to the way it was built?"
                     : L"Remove the voice \"" + info.name + L"\"?";
    if (MessageBoxW(dlg, question.c_str(), L"Infovox 230 Configuration",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    g_file.remove(info.name);
    rebuild_list(dlg, std::wstring());
    update_details(dlg);
    set_status(dlg, info.builtin ? L"The built-in settings will be restored when you save."
                                 : L"The voice will be removed when you save.");
}

void on_preview(HWND dlg)
{
    const int row = selected_row(dlg);
    if (row < 0) {
        say_problem(dlg, L"Choose a voice from the list first.");
        return;
    }
    set_status(dlg, L"Preparing the preview. The speech engine restarts, which takes a "
                    L"moment.");
    start_preview(dlg, edit_for(g_rows[static_cast<size_t>(row)]), IDC_PREVIEW, IDC_STOP);
}

void on_scope_changed(HWND dlg)
{
    const LRESULT choice = SendDlgItemMessageW(dlg, IDC_SCOPE, CB_GETCURSEL, 0, 0);
    const Scope wanted = (choice == 1) ? SCOPE_MACHINE : SCOPE_USER;
    if (wanted == g_file.scope()) {
        return;
    }

    if (g_file.dirty()) {
        const int answer = MessageBoxW(
            dlg, L"Save your changes to the current file before switching?",
            L"Infovox 230 Configuration", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (answer == IDCANCEL) {
            SendDlgItemMessageW(dlg, IDC_SCOPE, CB_SETCURSEL,
                                g_file.scope() == SCOPE_MACHINE ? 1 : 0, 0);
            return;
        }
        if (answer == IDYES && !save_now(dlg)) {
            SendDlgItemMessageW(dlg, IDC_SCOPE, CB_SETCURSEL,
                                g_file.scope() == SCOPE_MACHINE ? 1 : 0, 0);
            return;
        }
    }

    if (wanted == SCOPE_MACHINE && !scope_is_writable(SCOPE_MACHINE)) {
        MessageBoxW(dlg,
                    L"Voices for all users are kept in the installation folder, which only an "
                    L"administrator can write to.\r\n\r\n"
                    L"You can still look at the file and copy settings out of it. To change "
                    L"it, close this utility and start it again with \"Run as "
                    L"administrator\".",
                    L"Infovox 230 Configuration", MB_OK | MB_ICONINFORMATION);
    }

    reload_file(dlg, wanted, std::wstring());
    set_status(dlg, L"Now editing " + g_file.path());
}

void show_help(HWND dlg)
{
    const std::wstring readme = install_dir() + L"\\README.txt";
    if (file_exists(readme)) {
        ShellExecuteW(dlg, L"open", readme.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        set_status(dlg, L"The read me file has been opened.");
        return;
    }
    MessageBoxW(dlg,
                L"Choose a voice in the list and then Change to alter it, or New voice to "
                L"add one of your own.\r\n\r\n"
                L"Preview speaks the settings as they stand, without keeping them. Save "
                L"writes them to your voices file. Publish voices to Windows puts them in "
                L"the Windows voice list, which needs administrator permission and is what "
                L"makes them appear in your screen reader.\r\n\r\n"
                L"Engine settings covers everything that is not a property of one voice.",
                L"Infovox 230 Configuration", MB_OK | MB_ICONINFORMATION);
}

INT_PTR CALLBACK main_proc(HWND dlg, UINT message, WPARAM wparam, LPARAM)
{
    switch (message) {
        case WM_INITDIALOG: {
            HWND scope = GetDlgItem(dlg, IDC_SCOPE);
            SendMessageW(scope, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Just me"));
            SendMessageW(scope, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(L"All users (needs administrator)"));
            SendMessageW(scope, CB_SETCURSEL, g_file.scope() == SCOPE_MACHINE ? 1 : 0, 0);

            EnableWindow(GetDlgItem(dlg, IDC_STOP), FALSE);
            rebuild_list(dlg, std::wstring());
            update_details(dlg);
            update_path_box(dlg);
            set_status(dlg, L"Ready. " + number(static_cast<int>(catalog().size())) +
                                L" voices are installed.");
            SetFocus(GetDlgItem(dlg, IDC_VOICE_LIST));
            return FALSE;
        }

        case WM_APP_PREVIEW_DONE:
            finish_preview(dlg, IDC_PREVIEW, IDC_STOP);
            set_status(dlg, g_preview_message);
            if (!wparam) {
                say_problem(dlg, g_preview_message);
            }
            return TRUE;

        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case IDC_VOICE_LIST:
                    if (HIWORD(wparam) == LBN_SELCHANGE) {
                        update_details(dlg);
                    } else if (HIWORD(wparam) == LBN_DBLCLK) {
                        on_change_voice(dlg);
                    }
                    return TRUE;

                case IDC_NEW:
                    on_new_voice(dlg, false);
                    return TRUE;

                case IDC_DUPLICATE:
                    on_new_voice(dlg, true);
                    return TRUE;

                case IDC_CHANGE:
                    on_change_voice(dlg);
                    return TRUE;

                case IDC_RENAME:
                    on_rename(dlg);
                    return TRUE;

                case IDC_DELETE:
                    on_delete(dlg);
                    return TRUE;

                case IDC_PREVIEW:
                    on_preview(dlg);
                    return TRUE;

                case IDC_STOP:
                    InterlockedExchange(&g_preview_stop, 1);
                    set_status(dlg, L"Stopping.");
                    return TRUE;

                case IDC_SCOPE:
                    if (HIWORD(wparam) == CBN_SELCHANGE) {
                        on_scope_changed(dlg);
                    }
                    return TRUE;

                case IDC_ENGINE_SETTINGS: {
                    EngineDialogData data;
                    data.settings = g_file.settings();
                    data.log_level = g_log_level;
                    if (DialogBoxParamW(g_instance, MAKEINTRESOURCEW(IDD_ENGINE), dlg, engine_proc,
                                        reinterpret_cast<LPARAM>(&data)) == IDOK) {
                        g_file.settings() = data.settings;
                        g_file.mark_dirty();
                        g_log_level = data.log_level;
                        set_status(dlg, L"Engine settings changed. Choose Save to keep them.");
                    }
                    return TRUE;
                }

                case IDC_PUBLISH:
                    if (g_file.dirty() && !save_now(dlg)) {
                        return TRUE;
                    }
                    publish_voices(dlg);
                    return TRUE;

                case IDC_SAVE:
                    save_now(dlg);
                    return TRUE;

                case IDC_HELP_BUTTON:
                    show_help(dlg);
                    return TRUE;

                case IDC_CLOSE:
                case IDCANCEL:
                    if (confirm_close(dlg)) {
                        EndDialog(dlg, 0);
                    }
                    return TRUE;

                default:
                    break;
            }
            return FALSE;

        case WM_CLOSE:
            if (confirm_close(dlg)) {
                EndDialog(dlg, 0);
            }
            return TRUE;

        default:
            break;
    }
    return FALSE;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR command_line, int)
{
    g_instance = instance;

    // System DPI awareness: the dialogs are laid out in dialog units, which
    // scale with the system font, so this is all that is needed for them to
    // come out the right size on a high resolution screen.
    SetProcessDPIAware();

    log_init("config");
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    // A preview that was interrupted by a crash or a power cut would otherwise
    // leave its temporary voice behind.
    remove_preview_section();

    Scope scope = SCOPE_USER;
    if (command_line && wcsstr(command_line, L"--all-users")) {
        scope = SCOPE_MACHINE;
    }
    g_log_level = read_log_level();
    reload_catalog();
    g_file.load(scope);

    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, main_proc, 0);

    log_shutdown();
    return 0;
}
