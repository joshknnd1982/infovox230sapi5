#include "ivx_com.h"

#include "ivx_log.h"

namespace ivx {
namespace com {

LSTATUS set_value(HKEY root, const std::wstring& path, const wchar_t* name,
                  const std::wstring& value)
{
    HKEY key = nullptr;
    LSTATUS rc = RegCreateKeyExW(root, path.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                 KEY_SET_VALUE, nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS) {
        // Access denied here is expected and recoverable: the caller retries
        // under HKEY_CURRENT_USER, so record it quietly rather than as a failure.
        IVX_DEBUG("register: cannot create %s\\%S: %s",
                  root == HKEY_LOCAL_MACHINE ? "HKLM" : "HKCU", path.c_str(),
                  win_error(static_cast<DWORD>(rc)));
        return rc;
    }
    rc = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) {
        IVX_DEBUG("register: cannot set %s\\%S\\%S: %s",
                  root == HKEY_LOCAL_MACHINE ? "HKLM" : "HKCU", path.c_str(),
                  name ? name : L"(default)", win_error(static_cast<DWORD>(rc)));
    }
    return rc;
}

LSTATUS delete_tree(HKEY root, const std::wstring& path)
{
    // RegDeleteTreeW removes the key and everything under it; it has been
    // present since Vista, which is well below anything this ships for.
    const LSTATUS rc = RegDeleteTreeW(root, path.c_str());
    if (rc == ERROR_SUCCESS) {
        RegDeleteKeyW(root, path.c_str());
    }
    return rc;
}

bool register_class(HKEY root, REFCLSID clsid, const std::wstring& dll_path,
                    const wchar_t* friendly_name)
{
    const std::wstring base = L"Software\\Classes\\CLSID\\" + clsid_string(clsid);
    if (friendly_name && set_value(root, base, nullptr, friendly_name) != ERROR_SUCCESS) {
        return false;
    }
    if (set_value(root, base + L"\\InProcServer32", nullptr, dll_path) != ERROR_SUCCESS) {
        return false;
    }
    // "Both" so a SAPI host may use the engine from either apartment; the engine
    // itself is serialised behind the worker in any case.
    return set_value(root, base + L"\\InProcServer32", L"ThreadingModel", L"Both") ==
           ERROR_SUCCESS;
}

bool unregister_class(HKEY root, REFCLSID clsid)
{
    const std::wstring base = L"Software\\Classes\\CLSID\\" + clsid_string(clsid);
    const LSTATUS rc = delete_tree(root, base);
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}

}  // namespace com
}  // namespace ivx
