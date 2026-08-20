// BestSpeechDiagnostics.exe -- run this when a voice will not speak.
//
// Walks every installed BestSpeech voice the way an ordinary application does: it asks
// SAPI for the voice by its registry id, selects it, and speaks a phrase into a wav
// file. Then it writes a plain text report saying which voices produced audio, which
// produced silence, and which could not be loaded at all.
//
// It touches nothing: no registration, no settings, no default voice. Output goes to
// %LOCALAPPDATA%\BestSpeech\diagnostics.txt alongside the engine's own log.
//
//   BestSpeechDiagnostics.exe            report on every voice
//   BestSpeechDiagnostics.exe <engine>   only that language, e.g. "por"

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

FILE* g_report = nullptr;

void report(const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vwprintf(fmt, args);
    va_end(args);
    if (g_report) {
        va_start(args, fmt);
        vfwprintf(g_report, fmt, args);
        va_end(args);
    }
}

std::wstring local_dir()
{
    wchar_t base[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH) == 0) {
        GetTempPathW(MAX_PATH, base);
    }
    std::wstring dir = std::wstring(base) + L"\\BestSpeech";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

// Loads a token straight from the machine store, without registering anything.
HRESULT load_token(const sapi::voice_attributes& v, ISpObjectToken** out)
{
    *out = nullptr;
    const std::wstring id = std::wstring(L"HKEY_LOCAL_MACHINE\\") +
                            sapi::voices_path + L"\\" + v.get_token_id();

    ISpObjectToken* token = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_SpObjectToken, nullptr, CLSCTX_ALL,
                                  __uuidof(ISpObjectToken), reinterpret_cast<void**>(&token));
    if (FAILED(hr)) {
        return hr;
    }
    hr = token->SetId(nullptr, id.c_str(), FALSE);
    if (FAILED(hr)) {
        token->Release();
        return hr;
    }
    *out = token;
    return S_OK;
}

HRESULT bind_wav(const wchar_t* path, ISpStream** out)
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
    wfex.nBlockAlign = 2;
    wfex.nAvgBytesPerSec = wfex.nSamplesPerSec * 2;
    hr = stream->BindToFile(path, SPFM_CREATE_ALWAYS, &SPDFID_WaveFormatEx, &wfex, 0);
    if (FAILED(hr)) {
        stream->Release();
        return hr;
    }
    *out = stream;
    return S_OK;
}

// Loudest sample in the file, so a wav full of digital silence is not mistaken for speech.
int wav_peak(const wchar_t* path)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) {
        return -1;
    }
    fseek(f, 44, SEEK_SET);
    int peak = 0;
    short buf[4096];
    size_t n;
    while ((n = fread(buf, sizeof(short), 4096, f)) > 0) {
        for (size_t i = 0; i < n; ++i) {
            const int v = buf[i] < 0 ? -buf[i] : buf[i];
            if (v > peak) peak = v;
        }
    }
    fclose(f);
    return peak;
}

}  // namespace

int wmain(int argc, wchar_t** argv)
{
    const std::string only = (argc > 1) ? [&] {
        char buf[32] = {};
        WideCharToMultiByte(CP_ACP, 0, argv[1], -1, buf, sizeof(buf) - 1, nullptr, nullptr);
        return std::string(buf);
    }() : std::string();

    const std::wstring dir = local_dir();
    const std::wstring report_path = dir + L"\\diagnostics.txt";
    const std::wstring wav_dir = dir + L"\\diagnostic_audio";
    CreateDirectoryW(wav_dir.c_str(), nullptr);
    _wfopen_s(&g_report, report_path.c_str(), L"w, ccs=UTF-8");

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    SYSTEMTIME now;
    GetLocalTime(&now);
    report(L"BestSpeech diagnostics  %04d-%02d-%02d %02d:%02d\n",
           now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute);
    report(L"This program is %d-bit, so it reads the %d-bit voice registrations.\n\n",
           (int)(sizeof(void*) * 8), (int)(sizeof(void*) * 8));

    int silent = 0, unloadable = 0, spoke = 0;
    std::vector<std::wstring> problems;

    for (int e = 0; e < engine_count; ++e) {
        if (!only.empty() && only != engines[e].id) {
            continue;
        }
        int first = 0;
        for (int k = 0; k < e; ++k) {
            first += engines[k].voice_count;
        }

        report(L"%s\n", engines[e].display);

        for (int vi = 0; vi < engines[e].voice_count; ++vi) {
            const sapi::voice_attributes v(first + vi);
            const std::wstring id = v.get_token_id();

            ISpObjectToken* token = nullptr;
            HRESULT hr = load_token(v, &token);
            if (FAILED(hr) || !token) {
                ++unloadable;
                problems.push_back(id + L": not registered (0x" + std::to_wstring(hr) + L")");
                report(L"   %-28s NOT REGISTERED  0x%08X\n", id.c_str(), hr);
                continue;
            }

            ISpVoice* voice = nullptr;
            int peak = -1;
            hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL,
                                  __uuidof(ISpVoice), reinterpret_cast<void**>(&voice));
            if (SUCCEEDED(hr) && voice) {
                hr = voice->SetVoice(token);
                if (SUCCEEDED(hr)) {
                    const std::wstring wav = wav_dir + L"\\" + id + L".wav";
                    ISpStream* stream = nullptr;
                    if (SUCCEEDED(bind_wav(wav.c_str(), &stream)) && stream) {
                        voice->SetOutput(stream, TRUE);
                        hr = voice->Speak(L"One two three four five.", SPF_DEFAULT, nullptr);
                        voice->SetOutput(nullptr, TRUE);
                        stream->Close();
                        stream->Release();
                        peak = wav_peak(wav.c_str());
                    }
                }
                voice->Release();
            }
            token->Release();

            if (FAILED(hr)) {
                ++silent;
                problems.push_back(id + L": Speak failed");
                report(L"   %-28s SPEAK FAILED    0x%08X\n", id.c_str(), hr);
            } else if (peak < 300) {
                ++silent;
                problems.push_back(id + L": silent");
                report(L"   %-28s SILENT          peak %d\n", id.c_str(), peak);
            } else {
                ++spoke;
                report(L"   %-28s ok              peak %d\n", id.c_str(), peak);
            }
        }
        report(L"\n");
    }

    report(L"----------------------------------------------------------------\n");
    report(L"%d voices spoke, %d were silent, %d were not registered.\n",
           spoke, silent, unloadable);
    if (!problems.empty()) {
        report(L"\nProblems:\n");
        for (const std::wstring& p : problems) {
            report(L"   %s\n", p.c_str());
        }
    }
    report(L"\nAudio written to: %s\n", wav_dir.c_str());
    report(L"Engine log:       %s\\bestspeech.log\n", dir.c_str());
    report(L"This report:      %s\n", report_path.c_str());

    CoUninitialize();
    if (g_report) {
        fclose(g_report);
    }
    wprintf(L"\nReport saved to %s\n", report_path.c_str());
    return (silent == 0 && unloadable == 0) ? 0 : 1;
}
