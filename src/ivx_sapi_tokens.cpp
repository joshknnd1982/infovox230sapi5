#include "ivx_sapi_tokens.h"

#include <sapi.h>
#include <sperror.h>

#include <string>
#include <vector>

#include "ivx_com.h"
#include "ivx_log.h"
#include "ivx_paths.h"
#include "ivx_sapi_engine.h"  // for the engine's class id, which each token names

namespace ivx {
namespace sapi5 {
namespace {

const wchar_t kVoicesKey[] = L"Software\\Microsoft\\Speech\\Voices\\Tokens";

// Every token this product owns starts with this, so uninstalling can find them
// all and nothing else.
const wchar_t kTokenPrefix[] = L"Infovox230_";

std::wstring widen(const std::string& s)
{
    if (s.empty()) {
        return std::wstring();
    }
    const int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n);
    return out;
}

// A registry-safe key name. Backslashes would nest keys and spaces are legal but
// awkward to type when someone is reading a log out loud.
std::wstring token_name(const Voice& voice)
{
    std::wstring name = kTokenPrefix;
    for (wchar_t ch : voice.sapi_name()) {
        name += (ch == L' ' || ch == L'\\' || ch == L'/') ? L'_' : ch;
    }
    return name;
}

}  // namespace

const Catalog& shared_catalog()
{
    static Catalog catalog = [] {
        Catalog c;
        c.load(ivx::install_dir());
        return c;
    }();
    return catalog;
}

bool register_voices(HKEY root)
{
    // Clear our own tokens first. Registering is also how the list is refreshed
    // after voices.ini changes, and without this a voice removed from that file
    // would keep its entry in Windows for ever -- a name in every application's
    // voice list that no longer resolves to anything.
    unregister_voices(root);

    const std::wstring clsid = com::clsid_string(__uuidof(TtsEngine));
    const Catalog& catalog = shared_catalog();

    int written = 0;
    for (const Voice& voice : catalog.voices()) {
        const std::wstring key = std::wstring(kVoicesKey) + L"\\" + token_name(voice);
        const std::wstring name = voice.sapi_name();

        // The unnamed value is the fallback display name; the value named after
        // the language's hex LCID is what SAPI prefers when it matches the user
        // interface language.
        if (com::set_value(root, key, nullptr, name) != ERROR_SUCCESS) {
            IVX_DEBUG("voices: cannot write %S", key.c_str());
            return false;
        }
        com::set_value(root, key, voice.sapi_language().c_str(), name);
        com::set_value(root, key, L"CLSID", clsid);

        const std::wstring attributes = key + L"\\Attributes";
        com::set_value(root, attributes, L"Name", name);
        com::set_value(root, attributes, L"Gender", voice.sapi_gender());
        com::set_value(root, attributes, L"Age", voice.sapi_age());
        com::set_value(root, attributes, L"Language", voice.sapi_language());
        com::set_value(root, attributes, L"Vendor", L"Infovox");

        // Everything the Infovox engine itself can be told about a voice, so an
        // application -- or a user reading the registry -- can see exactly what
        // distinguishes one speaker from another, and so voices.ini has a
        // documented vocabulary. SetObjectToken reads InfovoxModeGUID back out
        // of here to know which engine mode to select.
        com::set_value(root, attributes, L"InfovoxModeGUID", widen(voice.mode_guid));
        com::set_value(root, attributes, L"InfovoxLanguageFile", widen(voice.language_file));
        com::set_value(root, attributes, L"InfovoxLanguageID", widen(voice.language_id));
        com::set_value(root, attributes, L"InfovoxSpeakerName", widen(voice.speaker_name));
        if (!voice.speaker_style.empty()) {
            com::set_value(root, attributes, L"InfovoxSpeakerStyle", widen(voice.speaker_style));
        }
        if (!voice.library_file.empty()) {
            com::set_value(root, attributes, L"InfovoxLibraryFile", widen(voice.library_file));
        }
        if (!voice.phsym_file.empty()) {
            com::set_value(root, attributes, L"InfovoxPhSymFile", widen(voice.phsym_file));
        }
        if (!voice.pitch.empty()) {
            com::set_value(root, attributes, L"InfovoxPitch", widen(voice.pitch));
        }
        if (!voice.dynamic.empty()) {
            com::set_value(root, attributes, L"InfovoxDynamic", widen(voice.dynamic));
        }
        if (!voice.aspiration.empty()) {
            com::set_value(root, attributes, L"InfovoxAspiration", widen(voice.aspiration));
        }
        if (!voice.formant_no.empty()) {
            com::set_value(root, attributes, L"InfovoxFormantNo", widen(voice.formant_no));
        }
        com::set_value(root, attributes, L"InfovoxUserDefined", voice.user_defined ? L"1" : L"0");
        ++written;
    }

    IVX_INFO("voices: published %d voices under %s", written,
             root == HKEY_LOCAL_MACHINE ? "HKLM" : "HKCU");
    return written > 0;
}

void unregister_voices(HKEY root)
{
    HKEY tokens = nullptr;
    if (RegOpenKeyExW(root, kVoicesKey, 0, KEY_READ, &tokens) != ERROR_SUCCESS) {
        return;
    }

    // Collect first, delete afterwards: removing keys while enumerating them
    // silently skips entries.
    std::vector<std::wstring> ours;
    for (DWORD index = 0;; ++index) {
        wchar_t name[256];
        DWORD length = _countof(name);
        if (RegEnumKeyExW(tokens, index, name, &length, nullptr, nullptr, nullptr, nullptr) !=
            ERROR_SUCCESS) {
            break;
        }
        if (_wcsnicmp(name, kTokenPrefix, wcslen(kTokenPrefix)) == 0) {
            ours.push_back(name);
        }
    }
    RegCloseKey(tokens);

    for (const std::wstring& name : ours) {
        com::delete_tree(root, std::wstring(kVoicesKey) + L"\\" + name);
    }
    if (!ours.empty()) {
        IVX_INFO("voices: removed %u voices from %s", static_cast<unsigned>(ours.size()),
                 root == HKEY_LOCAL_MACHINE ? "HKLM" : "HKCU");
    }
}

}  // namespace sapi5
}  // namespace ivx
