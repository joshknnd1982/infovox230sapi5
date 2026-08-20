#pragma once

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <share.h>

// Diagnostic log, on by default while the engine is still settling.
//
// Written to %LOCALAPPDATA%\BestSpeech\bestspeech.log, which is writable without
// elevation -- the install directory is not, and a SAPI engine runs inside whatever
// application is speaking. Every line carries the process name, its bitness and its pid,
// because a single utterance from a 64-bit host crosses two processes.
//
// Turn it off without reinstalling by creating this registry value:
//   HKCU\Software\BestSpeech  DWORD  Logging = 0
// and back on with Logging = 1. The value is read once per process.
//
// The file is capped and rotated to one previous copy, so leaving it on cannot fill a
// disk during a long session.

#ifndef ENABLE_DEBUG_LOG
#define ENABLE_DEBUG_LOG 1
#endif

#if ENABLE_DEBUG_LOG
namespace DebugLog {

inline constexpr long MAX_LOG_BYTES = 4 * 1024 * 1024;

inline bool BuildLogPath(wchar_t* path, size_t size, const wchar_t* suffix)
{
    wchar_t base[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH) == 0) {
        if (GetTempPathW(MAX_PATH, base) == 0) {
            return false;
        }
    }
    wchar_t dir[MAX_PATH];
    if (swprintf_s(dir, L"%s\\BestSpeech", base) < 0) {
        return false;
    }
    CreateDirectoryW(dir, nullptr);
    return swprintf_s(path, size, L"%s\\bestspeech%s.log", dir, suffix) >= 0;
}

// Read once per process: a hot path should not touch the registry per line.
inline bool Enabled()
{
    static int cached = -1;
    if (cached < 0) {
        cached = 1;
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\BestSpeech", 0, KEY_READ, &key)
            == ERROR_SUCCESS) {
            DWORD value = 1;
            DWORD size = sizeof(value);
            DWORD type = 0;
            if (RegQueryValueExW(key, L"Logging", nullptr, &type,
                                 reinterpret_cast<LPBYTE>(&value), &size) == ERROR_SUCCESS &&
                type == REG_DWORD) {
                cached = (value != 0) ? 1 : 0;
            }
            RegCloseKey(key);
        }
    }
    return cached == 1;
}

inline const char* ProcessTag()
{
    static char tag[64] = {};
    if (!tag[0]) {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        const wchar_t* name = wcsrchr(exe, L'\\');
        name = name ? name + 1 : exe;
        char narrow[48] = {};
        WideCharToMultiByte(CP_UTF8, 0, name, -1, narrow, sizeof(narrow) - 1, nullptr, nullptr);
        sprintf_s(tag, "%s/%d-bit pid %lu", narrow,
                  static_cast<int>(sizeof(void*) * 8), GetCurrentProcessId());
    }
    return tag;
}

inline void Rotate(const wchar_t* path)
{
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &info)) {
        return;
    }
    if (info.nFileSizeHigh == 0 && info.nFileSizeLow < MAX_LOG_BYTES) {
        return;
    }
    wchar_t previous[MAX_PATH];
    if (BuildLogPath(previous, MAX_PATH, L".1")) {
        DeleteFileW(previous);
        MoveFileW(path, previous);
    }
}

// Speech latency is the whole point of a screen reader, so a log line costs one write
// and one flush rather than an open, a write and a close. The handle is kept for the
// life of the process; the flush is what keeps the file useful when the host crashes.
//
// The share mode matters more than it looks: _wfopen_s opens a file for EXCLUSIVE access,
// so the first process to log would lock every other one out for as long as it ran --
// and since a screen reader and the 32-bit worker are both long-lived, whichever started
// first silently stole the log from everyone else. _wfsopen with _SH_DENYNO lets them all
// append. The open is also retried rather than latched, so a process that starts while
// the file is briefly unavailable still ends up logging.
inline FILE* Handle()
{
    static FILE* file = nullptr;
    static DWORD next_try = 0;

    if (!file) {
        const DWORD now = GetTickCount();
        if (next_try != 0 && now < next_try) {
            return nullptr;
        }
        next_try = now + 5000;

        wchar_t path[MAX_PATH];
        if (BuildLogPath(path, MAX_PATH, L"")) {
            Rotate(path);
            file = _wfsopen(path, L"a", _SH_DENYNO);
        }
    }
    return file;
}

inline void Log(const char* format, ...)
{
    if (!Enabled()) {
        return;
    }
    FILE* file = Handle();
    if (!file) {
        return;
    }

    // The whole line is composed first and written once: the SAPI engine and the 32-bit
    // worker both append to this file, and a single write per line keeps their output
    // from interleaving mid-sentence.
    char line[2048];
    SYSTEMTIME now;
    GetLocalTime(&now);
    int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
                        "%04d-%02d-%02d %02d:%02d:%02d.%03d [%s] ",
                        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                        now.wSecond, now.wMilliseconds, ProcessTag());
    if (n < 0) {
        return;
    }

    va_list args;
    va_start(args, format);
    const int m = _vsnprintf_s(line + n, sizeof(line) - n, _TRUNCATE, format, args);
    va_end(args);
    if (m > 0) {
        n += m;
    }
    if (n < static_cast<int>(sizeof(line)) - 1) {
        line[n++] = '\n';
    }

    fwrite(line, 1, static_cast<size_t>(n), file);
    fflush(file);
}
}

#define DEBUG_LOG(...) DebugLog::Log(__VA_ARGS__)
#else
#define DEBUG_LOG(...) ((void)0)
#endif
