#pragma once

// Where things are. Every module needs to find the same three places without
// asking the registry, so all of it is derived from the module's own location.
//
// Installed layout:
//   <app>\Infovox230SAPI.dll      32-bit SAPI5 engine
//   <app>\Infovox230Server.exe    32-bit worker that owns the engine
//   <app>\Infovox230Diag.exe      diagnostics
//   <app>\voices.ini              optional user voices
//   <app>\engine\                 Ivx230nt.dll, sx32w.dll, the .ivx rule files
//   <app>\x64\Infovox230SAPI.dll  64-bit SAPI5 engine

#include <string>

namespace ivx {

// Folder holding the calling module (dll or exe).
std::wstring module_dir();

// The install root: module_dir(), or its parent when the module lives in the
// x64 subfolder.
std::wstring install_dir();

// Folder holding Ivx230nt.dll, or an empty string if it cannot be found.
// INFOVOX230_ENGINE_DIR overrides the search, which is what the build tree uses.
std::wstring find_engine_dir();

// Full path to the 32-bit worker, whether or not it exists yet.
std::wstring server_exe_path();

// %LOCALAPPDATA%\Infovox230SAPI, created if missing.
std::wstring user_data_dir();

}  // namespace ivx
