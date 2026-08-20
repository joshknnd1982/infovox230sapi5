// Infovox230SapiTest -- exercises the engine the way a real application does:
// through SAPI5 itself, not through our own interfaces. Built for both
// architectures, because the 64-bit path crosses a process boundary the 32-bit
// one also crosses, and only a real SAPI host proves the whole chain.
//
//   Infovox230SapiTest list
//   Infovox230SapiTest say [text] [--voice NAME]      speak aloud
//   Infovox230SapiTest speak <out.wav> [text] [--voice NAME] [--rate N]
//                           [--volume N] [--xml]
//   Infovox230SapiTest all <outdir>
//
// Exit code 0 means audio was produced.

#include <windows.h>
#include <sapi.h>
#include <sperror.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Voice {
    std::wstring name;
    std::wstring language;
    std::wstring gender;
    std::wstring vendor;
    ISpObjectToken* token = nullptr;
};

std::wstring attribute(ISpObjectToken* token, const wchar_t* name)
{
    ISpDataKey* attributes = nullptr;
    if (FAILED(token->OpenKey(L"Attributes", &attributes)) || !attributes) {
        return std::wstring();
    }
    LPWSTR value = nullptr;
    std::wstring out;
    if (SUCCEEDED(attributes->GetStringValue(name, &value)) && value) {
        out = value;
        CoTaskMemFree(value);
    }
    attributes->Release();
    return out;
}

bool collect_voices(std::vector<Voice>* out, bool infovox_only)
{
    ISpObjectTokenCategory* category = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                                  IID_ISpObjectTokenCategory,
                                  reinterpret_cast<void**>(&category));
    if (FAILED(hr)) {
        wprintf(L"CoCreateInstance(SpObjectTokenCategory) failed: 0x%08lX\n", hr);
        return false;
    }
    hr = category->SetId(SPCAT_VOICES, FALSE);
    if (FAILED(hr)) {
        wprintf(L"SetId(SPCAT_VOICES) failed: 0x%08lX\n", hr);
        category->Release();
        return false;
    }

    IEnumSpObjectTokens* tokens = nullptr;
    hr = category->EnumTokens(nullptr, nullptr, &tokens);
    category->Release();
    if (FAILED(hr) || !tokens) {
        wprintf(L"EnumTokens failed: 0x%08lX\n", hr);
        return false;
    }

    for (;;) {
        ISpObjectToken* token = nullptr;
        ULONG fetched = 0;
        if (FAILED(tokens->Next(1, &token, &fetched)) || fetched == 0 || !token) {
            break;
        }
        Voice v;
        v.name = attribute(token, L"Name");
        v.language = attribute(token, L"Language");
        v.gender = attribute(token, L"Gender");
        v.vendor = attribute(token, L"Vendor");
        if (infovox_only && _wcsicmp(v.vendor.c_str(), L"Infovox") != 0) {
            token->Release();
            continue;
        }
        v.token = token;
        out->push_back(v);
    }
    tokens->Release();
    return true;
}

// Binds the voice's output to a wav file so a run leaves something that can be
// listened to and measured.
ISpStream* open_wav(ISpVoice* voice, const std::wstring& path)
{
    ISpStreamFormat* current = nullptr;
    GUID format_id = SPDFID_WaveFormatEx;
    WAVEFORMATEX* wfx = nullptr;
    if (SUCCEEDED(voice->GetOutputStream(&current)) && current) {
        current->GetFormat(&format_id, &wfx);
        current->Release();
    }

    WAVEFORMATEX fallback = {};
    if (!wfx) {
        fallback.wFormatTag = WAVE_FORMAT_PCM;
        fallback.nChannels = 1;
        fallback.nSamplesPerSec = 16000;
        fallback.wBitsPerSample = 16;
        fallback.nBlockAlign = 2;
        fallback.nAvgBytesPerSec = 32000;
        wfx = &fallback;
    }

    ISpStream* stream = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_ALL, IID_ISpStream,
                                  reinterpret_cast<void**>(&stream));
    if (FAILED(hr)) {
        wprintf(L"CoCreateInstance(SpStream) failed: 0x%08lX\n", hr);
        return nullptr;
    }
    hr = stream->BindToFile(path.c_str(), SPFM_CREATE_ALWAYS, &SPDFID_WaveFormatEx, wfx,
                            SPFEI_ALL_EVENTS);
    if (wfx != &fallback) {
        CoTaskMemFree(wfx);
    }
    if (FAILED(hr)) {
        wprintf(L"BindToFile(%s) failed: 0x%08lX\n", path.c_str(), hr);
        stream->Release();
        return nullptr;
    }
    return stream;
}

struct EventTally {
    int words = 0;
    int sentences = 0;
    int bookmarks = 0;
    int visemes = 0;
    int other = 0;
};

void drain_events(ISpVoice* voice, EventTally* tally, bool verbose)
{
    for (;;) {
        SPEVENT event = {};
        ULONG fetched = 0;
        if (FAILED(voice->GetEvents(1, &event, &fetched)) || fetched == 0) {
            return;
        }
        switch (event.eEventId) {
            case SPEI_WORD_BOUNDARY:
                ++tally->words;
                if (verbose) {
                    wprintf(L"    word   at audio %llu, text offset %lu length %lu\n",
                            event.ullAudioStreamOffset,
                            static_cast<unsigned long>(event.lParam),
                            static_cast<unsigned long>(event.wParam));
                }
                break;
            case SPEI_SENTENCE_BOUNDARY:
                ++tally->sentences;
                break;
            case SPEI_TTS_BOOKMARK:
                ++tally->bookmarks;
                if (verbose) {
                    wprintf(L"    mark   at audio %llu: \"%s\"\n", event.ullAudioStreamOffset,
                            event.lParam ? reinterpret_cast<LPCWSTR>(event.lParam) : L"");
                }
                break;
            case SPEI_VISEME:
                ++tally->visemes;
                break;
            default:
                ++tally->other;
                break;
        }
        if (event.elParamType == SPET_LPARAM_IS_STRING && event.lParam) {
            CoTaskMemFree(reinterpret_cast<void*>(event.lParam));
        }
    }
}

// Creates a directory and any missing parents. CreateDirectory alone fails on a
// nested path, and the failure then shows up much later as "NO AUDIO", which
// sends you looking in entirely the wrong place.
bool make_directory(const std::wstring& path)
{
    if (path.empty()) {
        return false;
    }
    if (CreateDirectoryW(path.c_str(), nullptr) ||
        GetLastError() == ERROR_ALREADY_EXISTS) {
        return true;
    }
    const size_t cut = path.find_last_of(L"\\/");
    if (cut == std::wstring::npos || cut == 0) {
        return false;
    }
    if (!make_directory(path.substr(0, cut))) {
        return false;
    }
    return CreateDirectoryW(path.c_str(), nullptr) ||
           GetLastError() == ERROR_ALREADY_EXISTS;
}

long file_size(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return -1;
    }
    return static_cast<long>(data.nFileSizeLow);
}

void usage()
{
    wprintf(L"Infovox230SapiTest -- drives the voices through SAPI5\n"
            L"\n"
            L"  Infovox230SapiTest list\n"
            L"  Infovox230SapiTest speak <out.wav> [text] [--voice NAME]\n"
            L"  Infovox230SapiTest all <outdir>\n"
            L"\n"
            L"  --rate N     -10..10\n"
            L"  --volume N   0..100\n"
            L"  --xml        treat the text as SAPI5 XML\n"
            L"  --verbose    print every event\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) {
        usage();
        return 1;
    }

    std::wstring want_voice;
    long rate = 0;
    long volume = 100;
    bool xml = false;
    bool verbose = false;
    std::vector<std::wstring> positional;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--voice" && i + 1 < argc) {
            want_voice = argv[++i];
        } else if (a == L"--rate" && i + 1 < argc) {
            rate = _wtol(argv[++i]);
        } else if (a == L"--volume" && i + 1 < argc) {
            volume = _wtol(argv[++i]);
        } else if (a == L"--xml") {
            xml = true;
        } else if (a == L"--verbose") {
            verbose = true;
        } else {
            positional.push_back(a);
        }
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        wprintf(L"CoInitializeEx failed: 0x%08lX\n", hr);
        return 2;
    }

    wprintf(L"host is %d-bit\n", static_cast<int>(sizeof(void*) * 8));

    std::vector<Voice> voices;
    if (!collect_voices(&voices, true) || voices.empty()) {
        wprintf(L"No Infovox voices are visible to SAPI5.\n"
                L"Register the engine first:  regsvr32 Infovox230SAPI.dll\n");
        CoUninitialize();
        return 3;
    }

    const std::wstring command = positional.empty() ? L"list" : positional[0];

    if (command == L"list") {
        wprintf(L"%u Infovox voices visible to SAPI5:\n", static_cast<unsigned>(voices.size()));
        for (const Voice& v : voices) {
            wprintf(L"  %-36s lang=%-6s %-7s\n", v.name.c_str(), v.language.c_str(),
                    v.gender.c_str());
        }
        for (Voice& v : voices) {
            v.token->Release();
        }
        CoUninitialize();
        return 0;
    }

    ISpVoice* voice = nullptr;
    hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice,
                          reinterpret_cast<void**>(&voice));
    if (FAILED(hr)) {
        wprintf(L"CoCreateInstance(SpVoice) failed: 0x%08lX\n", hr);
        CoUninitialize();
        return 4;
    }
    voice->SetInterest(SPFEI(SPEI_WORD_BOUNDARY) | SPFEI(SPEI_SENTENCE_BOUNDARY) |
                           SPFEI(SPEI_TTS_BOOKMARK) | SPFEI(SPEI_VISEME),
                       SPFEI(SPEI_WORD_BOUNDARY) | SPFEI(SPEI_SENTENCE_BOUNDARY) |
                           SPFEI(SPEI_TTS_BOOKMARK) | SPFEI(SPEI_VISEME));

    auto speak_to = [&](const Voice& v, const std::wstring& text,
                        const std::wstring& out_path) -> bool {
        HRESULT rc = voice->SetVoice(v.token);
        if (FAILED(rc)) {
            wprintf(L"SetVoice(%s) failed: 0x%08lX\n", v.name.c_str(), rc);
            return false;
        }
        voice->SetRate(rate);
        voice->SetVolume(static_cast<USHORT>(volume));

        ISpStream* stream = open_wav(voice, out_path);
        if (!stream) {
            return false;
        }
        voice->SetOutput(stream, TRUE);

        EventTally tally;
        const DWORD started = GetTickCount();
        rc = voice->Speak(text.c_str(), SPF_ASYNC | (xml ? SPF_IS_XML : SPF_IS_NOT_XML), nullptr);
        if (FAILED(rc)) {
            wprintf(L"Speak failed: 0x%08lX\n", rc);
            stream->Release();
            return false;
        }
        while (voice->WaitUntilDone(50) == S_FALSE) {
            drain_events(voice, &tally, verbose);
        }
        drain_events(voice, &tally, verbose);
        const DWORD elapsed = GetTickCount() - started;

        voice->SetOutput(nullptr, FALSE);
        stream->Close();
        stream->Release();

        const long bytes = file_size(out_path);
        wprintf(L"%-36s %8ld bytes  %5.2fs synth  words=%d sentences=%d marks=%d visemes=%d %s\n",
                v.name.c_str(), bytes, elapsed / 1000.0, tally.words, tally.sentences,
                tally.bookmarks, tally.visemes, bytes > 44 ? L"ok" : L"NO AUDIO");
        return bytes > 44;
    };

    int exit_code = 0;

    if (command == L"rapid") {
        // What arrowing through a document feels like: each keypress purges what
        // is being said and starts the next line. The number that matters is how
        // long the new Speak call takes to return, because SAPI cannot start it
        // until the engine has let go of the previous one -- that delay is the
        // lag between pressing a key and hearing the result.
        const int count = positional.size() > 1 ? _wtoi(positional[1].c_str()) : 10;
        const DWORD gap = positional.size() > 2 ? static_cast<DWORD>(_wtoi(positional[2].c_str()))
                                                : 150;
        const Voice* chosen = &voices[0];
        if (!want_voice.empty()) {
            for (const Voice& v : voices) {
                if (_wcsicmp(v.name.c_str(), want_voice.c_str()) == 0) {
                    chosen = &v;
                    break;
                }
            }
        }
        voice->SetVoice(chosen->token);
        voice->SetRate(rate);
        voice->SetVolume(static_cast<USHORT>(volume));

        wprintf(L"%d lines, %lu ms between keypresses, voice %s\n", count,
                static_cast<unsigned long>(gap), chosen->name.c_str());
        double worst = 0;
        double total = 0;
        for (int i = 0; i < count; ++i) {
            wchar_t line[128];
            _snwprintf_s(line, _countof(line), _TRUNCATE,
                         L"Line %d of the document, with enough words on it to be worth "
                         L"interrupting.",
                         i + 1);
            const ULONGLONG t0 = GetTickCount64();
            voice->Speak(line, SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_NOT_XML, nullptr);
            const double ms = static_cast<double>(GetTickCount64() - t0);
            total += ms;
            worst = (std::max)(worst, ms);
            if (verbose) {
                wprintf(L"  line %2d: %5.0f ms to take over\n", i + 1, ms);
            }
            Sleep(gap);
        }
        voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
        wprintf(L"time to interrupt and start the next line: average %.0f ms, worst %.0f ms\n",
                total / count, worst);
        exit_code = worst > 250 ? 9 : 0;
    } else if (command == L"say") {
        // Speaks aloud through the default audio device. This is what the
        // installer offers at the end: for someone who cannot see the wizard,
        // hearing the voice is the confirmation that it installed correctly.
        const Voice* chosen = &voices[0];
        if (!want_voice.empty()) {
            for (const Voice& v : voices) {
                if (_wcsicmp(v.name.c_str(), want_voice.c_str()) == 0) {
                    chosen = &v;
                    break;
                }
            }
        }
        const std::wstring text =
            positional.size() > 1
                ? positional[1]
                : L"Infovox two thirty is installed and working. Sixty voices in twelve "
                  L"languages are now available to any program that uses Windows speech.";
        hr = voice->SetVoice(chosen->token);
        if (FAILED(hr)) {
            wprintf(L"SetVoice failed: 0x%08lX\n", hr);
            exit_code = 6;
        } else {
            voice->SetRate(rate);
            voice->SetVolume(static_cast<USHORT>(volume));
            wprintf(L"speaking with %s...\n", chosen->name.c_str());

            // Asynchronous, so progress events can be collected while the audio
            // plays. Events are tied to the playback position, which is exactly
            // what a program highlighting the current word needs -- and only a
            // real audio device produces them, so this is the path that proves
            // they work.
            EventTally tally;
            hr = voice->Speak(text.c_str(), SPF_ASYNC | (xml ? SPF_IS_XML : SPF_IS_NOT_XML),
                              nullptr);
            if (FAILED(hr)) {
                wprintf(L"Speak failed: 0x%08lX\n", hr);
                exit_code = 6;
            } else {
                while (voice->WaitUntilDone(50) == S_FALSE) {
                    drain_events(voice, &tally, verbose);
                }
                drain_events(voice, &tally, verbose);
                wprintf(L"words=%d sentences=%d marks=%d visemes=%d\n", tally.words,
                        tally.sentences, tally.bookmarks, tally.visemes);
            }
        }
    } else if (command == L"speak") {
        if (positional.size() < 2) {
            usage();
            exit_code = 1;
        } else {
            const std::wstring out = positional[1];
            const std::wstring text =
                positional.size() > 2
                    ? positional[2]
                    : L"Hello. This is the Infovox two thirty engine, speaking through "
                      L"Microsoft SAPI five.";
            const Voice* chosen = &voices[0];
            if (!want_voice.empty()) {
                chosen = nullptr;
                for (const Voice& v : voices) {
                    if (_wcsicmp(v.name.c_str(), want_voice.c_str()) == 0) {
                        chosen = &v;
                        break;
                    }
                }
                if (!chosen) {
                    wprintf(L"No voice called \"%s\".\n", want_voice.c_str());
                    exit_code = 5;
                }
            }
            if (chosen) {
                exit_code = speak_to(*chosen, text, out) ? 0 : 6;
            }
        }
    } else if (command == L"all") {
        if (positional.size() < 2) {
            usage();
            exit_code = 1;
        } else {
            const std::wstring dir = positional[1];
            if (!make_directory(dir)) {
                wprintf(L"cannot create %s\n", dir.c_str());
                exit_code = 8;
            }
            int ok = 0;
            int failed = 0;
            for (const Voice& v : voices) {
                std::wstring safe = v.name;
                for (wchar_t& ch : safe) {
                    if (ch == L' ') {
                        ch = L'_';
                    }
                }
                if (speak_to(v, L"This voice is speaking through SAPI five.",
                             dir + L"\\" + safe + L".wav")) {
                    ++ok;
                } else {
                    ++failed;
                }
            }
            wprintf(L"\n%d voices produced audio, %d failed.\n", ok, failed);
            exit_code = failed ? 7 : 0;
        }
    } else {
        usage();
        exit_code = 1;
    }

    for (Voice& v : voices) {
        v.token->Release();
    }
    voice->Release();
    CoUninitialize();
    return exit_code;
}
