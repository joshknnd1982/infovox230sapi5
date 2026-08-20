// Exercises the voices the way a real SAPI client does, end to end.
//
// sapi_probe hands the engine a stand-in token, which skips everything SAPI does between
// a registry key and ISpTTSEngine::SetObjectToken. This tool instead registers the COM
// server and the voice tokens for the current user, then enumerates the voice category
// through SAPI itself and speaks each voice through SpVoice into a wav file -- the same
// route Narrator, NVDA or Balabolka take.
//
// Everything is written under HKEY_CURRENT_USER, so it needs no elevation: SAPI merges
// the per-user token store with the machine one, and COM reads per-user classes from
// HKCU\Software\Classes. That makes the registration path testable, which it previously
// was not -- a bug that only appeared once SAPI read a token back could pass every suite.
//
//   token_probe register <path-to-BestspeechSAPI.dll>
//   token_probe speak <out-dir>      -- enumerate through SAPI and speak every voice
//   token_probe switch <out-dir>     -- speak every voice through ONE SpVoice, in turn,
//                                       the way a screen reader changes voice
//   token_probe audio               -- speak one voice per language to the real audio
//                                      device, the way the Speech control panel does
//   token_probe unregister

#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>
#include <cstdio>
#include <string>
#include <vector>

#include "engines.hpp"
#include "voice_attributes.hpp"
#include "voice_registry.hpp"

using namespace Bestspeech;

namespace {

const CLSID CLSID_BestspeechEngine =
    { 0xfd39c483, 0x34ae, 0x411b, { 0xb4, 0x05, 0xdb, 0x8b, 0x21, 0x05, 0x1a, 0xbc } };

std::wstring clsid_string()
{
    wchar_t buf[64];
    StringFromGUID2(CLSID_BestspeechEngine, buf, 64);
    return buf;
}

void set_value(HKEY root, const std::wstring& sub, const wchar_t* name, const std::wstring& value)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, sub.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr)
        != ERROR_SUCCESS) {
        throw std::runtime_error("could not create a registry key");
    }
    RegSetValueExW(key, name, 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

// Per-user COM registration, so the engine can be created without touching HKLM.
void register_com(const std::wstring& dll_path)
{
    const std::wstring base = L"Software\\Classes\\CLSID\\" + clsid_string();
    set_value(HKEY_CURRENT_USER, base, nullptr, L"BestSpeech SAPI5 Engine");
    set_value(HKEY_CURRENT_USER, base + L"\\InprocServer32", nullptr, dll_path);
    set_value(HKEY_CURRENT_USER, base + L"\\InprocServer32", L"ThreadingModel", L"Both");
}

void unregister_com()
{
    const std::wstring base = L"Software\\Classes\\CLSID\\" + clsid_string();
    RegDeleteKeyW(HKEY_CURRENT_USER, (base + L"\\InprocServer32").c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, base.c_str());
}

// Binds an ISpVoice to a wav file so the test never plays audio out loud.
HRESULT make_file_output(const wchar_t* path, ISpStream** out)
{
    *out = nullptr;
    ISpStream* stream = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_ALL,
                                  __uuidof(ISpStream), reinterpret_cast<void**>(&stream));
    if (FAILED(hr)) {
        return hr;
    }

    WAVEFORMATEX wfex = {};
    wfex.wFormatTag = WAVE_FORMAT_PCM;
    wfex.nChannels = 1;
    wfex.nSamplesPerSec = 22050;
    wfex.wBitsPerSample = 16;
    wfex.nBlockAlign = wfex.nChannels * wfex.wBitsPerSample / 8;
    wfex.nAvgBytesPerSec = wfex.nSamplesPerSec * wfex.nBlockAlign;

    hr = stream->BindToFile(path, SPFM_CREATE_ALWAYS, &SPDFID_WaveFormatEx, &wfex, 0);
    if (FAILED(hr)) {
        stream->Release();
        return hr;
    }
    *out = stream;
    return S_OK;
}

long wav_data_bytes(const wchar_t* path)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fclose(f);
    return size > 44 ? size - 44 : 0;
}

struct Row {
    std::wstring token_id;
    std::wstring name;
    HRESULT hr;
    long bytes;
};

int cmd_speak(const wchar_t* out_dir)
{
    ISpObjectTokenCategory* category = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                                  __uuidof(ISpObjectTokenCategory),
                                  reinterpret_cast<void**>(&category));
    if (FAILED(hr)) {
        wprintf(L"could not create the voice category: 0x%08X\n", hr);
        return 1;
    }
    hr = category->SetId(SPCAT_VOICES, FALSE);
    if (FAILED(hr)) {
        wprintf(L"could not open the voice category: 0x%08X\n", hr);
        return 1;
    }

    IEnumSpObjectTokens* tokens = nullptr;
    hr = category->EnumTokens(nullptr, nullptr, &tokens);
    if (FAILED(hr) || !tokens) {
        wprintf(L"could not enumerate voices: 0x%08X\n", hr);
        return 1;
    }

    ULONG total = 0;
    tokens->GetCount(&total);
    wprintf(L"SAPI lists %lu voices in total\n\n", total);

    std::vector<Row> rows;
    ISpObjectToken* token = nullptr;
    while (tokens->Next(1, &token, nullptr) == S_OK && token) {
        LPWSTR id = nullptr;
        if (FAILED(token->GetId(&id)) || !id) {
            token->Release();
            token = nullptr;
            continue;
        }
        const std::wstring token_id = id;
        CoTaskMemFree(id);

        // Only our own voices.
        if (token_id.find(L"BestSpeech_") == std::wstring::npos) {
            token->Release();
            token = nullptr;
            continue;
        }

        const std::wstring short_id = token_id.substr(token_id.rfind(L'\\') + 1);

        // The name a client shows and selects by.
        std::wstring name;
        ISpDataKey* attrs = nullptr;
        if (SUCCEEDED(token->OpenKey(L"Attributes", &attrs)) && attrs) {
            LPWSTR value = nullptr;
            if (SUCCEEDED(attrs->GetStringValue(L"Name", &value)) && value) {
                name = value;
                CoTaskMemFree(value);
            }
            attrs->Release();
        }

        ISpVoice* voice = nullptr;
        Row row{ short_id, name, E_FAIL, -1 };
        hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL,
                              __uuidof(ISpVoice), reinterpret_cast<void**>(&voice));
        if (SUCCEEDED(hr) && voice) {
            hr = voice->SetVoice(token);
            if (SUCCEEDED(hr)) {
                std::wstring wav = std::wstring(out_dir) + L"\\" + short_id + L".wav";
                ISpStream* stream = nullptr;
                if (SUCCEEDED(make_file_output(wav.c_str(), &stream)) && stream) {
                    voice->SetOutput(stream, TRUE);
                    hr = voice->Speak(L"Hello world, this is a test.",
                                      SPF_DEFAULT, nullptr);
                    voice->SetOutput(nullptr, TRUE);
                    stream->Close();
                    stream->Release();
                    row.bytes = wav_data_bytes(wav.c_str());
                }
            }
            row.hr = hr;
            voice->Release();
        }
        rows.push_back(row);

        token->Release();
        token = nullptr;
    }
    tokens->Release();
    category->Release();

    int failed = 0;
    wprintf(L"%-26s %-34s %-12s %s\n", L"token", L"Name attribute", L"Speak", L"audio");
    wprintf(L"%s\n", std::wstring(88, L'-').c_str());
    for (const Row& r : rows) {
        const bool ok = SUCCEEDED(r.hr) && r.bytes > 4000;
        if (!ok) {
            ++failed;
        }
        if (!ok || r.token_id.find(L"_Fred") != std::wstring::npos ||
            r.token_id.find(L'_', 11) == std::wstring::npos) {
            wprintf(L"%-26s %-34s 0x%08X   %ld bytes%s\n",
                    r.token_id.c_str(),
                    r.name.empty() ? L"<EMPTY>" : r.name.c_str(),
                    r.hr, r.bytes, ok ? L"" : L"   <-- FAILED");
        }
    }
    wprintf(L"%s\n", std::wstring(88, L'-').c_str());
    wprintf(L"%zu BestSpeech voices reached through SAPI, %d failed to speak\n",
            rows.size(), failed);
    return failed == 0 ? 0 : 1;
}

// A screen reader keeps one voice object and calls SetVoice on it when the user picks a
// different voice. That reuses a single engine instance across languages, which the
// per-voice test above never does because it builds a fresh SpVoice every time.
int cmd_switch(const wchar_t* out_dir)
{
    ISpObjectTokenCategory* category = nullptr;
    if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                                __uuidof(ISpObjectTokenCategory),
                                reinterpret_cast<void**>(&category))) ||
        FAILED(category->SetId(SPCAT_VOICES, FALSE))) {
        wprintf(L"could not open the voice category\n");
        return 1;
    }

    IEnumSpObjectTokens* tokens = nullptr;
    if (FAILED(category->EnumTokens(nullptr, nullptr, &tokens)) || !tokens) {
        wprintf(L"could not enumerate voices\n");
        return 1;
    }

    ISpVoice* voice = nullptr;
    if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL,
                                __uuidof(ISpVoice), reinterpret_cast<void**>(&voice)))) {
        wprintf(L"could not create a voice\n");
        return 1;
    }

    int failed = 0, spoken = 0;
    ISpObjectToken* token = nullptr;
    while (tokens->Next(1, &token, nullptr) == S_OK && token) {
        LPWSTR id = nullptr;
        if (FAILED(token->GetId(&id)) || !id) {
            token->Release(); token = nullptr; continue;
        }
        const std::wstring token_id = id;
        CoTaskMemFree(id);
        if (token_id.find(L"BestSpeech_") == std::wstring::npos) {
            token->Release(); token = nullptr; continue;
        }
        const std::wstring short_id = token_id.substr(token_id.rfind(L'\\') + 1);

        HRESULT hr = voice->SetVoice(token);
        long bytes = -1;
        if (SUCCEEDED(hr)) {
            const std::wstring wav = std::wstring(out_dir) + L"\\" + short_id + L".wav";
            ISpStream* stream = nullptr;
            if (SUCCEEDED(make_file_output(wav.c_str(), &stream)) && stream) {
                voice->SetOutput(stream, TRUE);
                hr = voice->Speak(L"Hello world, this is a test.", SPF_DEFAULT, nullptr);
                voice->SetOutput(nullptr, TRUE);
                stream->Close();
                stream->Release();
                bytes = wav_data_bytes(wav.c_str());
            }
        }
        ++spoken;
        const bool ok = SUCCEEDED(hr) && bytes > 4000;
        if (!ok) {
            ++failed;
            wprintf(L"%-26s SetVoice/Speak 0x%08X  %ld bytes   <-- FAILED\n",
                    short_id.c_str(), hr, bytes);
        }
        token->Release();
        token = nullptr;
    }
    voice->Release();
    tokens->Release();
    category->Release();

    wprintf(L"%d voices spoken through a single reused voice object, %d failed\n",
            spoken, failed);
    return failed == 0 ? 0 : 1;
}

// Everything above writes to a file stream. The Speech control panel, and any ordinary
// application, speaks to the audio device instead -- a different SAPI audio object, with
// its own format negotiation. This walks one voice per language down that path and times
// each call, so a voice that returns instantly instead of speaking stands out.
// Every voice of one language, in sequence, through one process, to the real audio
// device. That is what the Speech control panel does as you step down its voice list,
// and it is the one combination the file-stream tests never cover.
int cmd_audio_voices(const char* engine_id)
{
    const int e = engine_by_id(engine_id);
    if (e < 0) {
        wprintf(L"unknown engine\n");
        return 2;
    }
    int first = 0;
    for (int k = 0; k < e; ++k) {
        first += engines[k].voice_count;
    }

    int failed = 0;
    for (int vi = 0; vi < engines[e].voice_count; ++vi) {
        const sapi::voice_attributes voice(first + vi);
        const std::wstring id = std::wstring(L"HKEY_LOCAL_MACHINE\\") +
                                sapi::voices_path + L"\\" + voice.get_token_id();
        ISpObjectToken* token = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_SpObjectToken, nullptr, CLSCTX_ALL,
                                      __uuidof(ISpObjectToken),
                                      reinterpret_cast<void**>(&token));
        if (SUCCEEDED(hr)) hr = token->SetId(nullptr, id.c_str(), FALSE);
        if (FAILED(hr) || !token) {
            wprintf(L"%-28s token load 0x%08X\n", voice.get_token_id().c_str(), hr);
            ++failed;
            if (token) token->Release();
            continue;
        }

        ISpVoice* spvoice = nullptr;
        DWORD elapsed = 0;
        hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL,
                              __uuidof(ISpVoice), reinterpret_cast<void**>(&spvoice));
        if (SUCCEEDED(hr) && spvoice) {
            hr = spvoice->SetVoice(token);
            if (SUCCEEDED(hr)) {
                const DWORD start = GetTickCount();
                hr = spvoice->Speak(L"One two three four.", SPF_DEFAULT, nullptr);
                elapsed = GetTickCount() - start;
            }
            spvoice->Release();
        }
        token->Release();

        const bool ok = SUCCEEDED(hr) && elapsed > 300;
        if (!ok) ++failed;
        wprintf(L"%-28s 0x%08X %5lu ms%s\n", voice.get_token_id().c_str(), hr, elapsed,
                ok ? L"" : L"   <-- NO AUDIO");
    }
    wprintf(L"%d of %d voices produced no audio through the device\n",
            failed, engines[e].voice_count);
    return failed == 0 ? 0 : 1;
}

int cmd_audio()
{
    ISpObjectTokenCategory* category = nullptr;
    if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                                __uuidof(ISpObjectTokenCategory),
                                reinterpret_cast<void**>(&category))) ||
        FAILED(category->SetId(SPCAT_VOICES, FALSE))) {
        wprintf(L"could not open the voice category\n");
        return 1;
    }

    int failed = 0;
    for (int e = 0; e < engine_count; ++e) {
        const sapi::voice_attributes v(e == 0 ? 0 : -1);
        // first voice of this engine
        int index = 0;
        for (int k = 0; k < e; ++k) {
            index += engines[k].voice_count;
        }
        const sapi::voice_attributes voice(index);

        const std::wstring id = std::wstring(L"HKEY_LOCAL_MACHINE\\") +
                                sapi::voices_path + L"\\" + voice.get_token_id();

        ISpObjectToken* token = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_SpObjectToken, nullptr, CLSCTX_ALL,
                                      __uuidof(ISpObjectToken),
                                      reinterpret_cast<void**>(&token));
        if (SUCCEEDED(hr)) {
            hr = token->SetId(nullptr, id.c_str(), FALSE);
        }
        if (FAILED(hr) || !token) {
            wprintf(L"%-10s could not load token: 0x%08X\n", engines[e].display, hr);
            ++failed;
            if (token) token->Release();
            continue;
        }

        ISpVoice* spvoice = nullptr;
        hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL,
                              __uuidof(ISpVoice), reinterpret_cast<void**>(&spvoice));
        DWORD elapsed = 0;
        if (SUCCEEDED(hr) && spvoice) {
            hr = spvoice->SetVoice(token);
            if (SUCCEEDED(hr)) {
                const DWORD start = GetTickCount();
                hr = spvoice->Speak(L"One two three four.", SPF_DEFAULT, nullptr);
                elapsed = GetTickCount() - start;
            }
            spvoice->Release();
        }
        token->Release();

        // A real utterance of that phrase takes on the order of a second; anything that
        // returns at once produced no audio.
        const bool ok = SUCCEEDED(hr) && elapsed > 300;
        if (!ok) ++failed;
        wprintf(L"%-24s %-28s 0x%08X %5lu ms%s\n",
                engines[e].display, voice.get_token_id().c_str(), hr, elapsed,
                ok ? L"" : L"   <-- NO AUDIO");
    }
    category->Release();
    wprintf(L"%d of %d languages produced no audio through the device\n",
            failed, engine_count);
    return failed == 0 ? 0 : 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) {
        wprintf(L"usage: token_probe register <dll> | speak <out-dir> | unregister\n");
        return 2;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int rc = 2;

    try {
        if (wcscmp(argv[1], L"register") == 0 && argc >= 3) {
            wchar_t full[MAX_PATH];
            GetFullPathNameW(argv[2], MAX_PATH, full, nullptr);
            register_com(full);
            sapi::write_voice_tokens(HKEY_CURRENT_USER, clsid_string());
            wprintf(L"registered %d voices for the current user, engine %s\n",
                    total_token_count(), full);
            rc = 0;
        } else if (wcscmp(argv[1], L"speak") == 0 && argc >= 3) {
            CreateDirectoryW(argv[2], nullptr);
            rc = cmd_speak(argv[2]);
        } else if (wcscmp(argv[1], L"switch") == 0 && argc >= 3) {
            CreateDirectoryW(argv[2], nullptr);
            rc = cmd_switch(argv[2]);
        } else if (wcscmp(argv[1], L"audio") == 0) {
            if (argc >= 3) {
                char buf[32] = {};
                WideCharToMultiByte(CP_ACP, 0, argv[2], -1, buf, sizeof(buf) - 1, nullptr, nullptr);
                rc = cmd_audio_voices(buf);
            } else {
                rc = cmd_audio();
            }
        } else if (wcscmp(argv[1], L"unregister") == 0) {
            sapi::remove_voice_tokens(HKEY_CURRENT_USER);
            unregister_com();
            wprintf(L"unregistered\n");
            rc = 0;
        } else {
            wprintf(L"unknown command\n");
        }
    }
    catch (const std::exception& e) {
        printf("failed: %s\n", e.what());
        rc = 1;
    }

    CoUninitialize();
    return rc;
}
