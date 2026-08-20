#pragma once

// A registry that exists only in this process's memory.
//
// The Infovox 230 engine reads every piece of its configuration -- where its
// rule files live, and the whole voice table -- from
// HKEY_CURRENT_USER\Software\Babel-Infovox AB\Infovox 230. Historically the only
// ways to satisfy that were to write those keys into the user's registry and
// leave them there, or to redirect HKEY_CURRENT_USER for the process with
// RegOverridePredefKey and a hive file.
//
// Neither is necessary. Ivx230nt.dll reaches the registry through exactly ten
// imported functions, all ANSI:
//
//     RegOpenKeyExA   RegCreateKeyExA  RegQueryValueExA  RegSetValueExA
//     RegSetValueA    RegEnumKeyExA    RegQueryInfoKeyA  RegCloseKey
//     RegDeleteKeyA   RegDeleteValueA
//
// Rewriting those ten slots in the engine module's import address table points
// them at the tree below. The engine then reads its configuration out of RAM.
// Nothing is written to the registry, nothing is read from it, and no other
// module in the process is affected -- only Ivx230nt.dll's own imports change.
//
// If the import table ever turns out not to be patchable, seed_hive() writes the
// same tree into a private hive file instead, which is the older redirection
// approach kept as a fallback so a failure here cannot mean silence.

#include <windows.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ivx {

// Case-insensitive ordering, because registry key and value names are.
struct CiLess {
    bool operator()(const std::string& a, const std::string& b) const
    {
        return _stricmp(a.c_str(), b.c_str()) < 0;
    }
};

struct VRegValue {
    DWORD type = REG_SZ;
    std::string data;  // raw bytes, including the terminating NUL for strings
};

struct VRegKey {
    std::string name;
    std::map<std::string, VRegValue, CiLess> values;
    std::map<std::string, std::unique_ptr<VRegKey>, CiLess> children;

    VRegKey* child(const std::string& path, bool create);
};

class VirtualRegistry {
public:
    static VirtualRegistry& instance();

    // Configuration, built before the engine is loaded. `path` is
    // backslash-separated and relative to HKEY_CURRENT_USER.
    void set_string(const std::string& path, const std::string& name, const std::string& value);
    void set_dword(const std::string& path, const std::string& name, DWORD value);

    // Redirect the engine module's ten registry imports at this tree. Returns
    // false only if the import table could not be rewritten, which is when the
    // caller should fall back to seed_hive().
    bool install(HMODULE engine_module);
    void uninstall();

    bool installed() const { return installed_; }

    // How many calls the engine has made, by function. Logged after start-up as
    // the evidence that the engine really is reading from here.
    unsigned long call_count() const { return calls_; }

    // Fallback path: write the same tree into `hive_path` as a private
    // application hive and point this process's HKEY_CURRENT_USER at it. Still
    // leaves the user's registry untouched, but does go through the real
    // registry API. Returns false if the hive could not be used.
    bool seed_hive(const wchar_t* hive_path);
    void release_hive();

    // --- called by the interception thunks; public so they can be free
    // functions with the exact WINAPI signatures the engine expects ---
    VRegKey* resolve(HKEY key);
    HKEY make_handle(VRegKey* node);
    void close_handle(HKEY key);
    void note_call() { ++calls_; }
    CRITICAL_SECTION* lock() { return &cs_; }

private:
    VirtualRegistry();
    ~VirtualRegistry();
    VirtualRegistry(const VirtualRegistry&) = delete;
    VirtualRegistry& operator=(const VirtualRegistry&) = delete;

    VRegKey root_;
    VRegKey denied_;  // stands in for HKLM/HKCR/HKU: always empty
    std::map<HKEY, VRegKey*> handles_;
    ULONG_PTR next_handle_ = 0x1;
    CRITICAL_SECTION cs_;
    bool installed_ = false;
    unsigned long calls_ = 0;

    HKEY hive_key_ = nullptr;
    bool overriding_ = false;

    struct PatchedSlot {
        void** slot;
        void* original;
    };
    std::vector<PatchedSlot> patched_;
};

}  // namespace ivx
