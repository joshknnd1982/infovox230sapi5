#pragma once

// The voice catalogue: the engine's mode table, plus whatever the user has
// added.
//
// Every field here is a value the Infovox 230 engine reads out of its
// configuration, spelled exactly as the engine spells it. The catalogue is
// written into the virtual registry before the engine loads, so this table --
// not anything installed on the machine -- is what the engine enumerates.
//
// The 60 built-in entries are the stock modes: twelve languages
// (American and British English, Danish, Dutch, Finnish, French, German,
// Icelandic, Italian, Norwegian, Castilian Spanish, Swedish) times five
// speakers (Male, Female, Child, Giant, Zombie). The speakers differ only in
// the four synthesis parameters Pitch, Dynamic, Aspiration and FormantNo, which
// is why users can define their own: see load_user_voices().

#include <string>
#include <vector>

#include "ivx_settings.h"

namespace ivx {

// A row of the generated table in ivx_voices.inc. Kept as plain char pointers
// so the table is const data with no start-up cost.
struct BuiltinVoice {
    const char* mode_key;
    const char* display_name;
    const char* language_name;
    const char* mode_guid;
    const char* language_id;
    unsigned short lcid;
    const char* language_file;
    const char* library_file;
    const char* phsym_file;
    const char* diphone_file;
    const char* mapping_file;
    const char* speaker_name;
    const char* speaker_style;
    const char* gender;
    const char* age;
    const char* pitch;
    const char* dynamic;
    const char* aspiration;
    const char* formant_no;
};

struct Voice {
    std::string mode_key;      // the engine's key name under Modes
    std::string display_name;  // what the Windows voice list shows
    std::string language_name; // the prefix the engine requires of mode_key
    std::string mode_guid;     // "{...}", the id Select() takes
    std::string language_id;   // as the engine wants it, decimal
    unsigned short lcid = 0;   // full LCID for the SAPI5 token
    std::string language_file;
    std::string library_file;
    std::string phsym_file;
    std::string diphone_file;
    std::string mapping_file;
    std::string speaker_name;
    std::string speaker_style;
    std::string gender;  // "1" female, "2" male, as the engine numbers them
    std::string age;
    std::string pitch;       // per-voice synthesis parameters; empty means
    std::string dynamic;     // "leave the engine's default for this language"
    std::string aspiration;
    std::string formant_no;
    bool user_defined = false;

    // SAPI5 token attributes derived from the above.
    std::wstring sapi_name() const;
    std::wstring sapi_gender() const;
    std::wstring sapi_age() const;
    std::wstring sapi_language() const;  // LCID in hex, no leading zeros
};

class Catalog {
public:
    // Built-ins, then any user voices found in `voices.ini` next to the module
    // and in %LOCALAPPDATA%\Infovox230SAPI\voices.ini. A user entry whose
    // section name matches a built-in display name overrides that voice rather
    // than adding another.
    void load(const std::wstring& module_dir);

    const std::vector<Voice>& voices() const { return voices_; }

    // The [Settings] section of those same files: everything that is not a
    // property of one voice.
    const EngineSettings& settings() const { return settings_; }

    size_t size() const { return voices_.size(); }

    // Index of the voice whose display name matches, or -1.
    int find_by_name(const std::wstring& display_name) const;
    int find_by_mode_guid(const std::string& guid, int except = -1) const;

    // The name the engine is given. `except` skips one entry, so a voice being
    // built can test its own candidate name for collisions.
    int find_by_mode_key(const std::string& mode_key, int except = -1) const;

    // First voice whose LanguageFile matches, which is how a user-defined
    // voice's language -- and so the prefix its engine name must carry -- is
    // worked out.
    int find_by_language_file(const std::string& language_file) const;

    // Write the whole catalogue, plus the engine's own directory settings, into
    // the virtual registry. `engine_dir` is where Ivx230nt.dll and the .ivx rule
    // files live; the engine resolves every data file relative to it.
    void seed_virtual_registry(const std::string& engine_dir) const;

private:
    void load_user_voices(const std::wstring& ini_path);

    std::vector<Voice> voices_;
    EngineSettings settings_;
};

}  // namespace ivx
