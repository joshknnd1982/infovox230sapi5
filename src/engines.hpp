#pragma once

#include <windows.h>
#include <cstddef>

namespace Bestspeech {

// How a build's text frontend treats inline tilde commands. Established empirically
// (byte comparison of the synthesized pcm, per command, per dll) and re-checked by
// tools/verify_engines.py:
//   classic : the 1994 engine, every command works including ~v headsize.
//   tilde   : every command works except ~v headsize, which is silently ignored. The
//             Dutch dll additionally ignores ~e excitation, which is why its Bruno and
//             Ghost voices -- alike but for excitation -- come out identical there.
//   none    : commands must not be sent at all. Polish reads them out as text, Japanese
//             vocalizes a short artifact per command while applying no effect, and Greek
//             strips them along with all other non-Greek text. For these, rate, pitch and
//             volume are realized outside the engine instead.
enum class cmd_mode { classic, tilde, none };

// Latin text has to be rewritten into the engine's own script before it reaches these
// two frontends, which drop every ascii letter silently.
enum class translit_mode { none, greek, cyrillic };

// Frontends that cannot read ascii digits: Greek and Japanese ignore them outright,
// Polish only spells them one at a time instead of reading the number.
enum class number_mode { native, greek, polish, japanese };

struct engine_info
{
    const char*   id;             // short stable id, used in registry token ids
    const wchar_t* display;       // language name shown in the voice list
    const wchar_t* dll;           // engine dll, loaded from the install directory
    const wchar_t* lcid;          // SAPI Language attribute, hex, no 0x prefix
    LANGID        langid;         // same value, numeric
    UINT          codepage;       // CP_UTF8, or 1252 for the classic engine
    cmd_mode      commands;
    // The German frontend does not recognize ~h at all: instead of setting the
    // inflection it reads the command out as text, so a stray "h nought" is spoken
    // ahead of every utterance, at the engine's default gain because the ~g that
    // follows has not been applied yet. Every other engine obeys it.
    bool          inflection;
    translit_mode translit;
    number_mode   numbers;
    DWORD         sample_rate;    // measured; the v2 dlls do not all share one
    int           gain_trim_db;   // engine loudness offset, see below
    int           voice_count;    // "none" engines ignore every voice command, so they
                                  // have exactly one voice rather than fourteen alike
    const wchar_t* decimal_word;  // a dot between digits is read as sentence punctuation,
                                  // so decimals are spelled out before anything else
    wchar_t       group_sep;      // thousands separator this frontend groups with
};

// The classic engine peaks near full scale while the v2 dlls are far quieter (Russian
// peaks below a fifth of full scale), so each v2 engine carries a fixed trim to bring
// its loudness into line before the SAPI volume setting is applied on top. Measured
// across all fourteen voices: +12 dB pins the loudest of them against the engine's
// internal limiter, so the trim stops just short of that and keeps some headroom.
inline constexpr int V2_GAIN_TRIM = 10;

inline constexpr engine_info engines[] = {
    { "classic", L"English (Classic 1994)", L"b32_tts.dll", L"409", 0x0409, 1252,
      cmd_mode::classic, true,  translit_mode::none,     number_mode::native,   11025, 0,             14, L"point",     L',' },
    { "eng",     L"English",                L"dll_eng.dll", L"409", 0x0409, CP_UTF8,
      cmd_mode::tilde,   true,  translit_mode::none,     number_mode::native,   10800, V2_GAIN_TRIM,  14, L"point",     L',' },
    { "dut",     L"Dutch",                  L"dll_dut.dll", L"413", 0x0413, CP_UTF8,
      cmd_mode::tilde,   true,  translit_mode::none,     number_mode::native,   10800, V2_GAIN_TRIM,  14, L"komma",     L'.' },
    { "fre",     L"French",                 L"dll_fre.dll", L"40c", 0x040C, CP_UTF8,
      cmd_mode::tilde,   true,  translit_mode::none,     number_mode::native,   10800, V2_GAIN_TRIM,  14, L"virgule",   L' ' },
    { "ger",     L"German",                 L"dll_ger.dll", L"407", 0x0407, CP_UTF8,
      cmd_mode::tilde,   false, translit_mode::none,     number_mode::native,   10800, V2_GAIN_TRIM,  14, L"Komma",     L'.' },
    { "gre",     L"Greek",                  L"dll_gre.dll", L"408", 0x0408, CP_UTF8,
      cmd_mode::none,    true,  translit_mode::greek,    number_mode::greek,    10800, V2_GAIN_TRIM,   1, L"\u03ba\u03cc\u03bc\u03bc\u03b1", L'.' },
    { "heb",     L"Hebrew",                 L"dll_heb.dll", L"40d", 0x040D, CP_UTF8,
      cmd_mode::tilde,   true,  translit_mode::none,     number_mode::native,   10800, V2_GAIN_TRIM,  14, L"\u05e0\u05e7\u05d5\u05d3\u05d4", L',' },
    { "ita",     L"Italian",                L"dll_ita.dll", L"410", 0x0410, CP_UTF8,
      cmd_mode::tilde,   true,  translit_mode::none,     number_mode::native,   10800, V2_GAIN_TRIM,  14, L"virgola",   L'.' },
    { "jpn",     L"Japanese",               L"dll_jpn.dll", L"411", 0x0411, CP_UTF8,
      cmd_mode::none,    true,  translit_mode::none,     number_mode::japanese, 10800, V2_GAIN_TRIM,   1, L"\u3066\u3093",  L',' },
    { "pol",     L"Polish",                 L"dll_pol.dll", L"415", 0x0415, CP_UTF8,
      cmd_mode::none,    true,  translit_mode::none,     number_mode::polish,   10800, V2_GAIN_TRIM,   1, L"przecinek", L' ' },
    { "por",     L"Portuguese",             L"dll_por.dll", L"816", 0x0816, CP_UTF8,
      cmd_mode::tilde,   true,  translit_mode::none,     number_mode::native,   10800, V2_GAIN_TRIM,  14, L"v\u00edrgula", L'.' },
    { "rus",     L"Russian",                L"dll_rus.dll", L"419", 0x0419, CP_UTF8,
      cmd_mode::tilde,   true,  translit_mode::cyrillic, number_mode::native,   10800, V2_GAIN_TRIM,  14, L"\u0437\u0430\u043f\u044f\u0442\u0430\u044f", L' ' },
    { "spa",     L"Spanish",                L"dll_spa.dll", L"40a", 0x040A, CP_UTF8,
      cmd_mode::tilde,   true,  translit_mode::none,     number_mode::native,   10800, V2_GAIN_TRIM,  14, L"punto",     L'.' },
};

inline constexpr int engine_count = static_cast<int>(sizeof(engines) / sizeof(engines[0]));

// Arabic (dll_ara.dll) is deliberately absent even where the file is present: that dll's
// synthesis core is a stub which emits the same short buffer of digital silence, peak
// amplitude zero, whatever text it is given, in either Latin or Arabic script.

struct voice_info
{
    const wchar_t* name;
    int  headsize;      // ~v, classic only
    int  excitation;    // ~e
    int  inflection;    // ~h, classic only
    int  unvoiced;      // ~u
    int  pitch;         // ~f, in hz
    bool is_female;
};

// The fourteen character voices, by Rommix. Every one was confirmed to produce audibly
// distinct output on both the classic engine and the v2 dlls.
inline constexpr voice_info voices[] = {
    { L"Fred",    1, 3,   0,   0,  80, false },
    { L"Sara",    2, 3, -20,   0, 175, true  },
    { L"Hary",    3, 3,  10,   0,  65, false },
    { L"Wendy",   2, 1,  50,   0, 150, true  },
    { L"Dexter",  6, 6,   0, -25,  90, false },
    { L"Alien",   4, 6, -50, -20, 115, false },
    { L"Kit",     5, 3,  40,   0, 230, true  },
    { L"Bruno",   3, 3,  50,   0,  60, false },
    { L"Ghost",   3, 2,  50,   0,  60, false },
    { L"Peeper",  2, 2,   0,   5,  80, false },
    { L"Dracula", 3, 3,  45,  -5,  47, false },
    { L"Granny",  4, 3, -60,   0, 350, true  },
    { L"Martha",  6, 4, 100,  -5, 300, true  },
    { L"Tim",     3, 4, -10,   0,  60, false },
};

inline constexpr int voice_count = static_cast<int>(sizeof(voices) / sizeof(voices[0]));

// Engine parameter ranges, from the original driver documentation.
inline constexpr int RATE_MIN_PARAM   =  200;   // slowest ~r
inline constexpr int RATE_MAX_PARAM   =  -90;   // fastest ~r
inline constexpr int PITCH_MIN_HZ     =   43;
inline constexpr int PITCH_MAX_HZ     =  413;
inline constexpr int GAIN_MIN_DB      =  -68;
inline constexpr int GAIN_MAX_DB      =   12;

// Total number of SAPI voice tokens this engine publishes.
inline constexpr int total_token_count()
{
    int n = 0;
    for (int i = 0; i < engine_count; ++i) {
        n += engines[i].voice_count;
    }
    return n;
}

// Map a flat token index onto its (engine, voice) pair.
inline constexpr bool token_at(int index, int& engine_out, int& voice_out)
{
    for (int e = 0; e < engine_count; ++e) {
        if (index < engines[e].voice_count) {
            engine_out = e;
            voice_out = index;
            return true;
        }
        index -= engines[e].voice_count;
    }
    return false;
}

inline int engine_by_id(const char* id)
{
    if (!id) {
        return -1;
    }
    for (int i = 0; i < engine_count; ++i) {
        const char* a = engines[i].id;
        const char* b = id;
        while (*a && *a == *b) { ++a; ++b; }
        if (!*a && !*b) {
            return i;
        }
    }
    return -1;
}

}
