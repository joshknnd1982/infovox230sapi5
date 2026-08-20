#pragma once

#include <windows.h>
#include <sapi.h>
#include <string>

#include "registry.hpp"
#include "voice_attributes.hpp"

namespace Bestspeech {
namespace sapi {

// Voice token registration, factored out of DllRegisterServer so the verification tools
// can drive the very same code against HKEY_CURRENT_USER. Registration used to be
// reachable only through regsvr32 into HKLM, which needs elevation, so nothing tested it
// -- and a bug that only showed up once SAPI read a token back from the registry could
// pass every suite.
inline constexpr const wchar_t* voices_path =
    L"Software\\Microsoft\\Speech\\Voices\\Tokens";

// Every voice is written as its own static token rather than being produced by a
// dynamic token enumerator. Static tokens are what every SAPI5 client reads, including
// Windows Narrator, so this is the form with the widest compatibility.
inline void write_voice_tokens(HKEY root, const std::wstring& clsid_str)
{
    using namespace Bestspeech::registry;

    key tokens(root, voices_path, KEY_CREATE_SUB_KEY | KEY_SET_VALUE, true);

    for (int i = 0; i < total_token_count(); ++i) {
        const voice_attributes v(i);
        const std::wstring name = v.get_name();

        key token(tokens, v.get_token_id(), KEY_CREATE_SUB_KEY | KEY_SET_VALUE, true);
        token.set(name);
        token.set(L"CLSID", clsid_str);
        // SAPI looks a display name up under a value named for the LCID it is asking
        // about, falling back to the key's default value, which is set just above.
        token.set(L"409", name);

        key attrs(token, L"Attributes", KEY_SET_VALUE, true);
        attrs.set(L"Name", name);
        attrs.set(L"Gender", v.get_gender());
        attrs.set(L"Age", v.get_age());
        attrs.set(L"Language", v.get_language());
        attrs.set(L"Vendor", v.get_vendor());
        // Read back by SetObjectToken, so the exact engine and voice are recovered
        // without parsing a display name apart.
        attrs.set(L"BstEngine", v.get_engine_id());
        attrs.set(L"BstVoice", v.get_voice_id());
    }
}

inline void remove_voice_tokens(HKEY root) noexcept
{
    using namespace Bestspeech::registry;

    try {
        key tokens(root, voices_path, KEY_ALL_ACCESS);
        for (int i = 0; i < total_token_count(); ++i) {
            const voice_attributes v(i);
            const std::wstring id = v.get_token_id();
            try {
                key token(tokens, id, KEY_ALL_ACCESS);
                token.delete_subkey(L"Attributes");
            }
            catch (...) {
            }
            try {
                tokens.delete_subkey(id);
            }
            catch (...) {
            }
        }
    }
    catch (...) {
    }
}
}
}
