#include "ivx_settings.h"

#include <windows.h>

#include "ivx_log.h"

namespace ivx {

namespace settings_key {

const wchar_t kSection[] = L"Settings";
const wchar_t kTrimTrailingSilence[] = L"TrimTrailingSilence";
const wchar_t kSilenceThreshold[] = L"SilenceThreshold";
const wchar_t kWordEvents[] = L"WordEvents";
const wchar_t kSentenceEvents[] = L"SentenceEvents";
const wchar_t kTimeoutBaseMs[] = L"TimeoutBaseMs";
const wchar_t kTimeoutPerCharMs[] = L"TimeoutPerCharMs";
const wchar_t kRateMin[] = L"RateMin";
const wchar_t kRateMax[] = L"RateMax";
const wchar_t kRateDefault[] = L"RateDefault";
const wchar_t kPitchMin[] = L"PitchMin";
const wchar_t kPitchMax[] = L"PitchMax";
const wchar_t kPitchDefault[] = L"PitchDefault";
const wchar_t kPreviewText[] = L"PreviewText";

}  // namespace settings_key

const wchar_t kDefaultPreviewText[] =
    L"This is the Infovox 230 speech engine, speaking with the voice you have just made.";

namespace {

// A value no ini file can contain, so "absent" and "empty" stay distinct: a key
// the file does not name must leave the current value alone.
const wchar_t kAbsent[] = L"\x01";

bool read_raw(const std::wstring& path, const wchar_t* key, std::wstring* out)
{
    wchar_t buf[1024];
    GetPrivateProfileStringW(settings_key::kSection, key, kAbsent, buf, _countof(buf),
                             path.c_str());
    if (wcscmp(buf, kAbsent) == 0) {
        return false;
    }
    // Trim, so "Pitch = 50 " and "Pitch=50" mean the same thing.
    std::wstring value = buf;
    const size_t b = value.find_first_not_of(L" \t");
    if (b == std::wstring::npos) {
        out->clear();
        return true;
    }
    const size_t e = value.find_last_not_of(L" \t");
    *out = value.substr(b, e - b + 1);
    return true;
}

void read_bool(const std::wstring& path, const wchar_t* key, bool* target)
{
    std::wstring value;
    if (!read_raw(path, key, &value) || value.empty()) {
        return;
    }
    *target = !(value == L"0" || _wcsicmp(value.c_str(), L"no") == 0 ||
                _wcsicmp(value.c_str(), L"off") == 0 || _wcsicmp(value.c_str(), L"false") == 0);
}

void read_int(const std::wstring& path, const wchar_t* key, int* target)
{
    std::wstring value;
    if (!read_raw(path, key, &value)) {
        return;
    }
    // An empty value means "not set", which for the range overrides is how the
    // engine's own answer is asked for.
    *target = value.empty() ? 0 : static_cast<int>(wcstol(value.c_str(), nullptr, 10));
}

bool file_exists(const std::wstring& path)
{
    return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

}  // namespace

void EngineSettings::merge_from(const std::wstring& ini_path)
{
    if (!file_exists(ini_path)) {
        return;
    }

    read_bool(ini_path, settings_key::kTrimTrailingSilence, &trim_trailing_silence);
    read_int(ini_path, settings_key::kSilenceThreshold, &silence_threshold);
    read_bool(ini_path, settings_key::kWordEvents, &word_events);
    read_bool(ini_path, settings_key::kSentenceEvents, &sentence_events);
    read_int(ini_path, settings_key::kTimeoutBaseMs, &timeout_base_ms);
    read_int(ini_path, settings_key::kTimeoutPerCharMs, &timeout_per_char_ms);
    read_int(ini_path, settings_key::kRateMin, &rate_min);
    read_int(ini_path, settings_key::kRateMax, &rate_max);
    read_int(ini_path, settings_key::kRateDefault, &rate_default);
    read_int(ini_path, settings_key::kPitchMin, &pitch_min);
    read_int(ini_path, settings_key::kPitchMax, &pitch_max);
    read_int(ini_path, settings_key::kPitchDefault, &pitch_default);

    std::wstring text;
    if (read_raw(ini_path, settings_key::kPreviewText, &text)) {
        preview_text = text;
    }

    // Values a mistyped file could make nonsensical, brought back into range
    // rather than left to produce silence or a hang.
    if (silence_threshold < 0) {
        silence_threshold = 0;
    }
    if (silence_threshold > 32767) {
        silence_threshold = 32767;
    }
    if (timeout_base_ms < 1000) {
        timeout_base_ms = 1000;
    }
    if (timeout_per_char_ms < 0) {
        timeout_per_char_ms = 0;
    }

    IVX_INFO("settings: read from %S (trim=%d level=%d words=%d sentences=%d "
             "timeout=%d+%d rate=%d..%d/%d pitch=%d..%d/%d)",
             ini_path.c_str(), trim_trailing_silence ? 1 : 0, silence_threshold,
             word_events ? 1 : 0, sentence_events ? 1 : 0, timeout_base_ms, timeout_per_char_ms,
             rate_min, rate_max, rate_default, pitch_min, pitch_max, pitch_default);
}

}  // namespace ivx
