#include <algorithm>
#include <cwctype>
#include <cwchar>
#include <vector>
#include <windows.h>

#include "text_pipeline.hpp"

namespace Bestspeech {
namespace text {

namespace {

struct translit_rule
{
    const wchar_t* src;
    int            len;
    const wchar_t* dst;
};

#include "translit_tables.inc"

[[nodiscard]] bool is_latin(wchar_t c)
{
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

[[nodiscard]] wchar_t lower_ascii(wchar_t c)
{
    return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
}

[[nodiscard]] bool is_digit(wchar_t c)
{
    return c >= L'0' && c <= L'9';
}

// ---------------------------------------------------------------------------
// Transliteration
// ---------------------------------------------------------------------------

// English spells a long vowel with a final mute e ("drive", "complete"). Neither target
// frontend has that convention, so the e would be voiced as a whole extra syllable.
[[nodiscard]] std::wstring drop_silent_e(const std::wstring& run)
{
    if (run.size() > 3 && lower_ascii(run.back()) == L'e') {
        const wchar_t prev = lower_ascii(run[run.size() - 2]);
        if (prev != L'a' && prev != L'e' && prev != L'i' && prev != L'o' && prev != L'u') {
            return run.substr(0, run.size() - 1);
        }
    }
    return run;
}

[[nodiscard]] bool rule_matches(const std::wstring& run, size_t pos, const translit_rule& r)
{
    if (pos + static_cast<size_t>(r.len) > run.size()) {
        return false;
    }
    for (int i = 0; i < r.len; ++i) {
        if (lower_ascii(run[pos + i]) != r.src[i]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::wstring translit_run(const std::wstring& run,
                                        const translit_rule* multi, size_t multi_count,
                                        const wchar_t* const* single)
{
    std::wstring out;
    size_t i = 0;
    while (i < run.size()) {
        bool matched = false;
        for (size_t r = 0; r < multi_count; ++r) {
            if (rule_matches(run, i, multi[r])) {
                out += multi[r].dst;
                i += static_cast<size_t>(multi[r].len);
                matched = true;
                break;
            }
        }
        if (!matched) {
            const wchar_t c = lower_ascii(run[i]);
            if (c >= L'a' && c <= L'z') {
                out += single[c - L'a'];
            }
            ++i;
        }
    }
    return out;
}

[[nodiscard]] std::wstring transliterate(const std::wstring& text,
                                         const translit_rule* multi, size_t multi_count,
                                         const wchar_t* const* single,
                                         const wchar_t* const* names)
{
    std::wstring out;
    out.reserve(text.size() * 2);

    size_t i = 0;
    while (i < text.size()) {
        if (!is_latin(text[i])) {
            out += text[i];
            ++i;
            continue;
        }
        size_t j = i;
        while (j < text.size() && is_latin(text[j])) {
            ++j;
        }
        const std::wstring run = text.substr(i, j - i);
        if (run.size() == 1) {
            // A lone Latin letter is a screen reader echoing a character, so read it as
            // the English letter name; a bare consonant would be an unintelligible grunt.
            out += names[lower_ascii(run[0]) - L'a'];
        } else {
            out += translit_run(drop_silent_e(run), multi, multi_count, single);
        }
        i = j;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Numbers
// ---------------------------------------------------------------------------

void append_word(std::wstring& out, const wchar_t* w)
{
    if (!w || !*w) {
        return;
    }
    if (!out.empty() && out.back() != L' ') {
        out += L' ';
    }
    out += w;
}

[[nodiscard]] std::wstring greek_under_thousand(unsigned n)
{
    std::wstring out;
    if (n >= 100) {
        if (n == 100) {
            return el_misc[0];  // the flat form, without the linking nu
        }
        append_word(out, el_hundreds[n / 100]);
        n %= 100;
    }
    if (n >= 10 && n <= 19) {
        append_word(out, el_teens[n - 10]);
    } else {
        if (n >= 20) {
            append_word(out, el_tens[n / 10]);
            n %= 10;
        }
        if (n) {
            append_word(out, el_units[n]);
        }
    }
    return out;
}

[[nodiscard]] std::wstring greek_number(unsigned long long n)
{
    if (n == 0) {
        return el_units[0];
    }
    std::wstring out;
    const unsigned long long millions = n / 1000000ULL;
    const unsigned long long rest = n % 1000000ULL;
    const unsigned thousands = static_cast<unsigned>(rest / 1000ULL);
    const unsigned units = static_cast<unsigned>(rest % 1000ULL);

    if (millions) {
        if (millions == 1) {
            append_word(out, el_misc[3]);
        } else {
            if (millions < 1000) {
                append_word(out, greek_under_thousand(static_cast<unsigned>(millions)).c_str());
            } else {
                append_word(out, greek_number(millions).c_str());
            }
            append_word(out, el_misc[4]);
        }
    }
    if (thousands) {
        if (thousands == 1) {
            append_word(out, el_misc[1]);
        } else {
            append_word(out, greek_under_thousand(thousands).c_str());
            append_word(out, el_misc[2]);
        }
    }
    if (units) {
        append_word(out, greek_under_thousand(units).c_str());
    }
    return out;
}

[[nodiscard]] std::wstring polish_under_thousand(unsigned n)
{
    std::wstring out;
    if (n >= 100) {
        append_word(out, pl_hundreds[n / 100]);
        n %= 100;
    }
    if (n >= 10 && n <= 19) {
        append_word(out, pl_teens[n - 10]);
    } else {
        if (n >= 20) {
            append_word(out, pl_tens[n / 10]);
            n %= 10;
        }
        if (n) {
            append_word(out, pl_units[n]);
        }
    }
    return out;
}

// Polish scale nouns take three forms: singular, a paucal form for 2 to 4, and a plural.
[[nodiscard]] const wchar_t* polish_scale_form(unsigned n, int scale)
{
    const wchar_t* const* forms = &pl_scales[scale * 3];
    if (n == 1) {
        return forms[0];
    }
    const unsigned last = n % 10;
    const unsigned last_two = n % 100;
    if (last >= 2 && last <= 4 && !(last_two >= 10 && last_two <= 19)) {
        return forms[1];
    }
    return forms[2];
}

[[nodiscard]] std::wstring polish_number(unsigned long long n)
{
    if (n == 0) {
        return pl_units[0];
    }
    unsigned groups[4] = { 0, 0, 0, 0 };
    int count = 0;
    while (n && count < 4) {
        groups[count++] = static_cast<unsigned>(n % 1000ULL);
        n /= 1000ULL;
    }
    std::wstring out;
    for (int i = count - 1; i >= 0; --i) {
        const unsigned g = groups[i];
        if (!g) {
            continue;
        }
        if (i == 0) {
            append_word(out, polish_under_thousand(g).c_str());
        } else {
            if (g != 1) {  // "tysiac", never "jeden tysiac"
                append_word(out, polish_under_thousand(g).c_str());
            }
            append_word(out, polish_scale_form(g, i - 1));
        }
    }
    return out;
}

[[nodiscard]] std::wstring japanese_group(unsigned n)
{
    std::wstring out;
    out += ja_thousands[n / 1000];
    n %= 1000;
    out += ja_hundreds[n / 100];
    n %= 100;
    const unsigned tens = n / 10;
    if (tens) {
        if (tens != 1) {
            out += ja_digits[tens];
        }
        out += ja_ten[0];
    }
    if (n % 10) {
        out += ja_digits[n % 10];
    }
    return out;
}

[[nodiscard]] std::wstring japanese_number(unsigned long long n)
{
    if (n == 0) {
        return ja_digits[0];
    }
    std::wstring parts[4];
    int count = 0;
    int i = 0;
    while (n && i < 4) {
        const unsigned g = static_cast<unsigned>(n % 10000ULL);
        if (g) {
            // ichi-man and ichi-oku keep their leading one, unlike the bare hundreds group
            parts[count++] = (g == 1 && i > 0)
                ? std::wstring(ja_digits[1]) + ja_groups[i]
                : japanese_group(g) + ja_groups[i];
        } else {
            parts[count++].clear();
        }
        n /= 10000ULL;
        ++i;
    }
    std::wstring out;
    for (int k = count - 1; k >= 0; --k) {
        out += parts[k];
    }
    return out;
}

// Beyond what the word tables cover, fall back to reading the digits one at a time,
// which is still infinitely better than the frontend dropping them in silence.
[[nodiscard]] std::wstring digits_individually(const std::wstring& run, number_mode mode)
{
    const wchar_t* const* names =
        (mode == number_mode::greek) ? el_units :
        (mode == number_mode::polish) ? pl_units : ja_digits;
    const bool spaced = (mode != number_mode::japanese);

    std::wstring out;
    for (wchar_t c : run) {
        if (spaced && !out.empty()) {
            out += L' ';
        }
        out += names[c - L'0'];
    }
    return out;
}

constexpr unsigned long long MAX_SPELLED_NUMBER = 999999999999ULL;

[[nodiscard]] std::wstring localize_numbers(const std::wstring& text, number_mode mode)
{
    if (mode == number_mode::native) {
        return text;
    }
    std::wstring out;
    out.reserve(text.size() * 3);

    size_t i = 0;
    while (i < text.size()) {
        if (!is_digit(text[i])) {
            out += text[i];
            ++i;
            continue;
        }
        size_t j = i;
        while (j < text.size() && is_digit(text[j])) {
            ++j;
        }
        const std::wstring run = text.substr(i, j - i);

        bool overflow = run.size() > 12;
        unsigned long long value = 0;
        if (!overflow) {
            for (wchar_t c : run) {
                value = value * 10ULL + static_cast<unsigned long long>(c - L'0');
            }
            overflow = value > MAX_SPELLED_NUMBER;
        }
        // A leading zero means the run is an identifier, a code or a time, not a
        // quantity; reading "007" as "seven" would lose information.
        const bool leading_zero = run.size() > 1 && run[0] == L'0';

        out += (overflow || leading_zero) ? digits_individually(run, mode)
                                          : number_to_words(value, mode);
        i = j;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Sanitizing
// ---------------------------------------------------------------------------

// Characters that break a frontend outright, established by sweeping every ascii
// symbol through every dll and comparing the synthesized pcm against a clean
// reference (tools/verify_engines.py):
//   ~     starts an inline command on every build, so user text containing one would
//         be swallowed as a malformed command.
//   { }   silence the whole utterance on every v2 dll, and the Russian frontend can
//         die outright on a brace once it has synthesized a few utterances.
//   ) ] * ^ `  truncate the Russian utterance at that point, dropping everything after
//         it; the backtick silences it completely.
// Replacing them with a space costs nothing: a SAPI client that wants a symbol spoken
// sends its name as text, and at low punctuation levels it should be silent anyway.
[[nodiscard]] bool is_unsafe(wchar_t c, const engine_info& eng)
{
    if (c == L'~') {
        return true;
    }
    if (eng.commands == cmd_mode::classic) {
        return false;
    }
    if (c == L'{' || c == L'}') {
        return true;
    }
    if (eng.translit == translit_mode::cyrillic) {
        return c == L')' || c == L']' || c == L'*' || c == L'^' || c == L'`';
    }
    return false;
}

void fold_unicode(std::wstring& s)
{
    for (auto& c : s) {
        switch (c) {
            case 0x2018: case 0x2019: case 0x201A: case 0x201B: c = L'\''; break;
            case 0x201C: case 0x201D: case 0x201E: case 0x201F: c = L'"';  break;
            case 0x00A0: case 0x2007: case 0x202F: case 0x3000: c = L' ';  break;
            case 0x2011: case 0x2012: c = L'-'; break;
            case 0xFEFF: case 0x200B: case 0x200C: case 0x200D: c = L' '; break;
            default: break;
        }
    }
}

void replace_all(std::wstring& s, const wchar_t* from, const wchar_t* to)
{
    const size_t from_len = wcslen(from);
    const size_t to_len = wcslen(to);
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::wstring::npos) {
        s.replace(pos, from_len, to);
        pos += to_len;
    }
}

void collapse_spaces(std::wstring& s)
{
    std::wstring out;
    out.reserve(s.size());
    bool prev_space = false;
    for (wchar_t c : s) {
        const bool sp = (c == L' ');
        if (sp && prev_space) {
            continue;
        }
        out += c;
        prev_space = sp;
    }
    s.swap(out);
}

// A dot between digits is sentence punctuation to these frontends ("7.1" becomes
// "seven. <pause> one"), so decimals, version strings and ip addresses are rewritten
// into words before anything else touches the text.
void spell_decimals(std::wstring& s, const wchar_t* word)
{
    std::wstring out;
    out.reserve(s.size() + 16);
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'.' && i > 0 && i + 1 < s.size() && is_digit(s[i - 1]) && is_digit(s[i + 1])) {
            out += L' ';
            out += word;
            out += L' ';
        } else {
            out += s[i];
        }
    }
    s.swap(out);
}

// Two v2 frontend quirks. Its normalizer expands apostrophe-s into "is", so possessives
// read as "Ivan is"; dropping just the apostrophe gives the identical-sounding plain
// form while other contractions stay untouched. And a dash is vocalized as a stray "oo",
// where a comma is what an em dash means in prose anyway.
void fix_v2_quirks(std::wstring& s)
{
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const wchar_t c = s[i];
        if ((c == L'\'' || c == 0x2019) && i + 1 < s.size() && lower_ascii(s[i + 1]) == L's' &&
            i > 0 && (iswalnum(s[i - 1]) != 0) &&
            (i + 2 >= s.size() || iswalnum(s[i + 2]) == 0)) {
            continue;  // drop the apostrophe, keep the s
        }
        if (c == 0x2013 || c == 0x2014 || c == 0x2015) {
            while (!out.empty() && out.back() == L' ') {
                out.pop_back();
            }
            out += L", ";
            while (i + 1 < s.size() && (s[i + 1] == L' ' || s[i + 1] == 0x2013 ||
                                        s[i + 1] == 0x2014 || s[i + 1] == 0x2015)) {
                ++i;
            }
            continue;
        }
        out += c;
    }
    s.swap(out);
}

}  // namespace

std::wstring number_to_words(unsigned long long n, number_mode mode)
{
    switch (mode) {
        case number_mode::greek:    return greek_number(n);
        case number_mode::polish:   return polish_number(n);
        case number_mode::japanese: return japanese_number(n);
        default:                    return std::wstring();
    }
}

std::wstring to_greek(const std::wstring& text)
{
    return transliterate(text, gre_multi, sizeof(gre_multi) / sizeof(gre_multi[0]),
                         gre_single, gre_names);
}

std::wstring to_cyrillic(const std::wstring& text)
{
    return transliterate(text, cyr_multi, sizeof(cyr_multi) / sizeof(cyr_multi[0]),
                         cyr_single, cyr_names);
}

std::wstring prepare(const std::wstring& text, const engine_info& eng)
{
    std::wstring s = text;

    fold_unicode(s);
    replace_all(s, L"\x2026", L"...");

    // Line breaks and control characters are not speech; they become word gaps.
    for (auto& c : s) {
        if (c < 0x20 || c == 0x7F) {
            c = L' ';
        }
    }

    if (eng.commands != cmd_mode::classic) {
        fix_v2_quirks(s);
    }

    spell_decimals(s, eng.decimal_word);
    s = localize_numbers(s, eng.numbers);

    switch (eng.translit) {
        case translit_mode::greek:    s = to_greek(s); break;
        case translit_mode::cyrillic: s = to_cyrillic(s); break;
        default: break;
    }

    // Greek writes its question mark as a semicolon, and only the semicolon actually
    // produces the questioning pause; a question mark is ignored entirely.
    if (eng.translit == translit_mode::greek) {
        std::replace(s.begin(), s.end(), L'?', L';');
    }

    for (auto& c : s) {
        if (is_unsafe(c, eng)) {
            c = L' ';
        }
    }

    collapse_spaces(s);
    return s;
}

std::wstring prepare_spelled(const std::wstring& text, const engine_info& eng)
{
    // Space the characters out so the frontend reads each on its own rather than
    // running them together into an invented word.
    std::wstring spaced;
    spaced.reserve(text.size() * 2);
    for (wchar_t c : text) {
        if (c == L' ') {
            continue;
        }
        spaced += c;
        spaced += L' ';
    }
    return prepare(spaced, eng);
}

std::string encode(const std::wstring& text, const engine_info& eng)
{
    if (text.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(eng.codepage, 0, text.c_str(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(needed), '\0');
    // The classic engine reads windows-1252, which has no default-char rules of its own;
    // anything outside it becomes a question mark rather than being dropped silently.
    const char* fallback = (eng.codepage == CP_UTF8) ? nullptr : "?";
    WideCharToMultiByte(eng.codepage, 0, text.c_str(), static_cast<int>(text.size()),
                        out.data(), needed, fallback, nullptr);
    return out;
}

std::wstring command_prefix(const engine_info& eng, const voice_info& voice,
                            int native_rate, int pitch_hz, int gain_db)
{
    if (eng.commands == cmd_mode::none) {
        return {};
    }

    native_rate = std::clamp(native_rate, RATE_MAX_PARAM, RATE_MIN_PARAM);
    pitch_hz = std::clamp(pitch_hz, PITCH_MIN_HZ, PITCH_MAX_HZ);
    gain_db = std::clamp(gain_db, GAIN_MIN_DB, GAIN_MAX_DB);

    std::wstring out;

    // Order matters here, and not obviously so. Several of these commands recompute the
    // voice model as a side effect and silently discard settings made before them:
    // ~v resizing the vocal tract resets the fundamental frequency, and ~f setting that
    // frequency in turn resets the inflection. Sending them in any order that puts ~f
    // before ~v, or ~h before ~f, makes those settings do nothing at all -- which is
    // exactly how the pitch control came to be dead in an earlier build. This sequence
    // was checked one command at a time against the real engines: with it, every
    // command a given engine understands still changes the audio.
    //
    // Gain leads, so that if any engine ever fails to recognize a later command and
    // reads it out as text instead, that text is at least spoken at the right volume.
    out += L"~g" + std::to_wstring(gain_db) + L"]";
    out += L"~r" + std::to_wstring(native_rate) + L"]";

    // ~v headsize is ignored by every v2 dll, so only the classic engine receives it;
    // elsewhere it would just eat into a 128 byte phrase buffer.
    if (eng.commands == cmd_mode::classic) {
        out += L"~v" + std::to_wstring(voice.headsize) + L"]";
    }
    out += L"~e" + std::to_wstring(voice.excitation) + L"]";
    out += L"~u" + std::to_wstring(voice.unvoiced) + L"]";
    out += L"~f" + std::to_wstring(pitch_hz) + L"]";

    // German is the one engine with no inflection command; it reads ~h out as text.
    if (eng.inflection) {
        out += L"~h" + std::to_wstring(voice.inflection) + L"]";
    }
    return out;
}

}
}
