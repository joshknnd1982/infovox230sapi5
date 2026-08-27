#pragma once

// Engine-wide settings: everything that is not a property of one voice.
//
// They live in the [Settings] section of the same voices.ini files the voices
// themselves come from -- the one beside the dll for everybody, and
// %LOCALAPPDATA%\Infovox230SAPI\voices.ini for one user -- read in that order,
// so a personal file overrides a machine-wide one key by key.
//
// Every part of the product that can act on a setting reads it from the
// catalogue it has already loaded, so nothing here needs a file of its own or a
// second trip to disk. The configuration utility writes the same keys, which is
// why their names are declared here rather than spelled out in two places.

#include <string>

namespace ivx {

struct EngineSettings {
    // The engine ends every utterance with about eight tenths of a second of
    // inaudible dither. Trimming it is what keeps a screen reader from pausing
    // for most of a second after everything it says; turn it off to hear
    // exactly what the engine produced.
    bool trim_trailing_silence = true;

    // A sample at or below this level counts as silence, out of a full scale of
    // 32767. The padding being trimmed sits at plus or minus one.
    int silence_threshold = 16;

    // Whether the engine reports where it is in the text. Word positions are
    // what a program uses to highlight the word being spoken.
    bool word_events = true;
    bool sentence_events = true;

    // How long an utterance may take before a wedged engine is given up on:
    // this many milliseconds, plus this many per character of text.
    int timeout_base_ms = 30000;
    int timeout_per_char_ms = 200;

    // Overrides for the range the engine reports for itself. Zero means "ask
    // the engine", which is what it normally does; setting them changes what
    // the rate and pitch controls in a speech program reach at their ends.
    int rate_min = 0;      // words per minute
    int rate_max = 0;
    int rate_default = 0;
    int pitch_min = 0;     // hertz
    int pitch_max = 0;
    int pitch_default = 0;

    // What the configuration utility speaks when previewing a voice.
    std::wstring preview_text;

    // Reads whichever of the keys this file names and leaves the rest as they
    // are, so a second file can override a first one key by key.
    void merge_from(const std::wstring& ini_path);
};

// The section and key names, shared by the loader above and the configuration
// utility that writes them, so the two cannot drift apart.
namespace settings_key {

extern const wchar_t kSection[];
extern const wchar_t kTrimTrailingSilence[];
extern const wchar_t kSilenceThreshold[];
extern const wchar_t kWordEvents[];
extern const wchar_t kSentenceEvents[];
extern const wchar_t kTimeoutBaseMs[];
extern const wchar_t kTimeoutPerCharMs[];
extern const wchar_t kRateMin[];
extern const wchar_t kRateMax[];
extern const wchar_t kRateDefault[];
extern const wchar_t kPitchMin[];
extern const wchar_t kPitchMax[];
extern const wchar_t kPitchDefault[];
extern const wchar_t kPreviewText[];

}  // namespace settings_key

// What the utility offers when nothing has been chosen yet.
extern const wchar_t kDefaultPreviewText[];

}  // namespace ivx
