#pragma once

// Diagnostic log shared by every part of the Infovox 230 SAPI5 engine.
//
// A single utterance can cross three modules -- the 64-bit SAPI dll inside the
// speaking application, the 32-bit SAPI dll inside a 32-bit application, and the
// 32-bit worker process that owns the engine -- so all of them append to ONE
// file and every line carries the module name, its bitness and its pid. Reading
// the log top to bottom then shows the whole conversation in order.
//
// Location: %LOCALAPPDATA%\Infovox230SAPI\infovox230.log
// (%LOCALAPPDATA% because a SAPI engine runs inside whatever application is
// speaking, under that user's token, and the install directory is not writable
// without elevation.)
//
// Level, highest wins, checked once per process:
//   1. environment variable INFOVOX230_LOG_LEVEL
//   2. the first line of %LOCALAPPDATA%\Infovox230SAPI\loglevel.txt
//   3. the built-in default, IVX_LOG_INFO
// Values: 0 off, 1 error, 2 warn, 3 info, 4 debug, 5 trace.
//
// The file is capped and rotated to one previous copy, so leaving it turned up
// cannot fill a disk during a long session.

#include <windows.h>

namespace ivx {

enum LogLevel {
    IVX_LOG_OFF = 0,
    IVX_LOG_ERROR = 1,
    IVX_LOG_WARN = 2,
    IVX_LOG_INFO = 3,
    IVX_LOG_DEBUG = 4,
    IVX_LOG_TRACE = 5,
};

// `module_tag` is a short name for the component ("sapi64", "sapi32",
// "worker", "diag"). Safe to call more than once; the first call wins.
void log_init(const char* module_tag);

// Flushes and closes. Optional -- the file is opened for append and each line is
// written whole, so an abrupt exit loses nothing but the handle.
void log_shutdown();

LogLevel log_level();

// Full path of the log file, for messages that tell the user where to look.
const wchar_t* log_path();

void log_write(LogLevel level, const char* fmt, ...);

// Formats a Windows error code as "123 (The message)". Returns a pointer to a
// thread-local buffer, valid until the next call on the same thread.
const char* win_error(DWORD code);

// Formats an HRESULT the same way.
const char* hr_error(HRESULT hr);

}  // namespace ivx

#define IVX_LOG(level, ...)                                   \
    do {                                                      \
        if (::ivx::log_level() >= (level)) {                  \
            ::ivx::log_write((level), __VA_ARGS__);           \
        }                                                     \
    } while (0)

#define IVX_ERROR(...) IVX_LOG(::ivx::IVX_LOG_ERROR, __VA_ARGS__)
#define IVX_WARN(...) IVX_LOG(::ivx::IVX_LOG_WARN, __VA_ARGS__)
#define IVX_INFO(...) IVX_LOG(::ivx::IVX_LOG_INFO, __VA_ARGS__)
#define IVX_DEBUG(...) IVX_LOG(::ivx::IVX_LOG_DEBUG, __VA_ARGS__)
#define IVX_TRACE(...) IVX_LOG(::ivx::IVX_LOG_TRACE, __VA_ARGS__)
