// Infovox230Diag -- the tool to reach for when the voices are not working.
//
// It can drive the engine two ways, so a fault can be placed:
//   * directly, in this process (32-bit build only), which tests the engine,
//     the in-memory registry and the voice catalogue with nothing else in the
//     way;
//   * through the worker over the pipe (both builds), which tests exactly what
//     the SAPI5 engine does, including the 64-bit to 32-bit crossing.
//
//   Infovox230Diag list                        every voice in the catalogue
//   Infovox230Diag registry                    prove the engine reads no registry
//   Infovox230Diag speak <out.wav> [text]      direct, one voice
//   Infovox230Diag all <outdir>                direct, every voice
//   Infovox230Diag worker <out.wav> [text]     through the worker
//   Infovox230Diag workerall <outdir>          through the worker, every voice
//   Infovox230Diag register | unregister       publish the voices to SAPI5
//
// Options: --voice NAME, --rate N, --pitch N, --volume N (0-100),
//          --engine-dir DIR

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ivx_catalog.h"
#include "ivx_client.h"
#include "ivx_log.h"
#include "ivx_paths.h"

#ifdef IVX_HAVE_ENGINE
#include "ivx_engine.h"
#include "ivx_vregistry.h"
#endif

namespace {

struct WavFormat {
    unsigned long samples_per_sec = 16000;
    unsigned long avg_bytes_per_sec = 32000;
    unsigned short channels = 1;
    unsigned short bits = 16;
};

struct WavWriter {
    FILE* f = nullptr;
    unsigned long bytes = 0;

    bool open(const std::wstring& path, const WavFormat& fmt)
    {
        if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) {
            return false;
        }
        // Placeholder header; the two sizes are patched in close().
        const unsigned long block = fmt.channels * (fmt.bits / 8u);
        unsigned char header[44] = {};
        memcpy(header, "RIFF", 4);
        memcpy(header + 8, "WAVEfmt ", 8);
        *reinterpret_cast<unsigned long*>(header + 16) = 16;
        *reinterpret_cast<unsigned short*>(header + 20) = 1;  // PCM
        *reinterpret_cast<unsigned short*>(header + 22) = fmt.channels;
        *reinterpret_cast<unsigned long*>(header + 24) = fmt.samples_per_sec;
        *reinterpret_cast<unsigned long*>(header + 28) = fmt.avg_bytes_per_sec;
        *reinterpret_cast<unsigned short*>(header + 32) = static_cast<unsigned short>(block);
        *reinterpret_cast<unsigned short*>(header + 34) = fmt.bits;
        memcpy(header + 36, "data", 4);
        fwrite(header, 1, sizeof(header), f);
        return true;
    }

    void write(const void* data, unsigned long n)
    {
        fwrite(data, 1, n, f);
        bytes += n;
    }

    void close()
    {
        if (!f) {
            return;
        }
        const unsigned long riff = 36 + bytes;
        fseek(f, 4, SEEK_SET);
        fwrite(&riff, 4, 1, f);
        fseek(f, 40, SEEK_SET);
        fwrite(&bytes, 4, 1, f);
        fclose(f);
        f = nullptr;
    }
};

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

std::wstring widen(const std::string& s)
{
    if (s.empty()) {
        return std::wstring();
    }
    const int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n);
    return out;
}

std::wstring file_name_for(const ivx::Voice& v)
{
    std::wstring safe = widen(v.display_name);
    for (wchar_t& ch : safe) {
        if (ch == L' ') {
            ch = L'_';
        }
    }
    return safe;
}

// One sentence per language, so a listener can tell the right rule file is in
// use rather than English phonetics applied to foreign words.
const wchar_t* sample_for(unsigned short lcid)
{
    struct Sample {
        unsigned short lcid;
        const wchar_t* text;
    };
    static const Sample samples[] = {
        {0x0409, L"Hello. This is the American English voice."},
        {0x0809, L"Good day. This is the British English voice."},
        {0x0406, L"Goddag. Dette er den danske stemme."},
        {0x0413, L"Hallo. Dit is de Nederlandse stem."},
        {0x040B, L"Hyvaa paivaa. Tama on suomenkielinen aani."},
        {0x040C, L"Bonjour. Ceci est la voix francaise."},
        {0x0407, L"Guten Tag. Dies ist die deutsche Stimme."},
        {0x040F, L"Godan dag. Thetta er islenska roeddin."},
        {0x0410, L"Buongiorno. Questa e la voce italiana."},
        {0x0414, L"God dag. Dette er den norske stemmen."},
        {0x040A, L"Hola. Esta es la voz en castellano."},
        {0x041D, L"God dag. Det haer aer den svenska roesten."},
    };
    for (const Sample& s : samples) {
        if (s.lcid == lcid) {
            return s.text;
        }
    }
    return L"Hello. This is a test.";
}

void print_usage()
{
    printf("Infovox 230 SAPI5 diagnostics\n"
           "\n"
           "  Infovox230Diag list                     every voice in the catalogue\n"
#ifdef IVX_HAVE_ENGINE
           "  Infovox230Diag registry                 prove the engine reads no registry\n"
           "  Infovox230Diag speak <out.wav> [text]   drive the engine directly\n"
           "  Infovox230Diag all <outdir>             directly, every voice\n"
#endif
           "  Infovox230Diag worker <out.wav> [text]  through the worker, as SAPI5 does\n"
           "  Infovox230Diag workerall <outdir>       through the worker, every voice\n"
           "  Infovox230Diag stop                     stop the worker\n"
           "\n"
           "  --voice NAME   which voice to use\n"
           "  --rate N       -10..10 through the worker, words per minute directly\n"
           "  --pitch N      -10..10 through the worker, hertz directly\n"
           "  --volume N     0..100\n"
           "  --verbose      print every progress event\n"
           "  --engine-dir D folder holding Ivx230nt.dll\n");
}

struct Options {
    std::wstring engine_dir;
    std::wstring voice;
    int rate = 0;
    int pitch = 0;
    int volume = 100;
    bool rate_given = false;
    bool pitch_given = false;
    bool verbose = false;
};

// --- through the worker, which is the path the SAPI5 engine takes ------------

int run_worker(const std::wstring& command, const std::vector<std::wstring>& positional,
               const Options& opt, const ivx::Catalog& catalog)
{
    ivx::WorkerClient client;
    ivx::HelloResponse hello = {};
    if (!client.hello(&hello)) {
        printf("Could not reach the worker. It is started on demand from\n  %S\n"
               "See the log for why that failed.\n",
               ivx::server_exe_path().c_str());
        return 8;
    }
    printf("worker: protocol %u, %u voices, %lu Hz %u-bit %u channel(s)\n\n", hello.version,
           hello.voice_count, static_cast<unsigned long>(hello.samples_per_sec), hello.bits,
           hello.channels);

    WavFormat format;
    format.samples_per_sec = hello.samples_per_sec;
    format.avg_bytes_per_sec = hello.avg_bytes_per_sec;
    format.channels = hello.channels;
    format.bits = hello.bits;

    auto speak_one = [&](const ivx::Voice& v, const std::wstring& text,
                         const std::wstring& out_path) -> bool {
        WavWriter wav;
        if (!wav.open(out_path, format)) {
            wprintf(L"cannot write %s\n", out_path.c_str());
            return false;
        }

        ivx::SpeakRequest request = {};
        strncpy_s(request.mode_guid, v.mode_guid.c_str(), _TRUNCATE);
        request.rate_step = opt.rate;
        request.pitch_step = opt.pitch;
        request.volume_pct = opt.volume;
        request.timeout_ms = 60000;

        int words = 0;
        int marks = 0;
        ivx::DoneResponse done = {};
        const bool ok = client.speak(
            request, text,
            [&](const void* data, unsigned long n) {
                wav.write(data, n);
                return true;
            },
            [&](const ivx::EventResponse& ev) {
                if (ev.kind == ivx::EV_WORD) {
                    ++words;
                } else {
                    ++marks;
                }
                if (opt.verbose) {
                    const size_t at = ev.value ? ev.value - 1 : 0;
                    wprintf(L"    %-8s audio %7lu  text offset %-4lu  %s\n",
                            ev.kind == ivx::EV_WORD ? L"word" : L"mark",
                            static_cast<unsigned long>(ev.audio_offset),
                            static_cast<unsigned long>(ev.value),
                            (ev.kind == ivx::EV_WORD && at < text.size())
                                ? text.substr(at, 12).c_str()
                                : L"");
                }
            },
            [&](const ivx::FormatResponse&) {}, &done);
        wav.close();

        const double secs =
            format.avg_bytes_per_sec ? wav.bytes / double(format.avg_bytes_per_sec) : 0.0;
        wprintf(L"%-36S %8lu bytes  %5.2fs  words=%d marks=%d  %s\n", v.display_name.c_str(),
                wav.bytes, secs, words, marks,
                (ok && done.status == ivx::DONE_COMPLETE) ? L"ok" : L"INCOMPLETE");
        return ok && wav.bytes > 0;
    };

    if (command == L"worker") {
        if (positional.size() < 2) {
            print_usage();
            return 1;
        }
        const ivx::Voice* chosen = &catalog.voices()[0];
        if (!opt.voice.empty()) {
            const int index = catalog.find_by_name(opt.voice);
            if (index < 0) {
                wprintf(L"No voice called \"%s\". Run 'list' to see them.\n", opt.voice.c_str());
                return 5;
            }
            chosen = &catalog.voices()[static_cast<size_t>(index)];
        }
        const std::wstring text =
            positional.size() > 2 ? positional[2]
                                  : L"Hello. This is the Infovox two thirty engine, reached "
                                    L"through the worker process.";
        return speak_one(*chosen, text, positional[1]) ? 0 : 6;
    }

    // workerall
    if (positional.size() < 2) {
        print_usage();
        return 1;
    }
    make_directory(positional[1]);
    int ok = 0;
    int failed = 0;
    for (const ivx::Voice& v : catalog.voices()) {
        if (speak_one(v, sample_for(v.lcid), positional[1] + L"\\" + file_name_for(v) + L".wav")) {
            ++ok;
        } else {
            ++failed;
        }
    }
    printf("\n%d voices produced audio, %d failed.\n", ok, failed);
    return failed ? 7 : 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv)
{
    ivx::log_init("diag");

    if (argc < 2) {
        print_usage();
        return 1;
    }

    Options opt;
    std::vector<std::wstring> positional;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        auto next = [&](const wchar_t* what) -> std::wstring {
            if (i + 1 >= argc) {
                wprintf(L"%s needs a value\n", what);
                exit(2);
            }
            return argv[++i];
        };
        if (a == L"--engine-dir") {
            opt.engine_dir = next(L"--engine-dir");
        } else if (a == L"--voice") {
            opt.voice = next(L"--voice");
        } else if (a == L"--rate") {
            opt.rate = _wtoi(next(L"--rate").c_str());
            opt.rate_given = true;
        } else if (a == L"--pitch") {
            opt.pitch = _wtoi(next(L"--pitch").c_str());
            opt.pitch_given = true;
        } else if (a == L"--volume") {
            opt.volume = _wtoi(next(L"--volume").c_str());
        } else if (a == L"--verbose") {
            opt.verbose = true;
        } else {
            positional.push_back(a);
        }
    }

    const std::wstring command = positional.empty() ? L"list" : positional[0];

    wprintf(L"this build is %d-bit\n", static_cast<int>(sizeof(void*) * 8));
    wprintf(L"log file:      %s\n", ivx::log_path());

    ivx::Catalog catalog;
    catalog.load(ivx::install_dir());

    if (command == L"list") {
        printf("\n%-36s %-7s %-7s %-6s %s\n", "voice", "lang", "gender", "age", "mode id");
        for (const ivx::Voice& v : catalog.voices()) {
            wprintf(L"%-36S 0x%04X  %-7s %-6s %S\n", v.display_name.c_str(), v.lcid,
                    v.sapi_gender().c_str(), v.sapi_age().c_str(), v.mode_guid.c_str());
        }
        printf("\n%u voices.\n", static_cast<unsigned>(catalog.size()));
        return 0;
    }

    if (command == L"stop") {
        // The worker keeps the engine files open for every client, so it has to
        // be gone before those files can be replaced.
        ivx::WorkerClient client;
        client.shutdown_worker();
        printf("\nAsked the worker to stop.\n");
        return 0;
    }

    if (command == L"worker" || command == L"workerall") {
        printf("\n");
        return run_worker(command, positional, opt, catalog);
    }

#ifndef IVX_HAVE_ENGINE
    printf("\nOnly the worker commands are available in the 64-bit build: the engine "
           "itself is 32-bit.\nUse 'worker' or 'workerall', or run the 32-bit "
           "Infovox230Diag.exe.\n");
    print_usage();
    return 1;
#else
    if (opt.engine_dir.empty()) {
        opt.engine_dir = ivx::find_engine_dir();
    }
    if (opt.engine_dir.empty()) {
        printf("\nCould not find the engine folder (the one holding Ivx230nt.dll).\n"
               "Pass --engine-dir explicitly.\n");
        return 3;
    }
    wprintf(L"engine folder: %s\n\n", opt.engine_dir.c_str());

    ivx::Engine engine;
    if (!engine.load(opt.engine_dir, catalog)) {
        printf("FAILED to load the engine. See the log for the reason.\n");
        return 4;
    }

    printf("%u voices, engine made %lu configuration reads, all served from memory.\n\n",
           static_cast<unsigned>(engine.modes().size()), engine.registry_calls());

    if (command == L"registry") {
        printf("The engine's registry imports are %s.\n",
               ivx::VirtualRegistry::instance().installed()
                   ? "redirected into this process's memory"
                   : "NOT redirected (the hive fallback is in use)");
        printf("It made %lu configuration reads and enumerated %u voices.\n",
               engine.registry_calls(), static_cast<unsigned>(engine.modes().size()));
        printf("Nothing was read from or written to the Windows registry.\n");
        return 0;
    }

    auto speak_direct = [&](const ivx::Voice& v, const std::wstring& text,
                            const std::wstring& out_path) -> bool {
        if (!engine.select(v.mode_guid)) {
            return false;
        }
        const int rate = opt.rate_given ? opt.rate : engine.rate_default();
        const int pitch = opt.pitch_given ? opt.pitch : engine.pitch_default();

        WavFormat format;
        format.samples_per_sec = engine.format().samples_per_sec;
        format.avg_bytes_per_sec = engine.format().avg_bytes_per_sec;
        format.channels = engine.format().channels;
        format.bits = engine.format().bits;

        WavWriter wav;
        if (!wav.open(out_path, format)) {
            wprintf(L"cannot write %s\n", out_path.c_str());
            return false;
        }

        int events = 0;
        const std::wstring tagged =
            ivx::format_prologue(rate, pitch, opt.volume) + ivx::escape_text(text);
        const bool ok = engine.speak(
            tagged,
            [&](const void* data, unsigned long n) {
                wav.write(data, n);
                return true;
            },
            [&](const ivx::SpeakEvent&) { ++events; }, nullptr, 60000);
        wav.close();

        const double secs =
            format.avg_bytes_per_sec ? wav.bytes / double(format.avg_bytes_per_sec) : 0.0;
        wprintf(L"%-36S %8lu bytes  %5.2fs  %d events  %s\n", v.display_name.c_str(), wav.bytes,
                secs, events, ok ? L"ok" : L"INCOMPLETE");
        return ok && wav.bytes > 0;
    };

    if (command == L"speak") {
        if (positional.size() < 2) {
            print_usage();
            return 1;
        }
        const ivx::Voice* chosen = &catalog.voices()[0];
        if (!opt.voice.empty()) {
            const int index = catalog.find_by_name(opt.voice);
            if (index < 0) {
                wprintf(L"No voice called \"%s\". Run 'list' to see them.\n", opt.voice.c_str());
                return 5;
            }
            chosen = &catalog.voices()[static_cast<size_t>(index)];
        }
        const std::wstring text =
            positional.size() > 2 ? positional[2]
                                  : L"Hello. This is the Infovox two thirty engine, speaking "
                                    L"through its own SAPI five interface.";
        return speak_direct(*chosen, text, positional[1]) ? 0 : 6;
    }

    if (command == L"all") {
        if (positional.size() < 2) {
            print_usage();
            return 1;
        }
        make_directory(positional[1]);
        int ok = 0;
        int failed = 0;
        for (const ivx::Voice& v : catalog.voices()) {
            if (speak_direct(v, sample_for(v.lcid),
                             positional[1] + L"\\" + file_name_for(v) + L".wav")) {
                ++ok;
            } else {
                ++failed;
            }
        }
        printf("\n%d voices produced audio, %d failed.\n", ok, failed);
        return failed ? 7 : 0;
    }

    print_usage();
    return 1;
#endif
}
