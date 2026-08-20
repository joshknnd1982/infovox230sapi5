#include "ivx_paths.h"

#include <windows.h>

#include <vector>

#include "ivx_log.h"

namespace ivx {
namespace {

// A dummy address inside this module, so GetModuleHandleEx finds the dll rather
// than the host executable.
void module_anchor() {}

std::wstring parent_of(const std::wstring& path)
{
    const size_t cut = path.find_last_of(L'\\');
    return cut == std::wstring::npos ? path : path.substr(0, cut);
}

bool has_engine(const std::wstring& dir)
{
    if (dir.empty()) {
        return false;
    }
    const std::wstring dll = dir + L"\\Ivx230nt.dll";
    return GetFileAttributesW(dll.c_str()) != INVALID_FILE_ATTRIBUTES;
}

}  // namespace

std::wstring module_dir()
{
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&module_anchor), &self);

    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        const DWORD n = GetModuleFileNameW(self, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) {
            return std::wstring();
        }
        if (n < buf.size() - 1) {
            break;
        }
        buf.resize(buf.size() * 2);
    }
    return parent_of(buf.data());
}

std::wstring install_dir()
{
    const std::wstring dir = module_dir();
    const size_t cut = dir.find_last_of(L'\\');
    if (cut != std::wstring::npos && _wcsicmp(dir.c_str() + cut + 1, L"x64") == 0) {
        return dir.substr(0, cut);
    }
    return dir;
}

std::wstring find_engine_dir()
{
    wchar_t env[MAX_PATH];
    if (GetEnvironmentVariableW(L"INFOVOX230_ENGINE_DIR", env, MAX_PATH) > 0) {
        if (has_engine(env)) {
            IVX_INFO("paths: engine folder from INFOVOX230_ENGINE_DIR: %S", env);
            return env;
        }
        IVX_WARN("paths: INFOVOX230_ENGINE_DIR=%S has no Ivx230nt.dll; ignoring it", env);
    }

    const std::wstring here = module_dir();
    const std::wstring app = install_dir();
    const std::wstring candidates[] = {
        app + L"\\engine",
        here + L"\\engine",
        here,
        app,
        // Build tree: the payload still sits where it was unpacked.
        parent_of(app) + L"\\bin\\infovox230\\engine",
        parent_of(parent_of(app)) + L"\\bin\\infovox230\\engine",
        parent_of(parent_of(parent_of(app))) + L"\\bin\\infovox230\\engine",
    };
    for (const std::wstring& c : candidates) {
        if (has_engine(c)) {
            IVX_INFO("paths: engine folder is %S", c.c_str());
            return c;
        }
    }
    IVX_ERROR("paths: no Ivx230nt.dll found near %S", here.c_str());
    return std::wstring();
}

std::wstring server_exe_path()
{
    return install_dir() + L"\\Infovox230Server.exe";
}

std::wstring user_data_dir()
{
    wchar_t base[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH) == 0) {
        if (GetTempPathW(MAX_PATH, base) == 0) {
            return std::wstring();
        }
    }
    std::wstring dir = std::wstring(base) + L"\\Infovox230SAPI";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

}  // namespace ivx
