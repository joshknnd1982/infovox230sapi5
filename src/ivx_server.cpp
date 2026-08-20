// Infovox230Server -- the 32-bit worker that owns the engine.
//
// One worker per logon session serves every speaking application, 32- and
// 64-bit alike. It exists for three reasons:
//   * Ivx230nt.dll is 32-bit, so a 64-bit host cannot load it at all;
//   * a 1996 engine that faults or wedges must not be able to take a screen
//     reader down with it;
//   * the engine expects to own process-wide state (the current directory, its
//     own window classes), which is rude inside somebody else's application.
//
// Only one thread ever touches the engine. SAPI4 delivers its callbacks through
// that thread's message queue, and the engine allows a single active mode, so
// connection threads hand work to it and wait.

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <string>
#include <vector>

#include "ivx_catalog.h"
#include "ivx_engine.h"
#include "ivx_log.h"
#include "ivx_paths.h"
#include "ivx_protocol.h"

namespace {

using namespace ivx;

struct Job {
    bool phonemes = false;
    std::string mode_guid;
    std::wstring text;
    int rate_step = 0;
    int pitch_step = 0;
    int volume_pct = 100;
    unsigned timeout_ms = 60000;
    bool ipa = false;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE cancel = nullptr;
    HANDLE finished = nullptr;
    DoneStatus status = DONE_COMPLETE;
    unsigned long produced = 0;
};

Catalog g_catalog;
Engine g_engine;
AudioFormat g_format;
std::wstring g_engine_dir;

CRITICAL_SECTION g_queue_cs;
std::deque<Job*> g_queue;
HANDLE g_job_ready = nullptr;
HANDLE g_quit = nullptr;
volatile LONG g_clients = 0;
volatile LONG g_engine_ready = 0;

// --- framing ---------------------------------------------------------------

bool write_all(HANDLE pipe, const void* data, DWORD size)
{
    const BYTE* p = static_cast<const BYTE*>(data);
    while (size) {
        DWORD written = 0;
        if (!WriteFile(pipe, p, size, &written, nullptr) || written == 0) {
            return false;
        }
        p += written;
        size -= written;
    }
    return true;
}

bool write_frame(HANDLE pipe, uint32_t type, const void* payload = nullptr, uint32_t size = 0)
{
    FrameHeader header = {type, size};
    if (!write_all(pipe, &header, sizeof(header))) {
        return false;
    }
    return size == 0 || write_all(pipe, payload, size);
}

bool write_error(HANDLE pipe, const char* message)
{
    IVX_ERROR("server: %s", message);
    return write_frame(pipe, RSP_ERROR, message, static_cast<uint32_t>(strlen(message)));
}

bool read_all(HANDLE pipe, void* data, DWORD size)
{
    BYTE* p = static_cast<BYTE*>(data);
    while (size) {
        DWORD got = 0;
        if (!ReadFile(pipe, p, size, &got, nullptr) || got == 0) {
            return false;
        }
        p += got;
        size -= got;
    }
    return true;
}

// --- the engine thread -----------------------------------------------------

// SAPI expresses rate and pitch as a step from -10 to 10 with no defined units.
// Map a step geometrically onto the engine's real range, so that 0 is the
// voice's own default, +10 is the fastest (or highest) it will go and -10 the
// slowest (or lowest), and equal steps are equal ratios -- which is how speed
// and pitch are actually perceived.
int step_to_value(int step, int lo, int hi, int def)
{
    step = (std::max)(-10, (std::min)(10, step));
    if (step == 0 || hi <= lo) {
        return def;
    }
    const double d = def;
    const double ratio = step > 0 ? (hi > def ? hi / d : 1.0) : (def > lo ? d / lo : 1.0);
    const double value = d * pow(ratio, step / 10.0);
    return (std::max)(lo, (std::min)(hi, static_cast<int>(value + 0.5)));
}

// True if anything in the tagged text would actually be spoken. A client
// legitimately sends bookmark-only chunks; those produce no audio by design and
// must not be mistaken for the engine having failed.
bool wants_audio(const std::wstring& tagged)
{
    for (size_t i = 0; i < tagged.size();) {
        if (tagged[i] == L'\\') {
            if (i + 1 < tagged.size() && tagged[i + 1] == L'\\') {
                return true;  // an escaped backslash is text
            }
            const size_t end = tagged.find(L'\\', i + 1);
            if (end == std::wstring::npos) {
                break;
            }
            i = end + 1;
            continue;
        }
        if (!iswspace(tagged[i])) {
            return true;
        }
        ++i;
    }
    return false;
}

void run_job(Job* job)
{
    if (!InterlockedCompareExchange(&g_engine_ready, 0, 0)) {
        job->status = DONE_ENGINE_ERROR;
        return;
    }

    if (!job->mode_guid.empty() && job->mode_guid != g_engine.selected()) {
        if (!g_engine.select(job->mode_guid)) {
            job->status = DONE_ENGINE_ERROR;
            return;
        }
        // The format is fixed for this engine, but read it back per selection
        // rather than assuming.
        g_format = g_engine.format();
    }

    FormatResponse fmt = {};
    fmt.samples_per_sec = g_format.samples_per_sec;
    fmt.avg_bytes_per_sec = g_format.avg_bytes_per_sec;
    fmt.channels = g_format.channels;
    fmt.bits = g_format.bits;
    if (!write_frame(job->pipe, RSP_FORMAT, &fmt, sizeof(fmt))) {
        job->status = DONE_CANCELLED;
        return;
    }

    bool pipe_ok = true;
    auto on_pcm = [&](const void* data, unsigned long bytes) -> bool {
        job->produced += bytes;
        if (!write_frame(job->pipe, RSP_AUDIO, data, bytes)) {
            pipe_ok = false;
            return false;
        }
        return true;
    };
    // The engine numbers word positions from the start of everything it was
    // handed, which includes the prologue prepended below. The client only knows
    // about its own text, so shift the offsets back into that before reporting
    // them -- the protocol promises an offset into the text the client sent.
    unsigned long prologue_chars = 0;

    auto on_event = [&](const SpeakEvent& e) {
        EventResponse ev = {};
        ev.kind = (e.kind == SpeakEvent::Bookmark) ? EV_BOOKMARK : EV_WORD;
        ev.audio_offset = e.audio_offset;
        ev.value = e.value;
        if (ev.kind == EV_WORD) {
            if (e.value <= prologue_chars) {
                return;  // a word position inside the prologue, which is not text
            }
            ev.value = e.value - prologue_chars;
        }
        if (!write_frame(job->pipe, RSP_EVENT, &ev, sizeof(ev))) {
            pipe_ok = false;
        }
    };
    auto cancelled = [&]() -> bool {
        return job->cancel && WaitForSingleObject(job->cancel, 0) == WAIT_OBJECT_0;
    };

    const int rate = step_to_value(job->rate_step, g_engine.rate_min(), g_engine.rate_max(),
                                   g_engine.rate_default());
    const int pitch = step_to_value(job->pitch_step, g_engine.pitch_min(), g_engine.pitch_max(),
                                    g_engine.pitch_default());
    IVX_DEBUG("server: rate step %d -> %d wpm, pitch step %d -> %d Hz, volume %d%%",
              job->rate_step, rate, job->pitch_step, pitch, job->volume_pct);

    bool ok;
    if (job->phonemes) {
        ok = g_engine.speak_phonemes(job->text, job->ipa, on_pcm, cancelled, job->timeout_ms);
    } else {
        const std::wstring prologue = format_prologue(rate, pitch, job->volume_pct);
        prologue_chars = static_cast<unsigned long>(prologue.size());
        const std::wstring tagged = prologue + job->text;
        ok = g_engine.speak(tagged, on_pcm, on_event, cancelled, job->timeout_ms);

        // A live engine that returns nothing for real text has gone bad. Rebuild
        // the selection and try once more; this is the in-place equivalent of
        // switching synthesizers and back, so the user never has to.
        if (ok && !pipe_ok) {
            // nothing to do: the client went away
        } else if (job->produced == 0 && !cancelled() && wants_audio(job->text)) {
            IVX_WARN("server: utterance produced no audio; recycling the engine and retrying");
            if (g_engine.recycle()) {
                ok = g_engine.speak(tagged, on_pcm, on_event, cancelled, job->timeout_ms);
                if (job->produced == 0) {
                    IVX_ERROR("server: still silent after a recycle");
                }
            }
        }
    }

    if (!pipe_ok) {
        job->status = DONE_CANCELLED;
    } else if (cancelled()) {
        job->status = DONE_CANCELLED;
    } else if (!ok) {
        job->status = DONE_TIMEOUT;
    } else {
        job->status = DONE_COMPLETE;
    }
}

DWORD WINAPI engine_thread(LPVOID)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Give this thread a message queue before anything else: SAPI4 posts its
    // callbacks here.
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    g_catalog.load(ivx::install_dir());
    if (g_engine.load(g_engine_dir, g_catalog)) {
        // Selecting one voice up front means the first utterance does not pay
        // for it, and it is what fills in the audio format.
        if (!g_catalog.voices().empty()) {
            g_engine.select(g_catalog.voices()[0].mode_guid);
            g_format = g_engine.format();
        }
        InterlockedExchange(&g_engine_ready, 1);
        IVX_INFO("server: engine ready, %u voices, %lu Hz %u-bit %u channel(s)",
                 static_cast<unsigned>(g_catalog.size()), g_format.samples_per_sec, g_format.bits,
                 g_format.channels);
    } else {
        IVX_ERROR("server: the engine could not be started; clients will be told so");
    }

    for (;;) {
        HANDLE waits[2] = {g_job_ready, g_quit};
        const DWORD rc = MsgWaitForMultipleObjects(2, waits, FALSE, INFINITE, QS_ALLINPUT);
        if (rc == WAIT_OBJECT_0 + 1) {
            break;
        }
        if (rc == WAIT_OBJECT_0 + 2) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            continue;
        }

        for (;;) {
            Job* job = nullptr;
            EnterCriticalSection(&g_queue_cs);
            if (!g_queue.empty()) {
                job = g_queue.front();
                g_queue.pop_front();
            }
            LeaveCriticalSection(&g_queue_cs);
            if (!job) {
                break;
            }
            run_job(job);
            SetEvent(job->finished);
        }
    }

    g_engine.unload();
    CoUninitialize();
    IVX_INFO("server: engine thread stopped");
    return 0;
}

bool submit(Job* job)
{
    job->finished = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!job->finished) {
        return false;
    }
    EnterCriticalSection(&g_queue_cs);
    g_queue.push_back(job);
    LeaveCriticalSection(&g_queue_cs);
    SetEvent(g_job_ready);
    WaitForSingleObject(job->finished, INFINITE);
    CloseHandle(job->finished);
    job->finished = nullptr;
    return true;
}

// --- connection handling ---------------------------------------------------

void send_voices(HANDLE pipe)
{
    std::vector<VoiceRecord> records;
    records.reserve(g_catalog.size());
    for (const Voice& v : g_catalog.voices()) {
        VoiceRecord r = {};
        const std::wstring name = v.sapi_name();
        wcsncpy_s(r.display_name, name.c_str(), _TRUNCATE);
        strncpy_s(r.mode_guid, v.mode_guid.c_str(), _TRUNCATE);
        r.lcid = v.lcid;
        r.gender = static_cast<uint16_t>(v.gender.empty() ? 0 : atoi(v.gender.c_str()));
        r.age = static_cast<uint16_t>(v.age.empty() ? 30 : atoi(v.age.c_str()));
        r.user_defined = v.user_defined ? 1u : 0u;
        // The engine reports the same limits for every mode; the per-voice
        // default is what differs, and it is only known once selected. Report
        // the limits and let the client scale against them.
        r.rate_min = g_engine.rate_min();
        r.rate_max = g_engine.rate_max();
        r.rate_default = g_engine.rate_default();
        r.pitch_min = g_engine.pitch_min();
        r.pitch_max = g_engine.pitch_max();
        r.pitch_default = g_engine.pitch_default();
        records.push_back(r);
    }

    std::vector<char> payload(sizeof(uint32_t) + records.size() * sizeof(VoiceRecord));
    const uint32_t count = static_cast<uint32_t>(records.size());
    memcpy(payload.data(), &count, sizeof(count));
    if (!records.empty()) {
        memcpy(payload.data() + sizeof(count), records.data(),
               records.size() * sizeof(VoiceRecord));
    }
    write_frame(pipe, RSP_VOICES, payload.data(), static_cast<uint32_t>(payload.size()));
}

HANDLE open_cancel_event(const wchar_t* name)
{
    if (!name || !*name) {
        return nullptr;
    }
    HANDLE h = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name);
    if (!h) {
        IVX_DEBUG("server: no cancel event called %S (%s)", name, win_error(GetLastError()));
    }
    return h;
}

void handle_speak(HANDLE pipe, const std::vector<char>& payload, bool phonemes)
{
    Job job;
    job.pipe = pipe;
    job.phonemes = phonemes;

    if (phonemes) {
        if (payload.size() < sizeof(PhonemeRequest)) {
            write_error(pipe, "short phoneme request");
            return;
        }
        PhonemeRequest req;
        memcpy(&req, payload.data(), sizeof(req));
        const size_t need = sizeof(req) + static_cast<size_t>(req.text_chars) * sizeof(wchar_t);
        if (payload.size() < need) {
            write_error(pipe, "phoneme request text is truncated");
            return;
        }
        job.mode_guid.assign(req.mode_guid, strnlen(req.mode_guid, sizeof(req.mode_guid)));
        job.text.assign(reinterpret_cast<const wchar_t*>(payload.data() + sizeof(req)),
                        req.text_chars);
        job.rate_step = req.rate_step;
        job.pitch_step = req.pitch_step;
        job.volume_pct = req.volume_pct;
        job.timeout_ms = req.timeout_ms ? req.timeout_ms : 60000;
        job.ipa = req.ipa != 0;
        job.cancel = open_cancel_event(req.cancel_event);
    } else {
        if (payload.size() < sizeof(SpeakRequest)) {
            write_error(pipe, "short speak request");
            return;
        }
        SpeakRequest req;
        memcpy(&req, payload.data(), sizeof(req));
        const size_t need = sizeof(req) + static_cast<size_t>(req.text_chars) * sizeof(wchar_t);
        if (payload.size() < need) {
            write_error(pipe, "speak request text is truncated");
            return;
        }
        job.mode_guid.assign(req.mode_guid, strnlen(req.mode_guid, sizeof(req.mode_guid)));
        job.text.assign(reinterpret_cast<const wchar_t*>(payload.data() + sizeof(req)),
                        req.text_chars);
        job.rate_step = req.rate_step;
        job.pitch_step = req.pitch_step;
        job.volume_pct = req.volume_pct;
        job.timeout_ms = req.timeout_ms ? req.timeout_ms : 60000;
        job.cancel = open_cancel_event(req.cancel_event);
    }

    IVX_DEBUG("server: %s request, %u chars, voice %s, rate %d, pitch %d, volume %d",
              phonemes ? "phoneme" : "speak", static_cast<unsigned>(job.text.size()),
              job.mode_guid.c_str(), job.rate_step, job.pitch_step, job.volume_pct);

    submit(&job);

    if (job.cancel) {
        ResetEvent(job.cancel);
        CloseHandle(job.cancel);
    }

    DoneResponse done = {job.status, job.produced};
    write_frame(pipe, RSP_DONE, &done, sizeof(done));
}

DWORD WINAPI client_thread(LPVOID param)
{
    HANDLE pipe = static_cast<HANDLE>(param);
    InterlockedIncrement(&g_clients);
    IVX_INFO("server: client connected (%ld now)", InterlockedCompareExchange(&g_clients, 0, 0));

    for (;;) {
        FrameHeader header;
        if (!read_all(pipe, &header, sizeof(header))) {
            break;
        }
        std::vector<char> payload;
        if (header.size) {
            if (header.size > (64u << 20)) {
                write_error(pipe, "request too large");
                break;
            }
            payload.resize(header.size);
            if (!read_all(pipe, payload.data(), header.size)) {
                break;
            }
        }

        switch (header.type) {
            case REQ_HELLO: {
                HelloResponse hello = {};
                hello.version = kProtocolVersion;
                hello.voice_count = static_cast<uint32_t>(g_catalog.size());
                hello.samples_per_sec = g_format.samples_per_sec;
                hello.avg_bytes_per_sec = g_format.avg_bytes_per_sec;
                hello.channels = g_format.channels;
                hello.bits = g_format.bits;
                write_frame(pipe, RSP_HELLO, &hello, sizeof(hello));
                break;
            }
            case REQ_VOICES:
                send_voices(pipe);
                break;
            case REQ_SPEAK:
                handle_speak(pipe, payload, false);
                break;
            case REQ_PHONEMES:
                handle_speak(pipe, payload, true);
                break;
            case REQ_PING:
                write_frame(pipe, RSP_PONG);
                break;
            case REQ_SHUTDOWN:
                write_frame(pipe, RSP_OK);
                IVX_INFO("server: shutdown requested by a client");
                SetEvent(g_quit);
                break;
            default:
                write_error(pipe, "unknown request");
                break;
        }
        if (WaitForSingleObject(g_quit, 0) == WAIT_OBJECT_0) {
            break;
        }
    }

    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
    InterlockedDecrement(&g_clients);
    IVX_INFO("server: client disconnected (%ld left)", InterlockedCompareExchange(&g_clients, 0, 0));
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv)
{
    ivx::log_init("worker");

    std::wstring engine_dir;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--engine-dir" && i + 1 < argc) {
            engine_dir = argv[++i];
        } else if (a == L"--help") {
            wprintf(L"Infovox230Server [--engine-dir DIR]\n"
                    L"The worker is started on demand by the SAPI5 engine; there is "
                    L"normally no reason to run it by hand.\n");
            return 0;
        }
    }

    // One worker per session. Losing the race is not an error: the other one
    // serves everybody.
    const std::wstring mutex_name = server_mutex_name();
    HANDLE instance = CreateMutexW(nullptr, TRUE, mutex_name.c_str());
    if (!instance) {
        IVX_ERROR("server: could not create %S: %s", mutex_name.c_str(),
                  win_error(GetLastError()));
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        IVX_INFO("server: another worker already owns this session; exiting");
        CloseHandle(instance);
        return 0;
    }

    g_engine_dir = engine_dir.empty() ? ivx::find_engine_dir() : engine_dir;
    if (g_engine_dir.empty()) {
        IVX_ERROR("server: no engine folder found; nothing to serve");
        ReleaseMutex(instance);
        CloseHandle(instance);
        return 2;
    }

    InitializeCriticalSection(&g_queue_cs);
    g_job_ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_quit = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    HANDLE engine = CreateThread(nullptr, 0, engine_thread, nullptr, 0, nullptr);
    if (!engine) {
        IVX_ERROR("server: could not start the engine thread: %s", win_error(GetLastError()));
        return 3;
    }

    const std::wstring pipe = pipe_name();
    IVX_INFO("server: listening on %S", pipe.c_str());

    while (WaitForSingleObject(g_quit, 0) != WAIT_OBJECT_0) {
        HANDLE conn = CreateNamedPipeW(pipe.c_str(), PIPE_ACCESS_DUPLEX,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                       PIPE_UNLIMITED_INSTANCES, 1 << 16, 1 << 16, 0, nullptr);
        if (conn == INVALID_HANDLE_VALUE) {
            IVX_ERROR("server: CreateNamedPipe failed: %s", win_error(GetLastError()));
            Sleep(1000);
            continue;
        }
        if (!ConnectNamedPipe(conn, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(conn);
            continue;
        }
        HANDLE t = CreateThread(nullptr, 0, client_thread, conn, 0, nullptr);
        if (t) {
            CloseHandle(t);
        } else {
            CloseHandle(conn);
        }
    }

    SetEvent(g_quit);
    SetEvent(g_job_ready);
    WaitForSingleObject(engine, 10000);
    CloseHandle(engine);
    DeleteCriticalSection(&g_queue_cs);
    ReleaseMutex(instance);
    CloseHandle(instance);
    IVX_INFO("server: stopped");
    ivx::log_shutdown();
    return 0;
}
