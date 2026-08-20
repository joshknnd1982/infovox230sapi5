#pragma once

#include <string>
#include "engines.hpp"

namespace Bestspeech {
namespace sapi {

// One published SAPI voice: a language engine paired with one of its character voices.
// Engines whose frontend ignores every inline command have a single voice, so they are
// published under the language name alone rather than under a character name they
// cannot actually produce.
class voice_attributes
{
public:
    explicit voice_attributes(int token_index = 0) noexcept
    {
        if (!token_at(token_index, engine_, voice_)) {
            engine_ = 0;
            voice_ = 0;
        }
    }

    voice_attributes(int engine_index, int voice_index) noexcept
        : engine_(engine_index), voice_(voice_index)
    {
        if (engine_ < 0 || engine_ >= engine_count) {
            engine_ = 0;
        }
        if (voice_ < 0 || voice_ >= engines[engine_].voice_count) {
            voice_ = 0;
        }
    }

    [[nodiscard]] int get_engine_index() const noexcept { return engine_; }
    [[nodiscard]] int get_voice_index() const noexcept { return voice_; }

    [[nodiscard]] const engine_info& engine() const noexcept { return engines[engine_]; }
    [[nodiscard]] const voice_info& voice() const noexcept { return voices[voice_]; }

    [[nodiscard]] bool single_voice() const noexcept { return engine().voice_count == 1; }

    [[nodiscard]] std::wstring get_name() const
    {
        std::wstring name = L"BestSpeech ";
        if (!single_voice()) {
            name += voice().name;
            name += L" - ";
        }
        name += engine().display;
        return name;
    }

    // Registry key name for this voice. Stable across releases, so a user's chosen
    // voice survives an upgrade.
    [[nodiscard]] std::wstring get_token_id() const
    {
        std::wstring id = L"BestSpeech_";
        const char* p = engine().id;
        while (*p) {
            id += static_cast<wchar_t>(*p++);
        }
        if (!single_voice()) {
            id += L'_';
            id += voice().name;
        }
        return id;
    }

    [[nodiscard]] std::wstring get_age() const { return L"Adult"; }

    [[nodiscard]] std::wstring get_gender() const
    {
        // A single-voice engine has no character behind it, so it takes the neutral
        // default rather than claiming Fred's gender.
        return (!single_voice() && voice().is_female) ? L"Female" : L"Male";
    }

    [[nodiscard]] std::wstring get_language() const { return engine().lcid; }

    [[nodiscard]] std::wstring get_vendor() const { return L"BestSpeech"; }

    // The engine id is written into the token so SetObjectToken can recover the exact
    // voice without having to parse a display name back apart.
    [[nodiscard]] std::wstring get_engine_id() const
    {
        std::wstring id;
        const char* p = engine().id;
        while (*p) {
            id += static_cast<wchar_t>(*p++);
        }
        return id;
    }

    [[nodiscard]] std::wstring get_voice_id() const
    {
        return single_voice() ? std::wstring(L"0") : std::to_wstring(voice_);
    }

private:
    int engine_ = 0;
    int voice_ = 0;
};
}
}
