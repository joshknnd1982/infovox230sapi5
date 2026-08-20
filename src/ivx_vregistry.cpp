#include "ivx_vregistry.h"

#include "ivx_log.h"

#include <cstring>

namespace ivx {
namespace {

// A guard so a handle that did not come from here is never dereferenced. The
// engine only ever passes back what we handed it, but a stray predefined key
// (HKEY_CURRENT_USER and friends are small constants with the top bit set) must
// be recognised rather than treated as a pointer.
constexpr ULONG_PTR kHandleBase = 0x49560000;  // 'IV'

bool is_predefined(HKEY key)
{
    return (reinterpret_cast<ULONG_PTR>(key) & 0x80000000u) != 0;
}

std::string to_string(const char* s)
{
    return s ? std::string(s) : std::string();
}

const char* root_name(HKEY key)
{
    if (key == HKEY_CURRENT_USER) return "HKCU";
    if (key == HKEY_LOCAL_MACHINE) return "HKLM";
    if (key == HKEY_CLASSES_ROOT) return "HKCR";
    if (key == HKEY_USERS) return "HKU";
    if (key == HKEY_CURRENT_CONFIG) return "HKCC";
    return "HK?";
}

// ---------------------------------------------------------------------------
// Import address table patching
// ---------------------------------------------------------------------------

// Replaces the slot `module` uses to call `dll_name`!`func_name`. Returns the
// address of the slot so it can be put back, or nullptr if the import is not
// there. Only the one module's imports change; the exported function itself is
// untouched, so nothing else in the process is affected.
void** find_iat_slot(HMODULE module, const char* dll_name, const char* func_name)
{
    auto* base = reinterpret_cast<BYTE*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return nullptr;
    }
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return nullptr;
    }

    const IMAGE_DATA_DIRECTORY& dir =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dir.VirtualAddress == 0 || dir.Size == 0) {
        return nullptr;
    }

    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (; desc->Name; ++desc) {
        const char* name = reinterpret_cast<const char*>(base + desc->Name);
        if (_stricmp(name, dll_name) != 0) {
            continue;
        }

        // OriginalFirstThunk still holds the names after the loader has bound
        // the module; FirstThunk holds the addresses actually called.
        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
            base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
        auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);

        for (; thunk->u1.AddressOfData; ++thunk, ++iat) {
            if (IMAGE_SNAP_BY_ORDINAL(thunk->u1.Ordinal)) {
                continue;  // imported by ordinal; not one of ours
            }
            auto* import = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + thunk->u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char*>(import->Name), func_name) == 0) {
                return reinterpret_cast<void**>(&iat->u1.Function);
            }
        }
    }
    return nullptr;
}

bool write_slot(void** slot, void* value, void** previous)
{
    DWORD old_protect = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_protect)) {
        return false;
    }
    if (previous) {
        *previous = *slot;
    }
    *slot = value;
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), old_protect, &ignored);
    return true;
}

// ---------------------------------------------------------------------------
// The replacements. Signatures must match advapi32's exactly.
// ---------------------------------------------------------------------------

struct Scope {
    Scope() { EnterCriticalSection(VirtualRegistry::instance().lock()); }
    ~Scope() { LeaveCriticalSection(VirtualRegistry::instance().lock()); }
};

LSTATUS APIENTRY vreg_open(HKEY hKey, LPCSTR lpSubKey, DWORD, REGSAM, PHKEY phkResult)
{
    Scope scope;
    VirtualRegistry& vr = VirtualRegistry::instance();
    vr.note_call();

    VRegKey* parent = vr.resolve(hKey);
    if (!parent) {
        IVX_DEBUG("vreg: RegOpenKeyExA(%s, \"%s\") -> unknown root", root_name(hKey),
                  lpSubKey ? lpSubKey : "");
        return ERROR_FILE_NOT_FOUND;
    }
    VRegKey* node = parent->child(to_string(lpSubKey), false);
    if (!node) {
        IVX_DEBUG("vreg: RegOpenKeyExA(%s, \"%s\") -> not found", root_name(hKey),
                  lpSubKey ? lpSubKey : "");
        return ERROR_FILE_NOT_FOUND;
    }
    if (!phkResult) {
        return ERROR_INVALID_PARAMETER;
    }
    *phkResult = vr.make_handle(node);
    IVX_TRACE("vreg: RegOpenKeyExA(%s, \"%s\") -> ok (%u values, %u subkeys)", root_name(hKey),
              lpSubKey ? lpSubKey : "", static_cast<unsigned>(node->values.size()),
              static_cast<unsigned>(node->children.size()));
    return ERROR_SUCCESS;
}

LSTATUS APIENTRY vreg_create(HKEY hKey, LPCSTR lpSubKey, DWORD, LPSTR, DWORD, REGSAM,
                             const LPSECURITY_ATTRIBUTES, PHKEY phkResult, LPDWORD lpdwDisposition)
{
    Scope scope;
    VirtualRegistry& vr = VirtualRegistry::instance();
    vr.note_call();

    VRegKey* parent = vr.resolve(hKey);
    if (!parent || !phkResult) {
        return ERROR_INVALID_PARAMETER;
    }
    const bool existed = parent->child(to_string(lpSubKey), false) != nullptr;
    VRegKey* node = parent->child(to_string(lpSubKey), true);
    if (!node) {
        return ERROR_INVALID_PARAMETER;
    }
    *phkResult = vr.make_handle(node);
    if (lpdwDisposition) {
        *lpdwDisposition = existed ? REG_OPENED_EXISTING_KEY : REG_CREATED_NEW_KEY;
    }
    IVX_TRACE("vreg: RegCreateKeyExA(%s, \"%s\") -> %s", root_name(hKey), lpSubKey ? lpSubKey : "",
              existed ? "opened" : "created");
    return ERROR_SUCCESS;
}

LSTATUS APIENTRY vreg_query_value(HKEY hKey, LPCSTR lpValueName, LPDWORD, LPDWORD lpType,
                                  LPBYTE lpData, LPDWORD lpcbData)
{
    Scope scope;
    VirtualRegistry& vr = VirtualRegistry::instance();
    vr.note_call();

    VRegKey* node = vr.resolve(hKey);
    if (!node) {
        return ERROR_INVALID_HANDLE;
    }
    const std::string name = to_string(lpValueName);
    auto it = node->values.find(name);
    if (it == node->values.end()) {
        IVX_TRACE("vreg: RegQueryValueExA(\"%s\", \"%s\") -> not found", node->name.c_str(),
                  name.c_str());
        return ERROR_FILE_NOT_FOUND;
    }

    const VRegValue& value = it->second;
    if (lpType) {
        *lpType = value.type;
    }
    const DWORD needed = static_cast<DWORD>(value.data.size());
    if (!lpData) {
        if (lpcbData) {
            *lpcbData = needed;
        }
        return ERROR_SUCCESS;
    }
    if (!lpcbData) {
        return ERROR_INVALID_PARAMETER;
    }
    if (*lpcbData < needed) {
        *lpcbData = needed;
        return ERROR_MORE_DATA;
    }
    memcpy(lpData, value.data.data(), needed);
    *lpcbData = needed;
    IVX_TRACE("vreg: RegQueryValueExA(\"%s\", \"%s\") -> \"%s\"", node->name.c_str(), name.c_str(),
              value.type == REG_SZ ? value.data.c_str() : "<binary>");
    return ERROR_SUCCESS;
}

LSTATUS APIENTRY vreg_set_value_ex(HKEY hKey, LPCSTR lpValueName, DWORD, DWORD dwType,
                                   const BYTE* lpData, DWORD cbData)
{
    Scope scope;
    VirtualRegistry& vr = VirtualRegistry::instance();
    vr.note_call();

    VRegKey* node = vr.resolve(hKey);
    if (!node) {
        return ERROR_INVALID_HANDLE;
    }
    VRegValue value;
    value.type = dwType;
    if (lpData && cbData) {
        value.data.assign(reinterpret_cast<const char*>(lpData), cbData);
    }
    node->values[to_string(lpValueName)] = value;
    IVX_DEBUG("vreg: engine wrote \"%s\\%s\" (%lu bytes, type %lu) -- kept in memory only",
              node->name.c_str(), lpValueName ? lpValueName : "", static_cast<unsigned long>(cbData),
              static_cast<unsigned long>(dwType));
    return ERROR_SUCCESS;
}

LSTATUS APIENTRY vreg_set_value(HKEY hKey, LPCSTR lpSubKey, DWORD dwType, LPCSTR lpData,
                                DWORD cbData)
{
    Scope scope;
    VirtualRegistry& vr = VirtualRegistry::instance();
    vr.note_call();

    VRegKey* parent = vr.resolve(hKey);
    if (!parent) {
        return ERROR_INVALID_HANDLE;
    }
    VRegKey* node = parent->child(to_string(lpSubKey), true);
    if (!node) {
        return ERROR_INVALID_PARAMETER;
    }
    VRegValue value;
    value.type = dwType;
    if (lpData) {
        // RegSetValueA takes a NUL-terminated string; cbData excludes the NUL.
        value.data.assign(lpData, cbData);
        value.data.push_back('\0');
    }
    node->values[std::string()] = value;
    IVX_DEBUG("vreg: engine set default value of \"%s\" -- kept in memory only",
              node->name.c_str());
    return ERROR_SUCCESS;
}

LSTATUS APIENTRY vreg_enum_key(HKEY hKey, DWORD dwIndex, LPSTR lpName, LPDWORD lpcchName, LPDWORD,
                               LPSTR lpClass, LPDWORD lpcchClass, PFILETIME lpftLastWriteTime)
{
    Scope scope;
    VirtualRegistry& vr = VirtualRegistry::instance();
    vr.note_call();

    VRegKey* node = vr.resolve(hKey);
    if (!node) {
        return ERROR_INVALID_HANDLE;
    }
    if (dwIndex >= node->children.size()) {
        return ERROR_NO_MORE_ITEMS;
    }
    auto it = node->children.begin();
    std::advance(it, dwIndex);
    const std::string& name = it->first;

    if (!lpName || !lpcchName) {
        return ERROR_INVALID_PARAMETER;
    }
    if (*lpcchName <= name.size()) {
        return ERROR_MORE_DATA;
    }
    memcpy(lpName, name.c_str(), name.size() + 1);
    *lpcchName = static_cast<DWORD>(name.size());
    if (lpClass && lpcchClass) {
        if (*lpcchClass > 0) {
            lpClass[0] = '\0';
        }
        *lpcchClass = 0;
    }
    if (lpftLastWriteTime) {
        GetSystemTimeAsFileTime(lpftLastWriteTime);
    }
    IVX_TRACE("vreg: RegEnumKeyExA(\"%s\", %lu) -> \"%s\"", node->name.c_str(),
              static_cast<unsigned long>(dwIndex), name.c_str());
    return ERROR_SUCCESS;
}

LSTATUS APIENTRY vreg_query_info(HKEY hKey, LPSTR lpClass, LPDWORD lpcchClass, LPDWORD,
                                 LPDWORD lpcSubKeys, LPDWORD lpcbMaxSubKeyLen,
                                 LPDWORD lpcbMaxClassLen, LPDWORD lpcValues,
                                 LPDWORD lpcbMaxValueNameLen, LPDWORD lpcbMaxValueLen,
                                 LPDWORD lpcbSecurityDescriptor, PFILETIME lpftLastWriteTime)
{
    Scope scope;
    VirtualRegistry& vr = VirtualRegistry::instance();
    vr.note_call();

    VRegKey* node = vr.resolve(hKey);
    if (!node) {
        return ERROR_INVALID_HANDLE;
    }
    size_t max_subkey = 0;
    for (const auto& child : node->children) {
        max_subkey = (std::max)(max_subkey, child.first.size());
    }
    size_t max_name = 0;
    size_t max_value = 0;
    for (const auto& value : node->values) {
        max_name = (std::max)(max_name, value.first.size());
        max_value = (std::max)(max_value, value.second.data.size());
    }

    if (lpClass && lpcchClass && *lpcchClass > 0) {
        lpClass[0] = '\0';
    }
    if (lpcchClass) {
        *lpcchClass = 0;
    }
    if (lpcSubKeys) {
        *lpcSubKeys = static_cast<DWORD>(node->children.size());
    }
    if (lpcbMaxSubKeyLen) {
        *lpcbMaxSubKeyLen = static_cast<DWORD>(max_subkey);
    }
    if (lpcbMaxClassLen) {
        *lpcbMaxClassLen = 0;
    }
    if (lpcValues) {
        *lpcValues = static_cast<DWORD>(node->values.size());
    }
    if (lpcbMaxValueNameLen) {
        *lpcbMaxValueNameLen = static_cast<DWORD>(max_name);
    }
    if (lpcbMaxValueLen) {
        *lpcbMaxValueLen = static_cast<DWORD>(max_value);
    }
    if (lpcbSecurityDescriptor) {
        *lpcbSecurityDescriptor = 0;
    }
    if (lpftLastWriteTime) {
        GetSystemTimeAsFileTime(lpftLastWriteTime);
    }
    IVX_TRACE("vreg: RegQueryInfoKeyA(\"%s\") -> %u subkeys, %u values", node->name.c_str(),
              static_cast<unsigned>(node->children.size()),
              static_cast<unsigned>(node->values.size()));
    return ERROR_SUCCESS;
}

LSTATUS APIENTRY vreg_close(HKEY hKey)
{
    Scope scope;
    VirtualRegistry& vr = VirtualRegistry::instance();
    vr.note_call();
    vr.close_handle(hKey);
    return ERROR_SUCCESS;
}

LSTATUS APIENTRY vreg_delete_key(HKEY hKey, LPCSTR lpSubKey)
{
    Scope scope;
    VirtualRegistry& vr = VirtualRegistry::instance();
    vr.note_call();

    VRegKey* parent = vr.resolve(hKey);
    if (!parent || !lpSubKey) {
        return ERROR_INVALID_PARAMETER;
    }
    IVX_DEBUG("vreg: engine tried to delete key \"%s\" -- in-memory only", lpSubKey);
    // Resolve the parent of the leaf so a path with separators works too.
    std::string path(lpSubKey);
    const size_t cut = path.find_last_of('\\');
    VRegKey* owner = parent;
    std::string leaf = path;
    if (cut != std::string::npos) {
        owner = parent->child(path.substr(0, cut), false);
        leaf = path.substr(cut + 1);
    }
    if (!owner || owner->children.erase(leaf) == 0) {
        return ERROR_FILE_NOT_FOUND;
    }
    return ERROR_SUCCESS;
}

LSTATUS APIENTRY vreg_delete_value(HKEY hKey, LPCSTR lpValueName)
{
    Scope scope;
    VirtualRegistry& vr = VirtualRegistry::instance();
    vr.note_call();

    VRegKey* node = vr.resolve(hKey);
    if (!node) {
        return ERROR_INVALID_HANDLE;
    }
    IVX_DEBUG("vreg: engine tried to delete value \"%s\\%s\" -- in-memory only",
              node->name.c_str(), lpValueName ? lpValueName : "");
    return node->values.erase(to_string(lpValueName)) ? ERROR_SUCCESS : ERROR_FILE_NOT_FOUND;
}

struct Redirect {
    const char* name;
    void* replacement;
};

const Redirect kRedirects[] = {
    {"RegOpenKeyExA", reinterpret_cast<void*>(&vreg_open)},
    {"RegCreateKeyExA", reinterpret_cast<void*>(&vreg_create)},
    {"RegQueryValueExA", reinterpret_cast<void*>(&vreg_query_value)},
    {"RegSetValueExA", reinterpret_cast<void*>(&vreg_set_value_ex)},
    {"RegSetValueA", reinterpret_cast<void*>(&vreg_set_value)},
    {"RegEnumKeyExA", reinterpret_cast<void*>(&vreg_enum_key)},
    {"RegQueryInfoKeyA", reinterpret_cast<void*>(&vreg_query_info)},
    {"RegCloseKey", reinterpret_cast<void*>(&vreg_close)},
    {"RegDeleteKeyA", reinterpret_cast<void*>(&vreg_delete_key)},
    {"RegDeleteValueA", reinterpret_cast<void*>(&vreg_delete_value)},
};

}  // namespace

// ---------------------------------------------------------------------------

VRegKey* VRegKey::child(const std::string& path, bool create)
{
    VRegKey* node = this;
    size_t pos = 0;
    while (pos <= path.size()) {
        const size_t next = path.find('\\', pos);
        const std::string part =
            path.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        if (!part.empty()) {
            auto it = node->children.find(part);
            if (it == node->children.end()) {
                if (!create) {
                    return nullptr;
                }
                auto fresh = std::make_unique<VRegKey>();
                fresh->name = node->name.empty() ? part : node->name + "\\" + part;
                VRegKey* raw = fresh.get();
                node->children.emplace(part, std::move(fresh));
                node = raw;
            } else {
                node = it->second.get();
            }
        }
        if (next == std::string::npos) {
            break;
        }
        pos = next + 1;
    }
    return node;
}

VirtualRegistry::VirtualRegistry()
{
    InitializeCriticalSection(&cs_);
    root_.name = "HKCU";
    denied_.name = "<denied>";
}

VirtualRegistry::~VirtualRegistry()
{
    DeleteCriticalSection(&cs_);
}

VirtualRegistry& VirtualRegistry::instance()
{
    static VirtualRegistry vr;
    return vr;
}

void VirtualRegistry::set_string(const std::string& path, const std::string& name,
                                 const std::string& value)
{
    EnterCriticalSection(&cs_);
    VRegKey* node = root_.child(path, true);
    if (node) {
        VRegValue v;
        v.type = REG_SZ;
        v.data.assign(value.c_str(), value.size() + 1);  // include the NUL
        node->values[name] = v;
    }
    LeaveCriticalSection(&cs_);
}

void VirtualRegistry::set_dword(const std::string& path, const std::string& name, DWORD value)
{
    EnterCriticalSection(&cs_);
    VRegKey* node = root_.child(path, true);
    if (node) {
        VRegValue v;
        v.type = REG_DWORD;
        v.data.assign(reinterpret_cast<const char*>(&value), sizeof(value));
        node->values[name] = v;
    }
    LeaveCriticalSection(&cs_);
}

VRegKey* VirtualRegistry::resolve(HKEY key)
{
    if (key == HKEY_CURRENT_USER) {
        return &root_;
    }
    if (is_predefined(key)) {
        // HKLM, HKCR, HKU: deliberately empty. The engine finding nothing here
        // is the point -- it is what proves the configuration came from memory.
        IVX_DEBUG("vreg: engine reached for %s; serving an empty key", root_name(key));
        return &denied_;
    }
    auto it = handles_.find(key);
    return it == handles_.end() ? nullptr : it->second;
}

HKEY VirtualRegistry::make_handle(VRegKey* node)
{
    HKEY key = reinterpret_cast<HKEY>(kHandleBase + (next_handle_ += 4));
    handles_[key] = node;
    return key;
}

void VirtualRegistry::close_handle(HKEY key)
{
    handles_.erase(key);
}

bool VirtualRegistry::install(HMODULE engine_module)
{
    if (!engine_module) {
        return false;
    }
    EnterCriticalSection(&cs_);
    bool all = true;
    for (const Redirect& r : kRedirects) {
        void** slot = find_iat_slot(engine_module, "ADVAPI32.dll", r.name);
        if (!slot) {
            IVX_ERROR("vreg: engine does not import %s; cannot redirect it", r.name);
            all = false;
            continue;
        }
        void* original = nullptr;
        if (!write_slot(slot, r.replacement, &original)) {
            IVX_ERROR("vreg: could not write the import slot for %s: %s", r.name,
                      win_error(GetLastError()));
            all = false;
            continue;
        }
        patched_.push_back({slot, original});
        IVX_DEBUG("vreg: redirected %s", r.name);
    }
    installed_ = all;
    LeaveCriticalSection(&cs_);

    if (all) {
        IVX_INFO("vreg: all %u registry imports redirected into memory; the engine cannot "
                 "reach the Windows registry",
                 static_cast<unsigned>(_countof(kRedirects)));
    } else {
        IVX_ERROR("vreg: only %u of %u imports redirected", static_cast<unsigned>(patched_.size()),
                  static_cast<unsigned>(_countof(kRedirects)));
    }
    return all;
}

void VirtualRegistry::uninstall()
{
    EnterCriticalSection(&cs_);
    for (auto it = patched_.rbegin(); it != patched_.rend(); ++it) {
        write_slot(it->slot, it->original, nullptr);
    }
    patched_.clear();
    handles_.clear();
    installed_ = false;
    LeaveCriticalSection(&cs_);
}

// ---------------------------------------------------------------------------
// Fallback: the same tree, written into a private hive file.
// ---------------------------------------------------------------------------
namespace {

void write_tree(HKEY parent, const VRegKey& node)
{
    for (const auto& value : node.values) {
        const std::wstring name(value.first.begin(), value.first.end());
        RegSetValueExW(parent, name.c_str(), 0, value.second.type,
                       reinterpret_cast<const BYTE*>(value.second.data.data()),
                       static_cast<DWORD>(value.second.data.size()));
    }
    for (const auto& child : node.children) {
        const std::wstring name(child.first.begin(), child.first.end());
        HKEY sub = nullptr;
        if (RegCreateKeyExW(parent, name.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &sub,
                            nullptr) == ERROR_SUCCESS) {
            write_tree(sub, *child.second);
            RegCloseKey(sub);
        }
    }
}

}  // namespace

bool VirtualRegistry::seed_hive(const wchar_t* hive_path)
{
    using RegLoadAppKeyW_t = LSTATUS(WINAPI*)(LPCWSTR, PHKEY, REGSAM, DWORD, DWORD);
    HMODULE advapi = GetModuleHandleW(L"advapi32.dll");
    auto load_app_key =
        advapi ? reinterpret_cast<RegLoadAppKeyW_t>(GetProcAddress(advapi, "RegLoadAppKeyW"))
               : nullptr;
    if (!load_app_key) {
        IVX_ERROR("vreg: RegLoadAppKeyW is unavailable; no fallback possible");
        return false;
    }

    DeleteFileW(hive_path);  // a stale or corrupt hive would fail the load
    HKEY hive = nullptr;
    const LSTATUS rc = load_app_key(hive_path, &hive, KEY_ALL_ACCESS, 0, 0);
    if (rc != ERROR_SUCCESS) {
        IVX_ERROR("vreg: RegLoadAppKeyW(%S) failed: %s", hive_path,
                  win_error(static_cast<DWORD>(rc)));
        return false;
    }

    EnterCriticalSection(&cs_);
    write_tree(hive, root_);
    LeaveCriticalSection(&cs_);

    if (RegOverridePredefKey(HKEY_CURRENT_USER, hive) != ERROR_SUCCESS) {
        IVX_ERROR("vreg: RegOverridePredefKey failed: %s", win_error(GetLastError()));
        RegCloseKey(hive);
        return false;
    }
    hive_key_ = hive;
    overriding_ = true;
    IVX_WARN("vreg: falling back to a private hive at %S; the user's registry is still "
             "untouched, but the engine is reading through the registry API",
             hive_path);
    return true;
}

void VirtualRegistry::release_hive()
{
    if (overriding_) {
        RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        overriding_ = false;
    }
    if (hive_key_) {
        RegCloseKey(hive_key_);  // last handle closed unloads the hive
        hive_key_ = nullptr;
    }
}

}  // namespace ivx
