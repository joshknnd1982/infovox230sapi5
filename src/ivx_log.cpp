#include "ivx_log.h"

#include <shlwapi.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "shlwapi.lib")

namespace ivx {
namespace {

// Rotated at 8 MB. Trace level on a talkative screen reader writes roughly a
// megabyte an hour, so this holds most of a working day at the highest level
// and effectively forever at the default.
constexpr LONGLONG kMaxLogBytes = 8LL * 1024 * 1024;

// One writer at a time across all processes. Writes are single WriteFile calls
// on a handle opened with FILE_APPEND_DATA, which the kernel already serialises;
// the mutex exists for rotation, where the file is closed and renamed.
constexpr wchar_t kLogMutexName[] = L"Local\\Infovox230SAPI_Log";

struct LogState {
    CRITICAL_SECTION cs;
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mutex = nullptr;
    LogLevel level = IVX_LOG_INFO;
    bool initialised = false;
    char tag[24] = "ivx";
    DWORD pid = 0;
    wchar_t path[MAX_PATH] = L"";
    wchar_t dir[MAX_PATH] = L"";
    unsigned pending_size_check = 0;
};

LogState& state()
{
    static LogState s;
    return s;
}

// Runs before anything else touches the state; a plain static initialiser is
// enough because log_init() is called from DllMain/main before any thread that
// could log has been created.
struct CsInit {
    CsInit() { InitializeCriticalSection(&state().cs); }
} g_cs_init;

bool data_dir(wchar_t* out, size_t count)
{
    wchar_t base[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH) == 0) {
        if (GetTempPathW(MAX_PATH, base) == 0) {
            return false;
        }
    }
    if (_snwprintf_s(out, count, _TRUNCATE, L"%s\\Infovox230SAPI", base) < 0) {
        return false;
    }
    CreateDirectoryW(out, nullptr);
    return true;
}

LogLevel clamp_level(long v)
{
    if (v < IVX_LOG_OFF) {
        return IVX_LOG_OFF;
    }
    if (v > IVX_LOG_TRACE) {
        return IVX_LOG_TRACE;
    }
    return static_cast<LogLevel>(v);
}

LogLevel read_level(const wchar_t* dir)
{
    wchar_t buf[32];
    if (GetEnvironmentVariableW(L"INFOVOX230_LOG_LEVEL", buf, 32) > 0) {
        return clamp_level(wcstol(buf, nullptr, 10));
    }

    wchar_t path[MAX_PATH];
    if (_snwprintf_s(path, MAX_PATH, _TRUNCATE, L"%s\\loglevel.txt", dir) > 0) {
        FILE* f = nullptr;
        if (_wfopen_s(&f, path, L"r") == 0 && f) {
            char line[32] = {0};
            const bool got = fgets(line, sizeof(line), f) != nullptr;
            fclose(f);
            if (got) {
                return clamp_level(strtol(line, nullptr, 10));
            }
        }
    }
    return IVX_LOG_INFO;
}

void open_file_locked()
{
    LogState& s = state();
    if (s.file != INVALID_HANDLE_VALUE) {
        return;
    }
    s.file = CreateFileW(s.path, FILE_APPEND_DATA,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

// Called every 64 lines rather than every line: GetFileSizeEx on an append
// handle is cheap but not free, and the cap does not need to be exact.
void maybe_rotate_locked()
{
    LogState& s = state();
    if (s.file == INVALID_HANDLE_VALUE) {
        return;
    }
    if (++s.pending_size_check < 64) {
        return;
    }
    s.pending_size_check = 0;

    LARGE_INTEGER size;
    if (!GetFileSizeEx(s.file, &size) || size.QuadPart < kMaxLogBytes) {
        return;
    }

    // Another process may be mid-write; the mutex makes the close/rename/reopen
    // atomic with respect to other loggers.
    if (s.mutex && WaitForSingleObject(s.mutex, 2000) == WAIT_TIMEOUT) {
        return;
    }
    CloseHandle(s.file);
    s.file = INVALID_HANDLE_VALUE;

    wchar_t old_path[MAX_PATH];
    if (_snwprintf_s(old_path, MAX_PATH, _TRUNCATE, L"%s.1", s.path) > 0) {
        DeleteFileW(old_path);
        MoveFileW(s.path, old_path);
    }
    open_file_locked();
    if (s.mutex) {
        ReleaseMutex(s.mutex);
    }
}

}  // namespace

void log_init(const char* module_tag)
{
    LogState& s = state();
    EnterCriticalSection(&s.cs);
    if (s.initialised) {
        LeaveCriticalSection(&s.cs);
        return;
    }
    s.initialised = true;
    s.pid = GetCurrentProcessId();
    if (module_tag && *module_tag) {
        strncpy_s(s.tag, module_tag, _TRUNCATE);
    }

    if (data_dir(s.dir, MAX_PATH)) {
        _snwprintf_s(s.path, MAX_PATH, _TRUNCATE, L"%s\\infovox230.log", s.dir);
        s.level = read_level(s.dir);
        if (s.level != IVX_LOG_OFF) {
            s.mutex = CreateMutexW(nullptr, FALSE, kLogMutexName);
            open_file_locked();
        }
    } else {
        s.level = IVX_LOG_OFF;
    }
    LeaveCriticalSection(&s.cs);

    if (s.level == IVX_LOG_OFF) {
        return;
    }

    wchar_t exe[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    IVX_INFO("---- %s starting: host=%S bitness=%d level=%d ----", s.tag, exe,
             static_cast<int>(sizeof(void*) * 8), static_cast<int>(s.level));
}

void log_shutdown()
{
    LogState& s = state();
    EnterCriticalSection(&s.cs);
    if (s.file != INVALID_HANDLE_VALUE) {
        CloseHandle(s.file);
        s.file = INVALID_HANDLE_VALUE;
    }
    if (s.mutex) {
        CloseHandle(s.mutex);
        s.mutex = nullptr;
    }
    LeaveCriticalSection(&s.cs);
}

LogLevel log_level()
{
    return state().level;
}

const wchar_t* log_path()
{
    return state().path;
}

void log_write(LogLevel level, const char* fmt, ...)
{
    LogState& s = state();
    if (s.level == IVX_LOG_OFF || s.level < level) {
        return;
    }

    static const char* const kNames[] = {"OFF", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"};

    SYSTEMTIME t;
    GetLocalTime(&t);

    char body[2048];
    va_list args;
    va_start(args, fmt);
    int body_len = _vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, args);
    va_end(args);
    if (body_len < 0) {
        body_len = static_cast<int>(strlen(body));
    }

    char line[2304];
    const int len = _snprintf_s(line, sizeof(line), _TRUNCATE,
                                "%04u-%02u-%02u %02u:%02u:%02u.%03u %-6s %s/%u.%lu %s\r\n",
                                t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                                t.wMilliseconds, kNames[level], s.tag,
                                static_cast<unsigned>(sizeof(void*) * 8),
                                static_cast<unsigned long>(s.pid), body);
    if (len <= 0) {
        return;
    }

    EnterCriticalSection(&s.cs);
    open_file_locked();
    if (s.file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(s.file, line, static_cast<DWORD>(len), &written, nullptr);
        maybe_rotate_locked();
    }
    LeaveCriticalSection(&s.cs);
}

const char* win_error(DWORD code)
{
    static thread_local char buf[512];
    char* msg = nullptr;
    const DWORD n = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&msg), 0, nullptr);
    if (n && msg) {
        // Trim the trailing CRLF FormatMessage appends.
        char* end = msg + strlen(msg);
        while (end > msg && (end[-1] == '\r' || end[-1] == '\n' || end[-1] == ' ')) {
            *--end = '\0';
        }
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%lu (%s)",
                    static_cast<unsigned long>(code), msg);
    } else {
        _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%lu", static_cast<unsigned long>(code));
    }
    if (msg) {
        LocalFree(msg);
    }
    return buf;
}

const char* hr_error(HRESULT hr)
{
    static thread_local char buf[544];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "0x%08lX %s",
                static_cast<unsigned long>(hr), win_error(static_cast<DWORD>(hr)));
    return buf;
}

}  // namespace ivx
