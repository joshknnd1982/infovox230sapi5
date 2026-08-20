// Does one engine's use interfere with the next, inside a single process?
//
// A screen reader steps through the voice list in one long-lived process, so engines are
// loaded, used and freed over and over in whatever order the user browses. The
// command-blind engines (Greek, Japanese, Polish) additionally drive the shim's time
// stretcher and software gain, which the others leave switched off -- and that
// combination is what a fresh-process test never reproduces.
//
//   multi_engine_probe <install-dir>            every engine, alone and together
//   multi_engine_probe <install-dir> pairs      every ordered pair, stretched then plain

#include <windows.h>
#include <process.h>
#include <cstdio>
#include <cstring>
#include <clocale>
#include <string>
#include "b32_wrapper.h"
#include "engines.hpp"

using namespace Bestspeech;

static long g_bytes = 0;
static bool sink(const char*, long n, void*) { g_bytes += n; return true; }

// The command-blind engines are driven exactly as the SAPI engine drives them.
static bool stretched(int e) { return engines[e].commands == cmd_mode::none; }

static long speak_engine(const std::wstring& dir, int e)
{
    const std::wstring path = dir + L"\\" + engines[e].dll;
    b32::StatePtr s = b32::init(path.c_str());
    if (!s) {
        return -1;
    }
    std::string text;
    b32::SpeakParams p;
    if (stretched(e)) {
        text = "Best Speech test";
        p.sonic_speed = 0.76f;   // what NVDA's rate setting works out to
        p.gain_scale = 3.16f;    // +10 dB, applied in software for these engines
    } else {
        text = "~g10]~r53]~e3]~u0]~f80]~h0]Best Speech test ~|";
    }
    p.text = text.c_str();
    g_bytes = 0;
    b32::speak_async(s.get(), sink, nullptr, p);
    return g_bytes;
}

int wmain(int argc, wchar_t** argv)
{
    const std::wstring dir = argc > 1 ? argv[1] : L".";
    const bool pairs = (argc > 2 && wcscmp(argv[2], L"pairs") == 0);

    // A host application normally has a locale set; a bare test program does not. NVDA
    // sets exactly this one, and the 2006 engines are old enough to use locale-sensitive
    // C runtime calls internally, so it has to be part of the test.
    if (argc > 3 && wcscmp(argv[3], L"locale") == 0) {
        const char* got = setlocale(LC_ALL, "English_United States.1252");
        printf("locale set to: %s\n", got ? got : "(failed)");
    }

    if (!b32::load_shim(dir.c_str())) {
        printf("could not load b32_wrapper.dll from that directory\n");
        return 1;
    }

    if (argc > 2 && wcscmp(argv[2], L"thread") == 0) {
        // What a screen reader actually does: synthesis runs on a worker thread while
        // the host's own message loop runs on the main thread. The engine binds its
        // buffer-release message window to whichever thread first drives it, so this is
        // where a thread-affinity problem shows up and a single-threaded test cannot.
        struct Ctx { std::wstring dir; int engine; long bytes; };
        printf("--- synthesis on a worker thread, host pumping messages ---\n");
        for (int i = 0; i < engine_count; ++i) {
            Ctx ctx{ dir, i, 0 };
            HANDLE th = reinterpret_cast<HANDLE>(_beginthreadex(
                nullptr, 0,
                [](void* v) -> unsigned {
                    Ctx* c = static_cast<Ctx*>(v);
                    c->bytes = speak_engine(c->dir, c->engine);
                    return 0;
                },
                &ctx, 0, nullptr));

            // Pump the main thread's queue while the worker synthesizes, exactly as a
            // host application would.
            while (WaitForSingleObject(th, 10) == WAIT_TIMEOUT) {
                MSG msg;
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
            CloseHandle(th);
            printf("  %-8s %8ld bytes%s\n", engines[i].id, ctx.bytes,
                   ctx.bytes <= 0 ? "   <-- SILENT" : "");
        }
        return 0;
    }

    if (!pairs) {
        printf("--- each engine on its own, fresh in this process ---\n");
        for (int i = 0; i < engine_count; ++i) {
            const long n = speak_engine(dir, i);
            printf("  %-8s %8ld bytes%s\n", engines[i].id, n,
                   n <= 0 ? "   <-- SILENT" : "");
        }
        return 0;
    }

    // Every ordered pair where the first engine drives the stretcher. If using one
    // engine can poison the next, this is where it shows.
    printf("--- pair test: use A, free it, then use B ---\n");
    int broken = 0;
    for (int a = 0; a < engine_count; ++a) {
        for (int b = 0; b < engine_count; ++b) {
            if (a == b) {
                continue;
            }
            const long first = speak_engine(dir, a);
            const long second = speak_engine(dir, b);
            if (second <= 0 && first > 0) {
                ++broken;
                printf("  %-8s then %-8s -> second engine SILENT (%ld bytes)\n",
                       engines[a].id, engines[b].id, second);
            }
        }
    }
    printf("%d poisoning pairs found\n", broken);
    return broken == 0 ? 0 : 1;
}
