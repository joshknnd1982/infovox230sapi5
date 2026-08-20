#include "ivx_client.h"

#include "ivx_log.h"
#include "ivx_paths.h"

namespace ivx {
namespace {

struct Lock {
    explicit Lock(CRITICAL_SECTION* cs) : cs_(cs) { EnterCriticalSection(cs_); }
    ~Lock() { LeaveCriticalSection(cs_); }
    CRITICAL_SECTION* cs_;
};

LONG g_serial = 0;

}  // namespace

WorkerClient::WorkerClient()
{
    InitializeCriticalSection(&cs_);

    wchar_t name[64];
    _snwprintf_s(name, _countof(name), _TRUNCATE, L"Local\\Infovox230_Cancel_%lu_%ld",
                 static_cast<unsigned long>(GetCurrentProcessId()), InterlockedIncrement(&g_serial));
    cancel_name_ = name;
    cancel_ = CreateEventW(nullptr, TRUE, FALSE, cancel_name_.c_str());
    if (!cancel_) {
        IVX_ERROR("client: could not create the cancel event: %s", win_error(GetLastError()));
    }
}

WorkerClient::~WorkerClient()
{
    disconnect();
    if (cancel_) {
        CloseHandle(cancel_);
    }
    DeleteCriticalSection(&cs_);
}

bool WorkerClient::worker_running()
{
    const std::wstring name = server_mutex_name();
    HANDLE h = OpenMutexW(SYNCHRONIZE, FALSE, name.c_str());
    if (h) {
        CloseHandle(h);
        return true;
    }
    return false;
}

bool WorkerClient::launch_worker()
{
    if (worker_running()) {
        return true;
    }

    const std::wstring exe = server_exe_path();
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        IVX_ERROR("client: the worker is missing: %S", exe.c_str());
        return false;
    }

    // Two applications can start speaking at the same moment; the launch mutex
    // makes sure only one of them starts a worker.
    const std::wstring launch_name = launch_mutex_name();
    HANDLE launch = CreateMutexW(nullptr, FALSE, launch_name.c_str());
    if (!launch) {
        return false;
    }
    const DWORD waited = WaitForSingleObject(launch, 10000);
    if (waited != WAIT_OBJECT_0 && waited != WAIT_ABANDONED) {
        CloseHandle(launch);
        return false;
    }
    if (worker_running()) {
        ReleaseMutex(launch);
        CloseHandle(launch);
        return true;
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};

    std::wstring command = L"\"" + exe + L"\"";
    const std::wstring dir = install_dir();
    const BOOL started =
        CreateProcessW(exe.c_str(), &command[0], nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, dir.c_str(), &si, &pi);
    if (started) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        IVX_INFO("client: started the worker (%S)", exe.c_str());
        // Loading the engine takes about a second; wait for it to announce
        // itself rather than failing the first utterance.
        for (int i = 0; i < 100 && !worker_running(); ++i) {
            Sleep(50);
        }
    } else {
        IVX_ERROR("client: could not start the worker: %s", win_error(GetLastError()));
    }

    ReleaseMutex(launch);
    CloseHandle(launch);
    return started != FALSE;
}

bool WorkerClient::ensure_connected()
{
    if (pipe_ != INVALID_HANDLE_VALUE) {
        return true;
    }

    const std::wstring name = pipe_name();
    for (int attempt = 0; attempt < 10; ++attempt) {
        pipe_ = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                            0, nullptr);
        if (pipe_ != INVALID_HANDLE_VALUE) {
            DWORD mode = PIPE_READMODE_BYTE;
            SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr);
            IVX_INFO("client: connected to the worker on %S", name.c_str());
            return true;
        }

        const DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) {
            if (attempt == 0 && !launch_worker()) {
                return false;
            }
            Sleep(100);
        } else if (err == ERROR_PIPE_BUSY) {
            WaitNamedPipeW(name.c_str(), 2000);
        } else {
            IVX_ERROR("client: cannot open %S: %s", name.c_str(), win_error(err));
            return false;
        }
    }
    IVX_ERROR("client: the worker never became reachable on %S", name.c_str());
    return false;
}

bool WorkerClient::connect()
{
    Lock lock(&cs_);
    return ensure_connected();
}

void WorkerClient::disconnect()
{
    Lock lock(&cs_);
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

bool WorkerClient::send(uint32_t type, const void* payload, uint32_t size)
{
    FrameHeader header = {type, size};
    DWORD written = 0;
    if (!WriteFile(pipe_, &header, sizeof(header), &written, nullptr) ||
        written != sizeof(header)) {
        return false;
    }
    const BYTE* p = static_cast<const BYTE*>(payload);
    while (size) {
        if (!WriteFile(pipe_, p, size, &written, nullptr) || written == 0) {
            return false;
        }
        p += written;
        size -= written;
    }
    return true;
}

bool WorkerClient::read_frame(FrameHeader* header, std::vector<char>* payload)
{
    BYTE* p = reinterpret_cast<BYTE*>(header);
    DWORD remaining = sizeof(*header);
    while (remaining) {
        DWORD got = 0;
        if (!ReadFile(pipe_, p, remaining, &got, nullptr) || got == 0) {
            return false;
        }
        p += got;
        remaining -= got;
    }
    payload->clear();
    if (header->size) {
        payload->resize(header->size);
        BYTE* q = reinterpret_cast<BYTE*>(payload->data());
        remaining = header->size;
        while (remaining) {
            DWORD got = 0;
            if (!ReadFile(pipe_, q, remaining, &got, nullptr) || got == 0) {
                return false;
            }
            q += got;
            remaining -= got;
        }
    }
    return true;
}

bool WorkerClient::hello(HelloResponse* out)
{
    Lock lock(&cs_);
    if (!ensure_connected() || !send(REQ_HELLO)) {
        disconnect();
        return false;
    }
    FrameHeader header;
    std::vector<char> payload;
    if (!read_frame(&header, &payload) || header.type != RSP_HELLO ||
        payload.size() < sizeof(HelloResponse)) {
        disconnect();
        return false;
    }
    memcpy(out, payload.data(), sizeof(HelloResponse));
    if (out->version != kProtocolVersion) {
        IVX_ERROR("client: the worker speaks protocol %u, this build speaks %u -- the "
                  "installation is mixed; reinstall",
                  out->version, kProtocolVersion);
        disconnect();
        return false;
    }
    return true;
}

bool WorkerClient::voices(std::vector<VoiceRecord>* out)
{
    Lock lock(&cs_);
    out->clear();
    if (!ensure_connected() || !send(REQ_VOICES)) {
        disconnect();
        return false;
    }
    FrameHeader header;
    std::vector<char> payload;
    if (!read_frame(&header, &payload) || header.type != RSP_VOICES ||
        payload.size() < sizeof(uint32_t)) {
        disconnect();
        return false;
    }
    uint32_t count = 0;
    memcpy(&count, payload.data(), sizeof(count));
    if (payload.size() < sizeof(count) + static_cast<size_t>(count) * sizeof(VoiceRecord)) {
        IVX_ERROR("client: the voice list is truncated");
        disconnect();
        return false;
    }
    out->resize(count);
    if (count) {
        memcpy(out->data(), payload.data() + sizeof(count),
               static_cast<size_t>(count) * sizeof(VoiceRecord));
    }
    IVX_DEBUG("client: %u voices from the worker", count);
    return true;
}

bool WorkerClient::stream_utterance(const AudioHandler& on_audio, const EventHandler& on_event,
                                    const FormatHandler& on_format, DoneResponse* done)
{
    bool aborted = false;
    for (;;) {
        FrameHeader header;
        std::vector<char> payload;
        if (!read_frame(&header, &payload)) {
            IVX_ERROR("client: the worker closed the connection mid-utterance");
            disconnect();
            return false;
        }

        switch (header.type) {
            case RSP_FORMAT:
                if (on_format && payload.size() >= sizeof(FormatResponse)) {
                    FormatResponse fmt;
                    memcpy(&fmt, payload.data(), sizeof(fmt));
                    on_format(fmt);
                }
                break;

            case RSP_AUDIO:
                // Keep draining after an abort: the frames are already on their
                // way, and stopping the read would leave the pipe out of step.
                if (!aborted && on_audio && !payload.empty()) {
                    if (!on_audio(payload.data(), static_cast<unsigned long>(payload.size()))) {
                        aborted = true;
                        if (cancel_) {
                            SetEvent(cancel_);
                        }
                    }
                }
                break;

            case RSP_EVENT:
                if (!aborted && on_event && payload.size() >= sizeof(EventResponse)) {
                    EventResponse ev;
                    memcpy(&ev, payload.data(), sizeof(ev));
                    on_event(ev);
                }
                break;

            case RSP_ERROR: {
                const std::string message(payload.begin(), payload.end());
                IVX_ERROR("client: the worker reported: %s", message.c_str());
                if (done) {
                    done->status = DONE_ENGINE_ERROR;
                    done->total_bytes = 0;
                }
                return false;
            }

            case RSP_DONE:
                if (done && payload.size() >= sizeof(DoneResponse)) {
                    memcpy(done, payload.data(), sizeof(DoneResponse));
                }
                if (cancel_) {
                    ResetEvent(cancel_);
                }
                return true;

            default:
                IVX_WARN("client: unexpected frame %u mid-utterance", header.type);
                break;
        }
    }
}

bool WorkerClient::speak(SpeakRequest request, const std::wstring& text,
                         const AudioHandler& on_audio, const EventHandler& on_event,
                         const FormatHandler& on_format, DoneResponse* done)
{
    Lock lock(&cs_);
    if (!ensure_connected()) {
        return false;
    }
    if (cancel_) {
        ResetEvent(cancel_);
    }
    wcsncpy_s(request.cancel_event, cancel_name_.c_str(), _TRUNCATE);
    request.text_chars = static_cast<uint32_t>(text.size());

    std::vector<char> payload(sizeof(request) + text.size() * sizeof(wchar_t));
    memcpy(payload.data(), &request, sizeof(request));
    if (!text.empty()) {
        memcpy(payload.data() + sizeof(request), text.data(), text.size() * sizeof(wchar_t));
    }

    if (!send(REQ_SPEAK, payload.data(), static_cast<uint32_t>(payload.size()))) {
        IVX_ERROR("client: could not send the utterance: %s", win_error(GetLastError()));
        disconnect();
        return false;
    }
    return stream_utterance(on_audio, on_event, on_format, done);
}

bool WorkerClient::speak_phonemes(PhonemeRequest request, const std::wstring& phonemes,
                                  const AudioHandler& on_audio, const FormatHandler& on_format,
                                  DoneResponse* done)
{
    Lock lock(&cs_);
    if (!ensure_connected()) {
        return false;
    }
    if (cancel_) {
        ResetEvent(cancel_);
    }
    wcsncpy_s(request.cancel_event, cancel_name_.c_str(), _TRUNCATE);
    request.text_chars = static_cast<uint32_t>(phonemes.size());

    std::vector<char> payload(sizeof(request) + phonemes.size() * sizeof(wchar_t));
    memcpy(payload.data(), &request, sizeof(request));
    if (!phonemes.empty()) {
        memcpy(payload.data() + sizeof(request), phonemes.data(),
               phonemes.size() * sizeof(wchar_t));
    }

    if (!send(REQ_PHONEMES, payload.data(), static_cast<uint32_t>(payload.size()))) {
        disconnect();
        return false;
    }
    return stream_utterance(on_audio, nullptr, on_format, done);
}

void WorkerClient::shutdown_worker()
{
    Lock lock(&cs_);
    if (!worker_running()) {
        return;
    }
    if (ensure_connected() && send(REQ_SHUTDOWN)) {
        FrameHeader header;
        std::vector<char> payload;
        read_frame(&header, &payload);
    }
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
    for (int i = 0; i < 40 && worker_running(); ++i) {
        Sleep(50);
    }
    IVX_INFO("client: worker shutdown %s", worker_running() ? "did not complete" : "complete");
}

}  // namespace ivx
