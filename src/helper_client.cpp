#include <vector>

#include "helper_client.h"
#include "debug_log.h"

namespace {

// b32_helper.exe's wire format, as used by the NVDA driver in bin/bestspeech.py.
constexpr uint32_t HANDSHAKE_MAGIC = 0xFFFFFFFEu;
constexpr uint32_t CMD_CANCEL      = 0u;           // a zero-length utterance
constexpr uint32_t CMD_QUIT        = 0xFFFFFFFFu;

// Refuse an absurd chunk length rather than turning it into a huge allocation.
constexpr uint32_t MAX_CHUNK = 16u << 20;

// How long to wait for a helper that has gone quiet. Synthesis streams continuously and
// the clock restarts on every byte, so this only ever trips on a wedged helper.
constexpr DWORD STARTUP_TIMEOUT_MS = 10000;
constexpr DWORD STREAM_TIMEOUT_MS  = 15000;

}  // namespace

HelperClient::HelperClient()
{
    InitializeCriticalSection(&lock_);
}

HelperClient::~HelperClient()
{
    stop();
    DeleteCriticalSection(&lock_);
}

// Scope guard for the pipe lock.
namespace {
struct Lock {
    explicit Lock(CRITICAL_SECTION* cs) : cs_(cs) { EnterCriticalSection(cs_); }
    ~Lock() { LeaveCriticalSection(cs_); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    CRITICAL_SECTION* cs_;
};
}

bool HelperClient::alive() const
{
    if (!process_) {
        return false;
    }
    return WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
}

bool HelperClient::write_all(const void* data, DWORD size)
{
    auto* p = static_cast<const char*>(data);
    while (size > 0) {
        DWORD written = 0;
        if (!WriteFile(to_helper_, p, size, &written, nullptr) || written == 0) {
            return false;
        }
        p += written;
        size -= written;
    }
    return true;
}

bool HelperClient::read_all(void* data, DWORD size, DWORD timeout_ms)
{
    auto* p = static_cast<char*>(data);
    ULONGLONG deadline = GetTickCount64() + timeout_ms;

    while (size > 0) {
        // Never call ReadFile without knowing bytes are waiting: an anonymous pipe read
        // blocks indefinitely, and this code runs inside the application that is talking.
        DWORD available = 0;
        if (!PeekNamedPipe(from_helper_, nullptr, 0, nullptr, &available, nullptr)) {
            return false;  // the helper closed its end
        }
        if (available == 0) {
            if (!alive()) {
                return false;
            }
            if (GetTickCount64() > deadline) {
                DEBUG_LOG("helper: stopped responding with %lu bytes still expected", size);
                return false;
            }
            Sleep(1);
            continue;
        }

        DWORD got = 0;
        const DWORD want = (available < size) ? available : size;
        if (!ReadFile(from_helper_, p, want, &got, nullptr) || got == 0) {
            return false;
        }
        p += got;
        size -= got;
        deadline = GetTickCount64() + timeout_ms;  // progress, so grant more time
    }
    return true;
}

bool HelperClient::start(const std::wstring& helper_path, const std::wstring& engine_dll)
{
    Lock guard(&lock_);
    stop_locked();

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE child_stdin_read = nullptr, parent_stdin_write = nullptr;
    HANDLE parent_stdout_read = nullptr, child_stdout_write = nullptr;

    if (!CreatePipe(&child_stdin_read, &parent_stdin_write, &sa, 0) ||
        !CreatePipe(&parent_stdout_read, &child_stdout_write, &sa, 0)) {
        DEBUG_LOG("helper: could not create pipes (error %lu)", GetLastError());
        return false;
    }
    // Only the child ends may be inherited, or the pipes never report end of file.
    SetHandleInformation(parent_stdin_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(parent_stdout_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = child_stdin_read;
    si.hStdOutput = child_stdout_write;
    si.hStdError = INVALID_HANDLE_VALUE;

    std::wstring command = L"\"" + helper_path + L"\" \"" + engine_dll + L"\"";
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    PROCESS_INFORMATION pi = {};
    const BOOL ok = CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(child_stdin_read);
    CloseHandle(child_stdout_write);

    if (!ok) {
        DEBUG_LOG("helper: could not start %ls (error %lu)", helper_path.c_str(), GetLastError());
        CloseHandle(parent_stdin_write);
        CloseHandle(parent_stdout_read);
        return false;
    }
    CloseHandle(pi.hThread);

    process_ = pi.hProcess;
    to_helper_ = parent_stdin_write;
    from_helper_ = parent_stdout_read;

    // Startup handshake: magic, then the engine's real output rate.
    uint32_t handshake[2] = {};
    if (!read_all(handshake, sizeof(handshake), STARTUP_TIMEOUT_MS) ||
        handshake[0] != HANDSHAKE_MAGIC) {
        DEBUG_LOG("helper: bad handshake from %ls for %ls", helper_path.c_str(), engine_dll.c_str());
        stop_locked();
        return false;
    }
    sample_rate_ = handshake[1];

    DEBUG_LOG("helper: started for %ls, output rate %lu hz", engine_dll.c_str(), sample_rate_);
    return true;
}

bool HelperClient::speak(const char* text, size_t length, float rate_multiplier,
                         float gain_scale, AudioCallback callback, void* user)
{
    Lock guard(&lock_);
    if (!alive() || !text || !callback) {
        return false;
    }

    const uint32_t text_length = static_cast<uint32_t>(length);
    if (!write_all(&text_length, sizeof(text_length)) ||
        !write_all(&rate_multiplier, sizeof(rate_multiplier)) ||
        (text_length > 0 && !write_all(text, text_length))) {
        DEBUG_LOG("helper: write failed, the helper has probably exited");
        return false;
    }

    std::vector<short> scaled;
    bool stopped = false;

    while (true) {
        uint32_t chunk = 0;
        if (!read_all(&chunk, sizeof(chunk), STREAM_TIMEOUT_MS)) {
            DEBUG_LOG("helper: pipe closed mid utterance");
            return false;
        }
        if (chunk == 0) {
            break;  // end of utterance
        }
        if (chunk > MAX_CHUNK) {
            // The stream is out of step and cannot recover; the caller restarts us.
            DEBUG_LOG("helper: implausible chunk length %lu (0x%08X), stream desynced",
                      chunk, chunk);
            return false;
        }

        std::vector<char> buffer(chunk);
        if (!read_all(buffer.data(), chunk, STREAM_TIMEOUT_MS)) {
            DEBUG_LOG("helper: pipe closed reading a chunk");
            return false;
        }
        if (stopped) {
            continue;  // drain the rest so the next utterance starts cleanly
        }

        const char* out = buffer.data();
        long out_size = static_cast<long>(chunk);

        // Volume for the engines whose frontend ignores the inline gain command.
        if (gain_scale != 1.0f) {
            const size_t count = chunk / sizeof(short);
            const auto* in = reinterpret_cast<const short*>(buffer.data());
            scaled.assign(in, in + count);
            for (size_t i = 0; i < count; ++i) {
                const float v = static_cast<float>(scaled[i]) * gain_scale;
                scaled[i] = static_cast<short>(v > 32767.0f ? 32767.0f
                                              : (v < -32768.0f ? -32768.0f : v));
            }
            out = reinterpret_cast<const char*>(scaled.data());
            out_size = static_cast<long>(count * sizeof(short));
        }

        if (!callback(out, out_size, user)) {
            // The host cancelled. A cancel is a bare four byte zero -- NOT a zero length
            // utterance followed by a rate, which is what a speak command looks like.
            // Sending the extra four bytes leaves them in the pipe to be read as the next
            // command's length: 0x3F800000, about a gigabyte, which the helper then tries
            // to read as text. It balloons to a gigabyte of memory and blocks forever,
            // taking the calling application down with it.
            stopped = true;
            const uint32_t cancel = CMD_CANCEL;
            if (!write_all(&cancel, sizeof(cancel))) {
                return false;
            }
        }
    }
    return true;
}

void HelperClient::stop()
{
    Lock guard(&lock_);
    stop_locked();
}

void HelperClient::stop_locked()
{
    if (process_ && to_helper_) {
        const uint32_t quit = CMD_QUIT;
        write_all(&quit, sizeof(quit));
        WaitForSingleObject(process_, 2000);
    }
    if (process_) {
        if (alive()) {
            TerminateProcess(process_, 0);
        }
        CloseHandle(process_);
        process_ = nullptr;
    }
    if (to_helper_) {
        CloseHandle(to_helper_);
        to_helper_ = nullptr;
    }
    if (from_helper_) {
        CloseHandle(from_helper_);
        from_helper_ = nullptr;
    }
    sample_rate_ = 0;
}
