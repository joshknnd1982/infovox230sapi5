#pragma once

// What the configuration utility edits: the voices.ini file, the languages the
// engine can speak, and the arithmetic that turns the engine's numbers into
// something a person can judge.
//
// The utility never invents a vocabulary of its own. Every key it writes is one
// the catalogue already reads (ivx_catalog.cpp) or one [Settings] already
// defines (ivx_settings.h), so a file written here and a file typed by hand are
// the same file.
//
// Editing is done through the Windows profile API rather than by rewriting the
// file, which keeps a hand-written file's comments and any key this utility
// does not know about exactly where they were.

#include <string>
#include <vector>

#include "ivx_catalog.h"
#include "ivx_settings.h"

namespace ivx {
namespace config {

// Every per-voice key the engine reads, in the order the editor shows them.
enum VoiceKey {
    KEY_BASED_ON = 0,
    KEY_LANGUAGE_FILE,
    KEY_LANGUAGE_ID,
    KEY_LCID,
    KEY_PITCH,
    KEY_DYNAMIC,
    KEY_ASPIRATION,
    KEY_FORMANT_NO,
    KEY_GENDER,
    KEY_AGE,
    KEY_SPEAKER_NAME,
    KEY_SPEAKER_STYLE,
    KEY_LIBRARY_FILE,
    KEY_PHSYM_FILE,
    KEY_DIPHONE_FILE,
    KEY_MAPPING_FILE,
    KEY_COUNT
};

// The name as it appears in the file, e.g. "FormantNo".
const wchar_t* voice_key_name(int key);

// One [section] of the file: a voice the user has added, or a built-in voice
// the user has changed.
struct VoiceEdit {
    std::wstring name;                 // the section name, and what Windows shows
    std::wstring values[KEY_COUNT];    // empty means "this file does not say"
    bool from_file = false;            // false until it has been written
    bool builtin = false;              // the name matches a built-in voice

    const std::wstring& get(int key) const { return values[key]; }
    void set(int key, const std::wstring& value) { values[key] = value; }
    bool has(int key) const { return !values[key].empty(); }
};

// One of the twelve languages the engine has rules for, taken from the built-in
// voice table rather than from a second list that could drift away from it.
struct Language {
    std::wstring name;          // "American English"
    std::wstring rule_file;     // "amrules.ivx"
    std::wstring language_id;   // "1033", as the engine wants it
    unsigned short lcid = 0;    // 0x0409
};

// Which voices.ini is being edited.
enum Scope {
    SCOPE_USER = 0,   // %LOCALAPPDATA%\Infovox230SAPI\voices.ini
    SCOPE_MACHINE,    // the one beside the dll, which every user sees
};

std::wstring ini_path_for(Scope scope);

// True if that file can be written without administrator rights. The
// machine-wide one normally cannot, and the answer decides whether the utility
// offers to restart itself elevated.
bool scope_is_writable(Scope scope);

// The voices.ini currently being edited. Nothing is written until save().
class VoiceFile {
public:
    // Reads the file if it is there. A file that is not there is not an error:
    // it is simply a set of voices no one has defined yet.
    void load(Scope scope);

    Scope scope() const { return scope_; }
    const std::wstring& path() const { return path_; }

    const std::vector<VoiceEdit>& entries() const { return entries_; }
    std::vector<VoiceEdit>& entries() { return entries_; }

    // -1 when the file says nothing about that name.
    int find(const std::wstring& name) const;

    // Adds or replaces the section for `edit`. Nothing reaches disk until
    // save() is called.
    void put(const VoiceEdit& edit);

    // Forgets a voice: a custom one disappears, a changed built-in goes back to
    // how it was made.
    void remove(const std::wstring& name);

    EngineSettings& settings() { return settings_; }
    const EngineSettings& settings() const { return settings_; }

    bool dirty() const { return dirty_; }
    void mark_dirty() { dirty_ = true; }

    // Writes every pending change. `error` is filled in with something a person
    // can act on when it returns false -- most often that the machine-wide file
    // needs administrator rights.
    bool save(std::wstring* error);

private:
    bool ensure_file_exists(std::wstring* error);

    Scope scope_ = SCOPE_USER;
    std::wstring path_;
    std::vector<VoiceEdit> entries_;
    std::vector<std::wstring> removed_;
    EngineSettings settings_;
    bool dirty_ = false;
};

// The twelve languages, in alphabetical order of name.
const std::vector<Language>& languages();

// The catalogue as the engine will see it once the file is saved: built-ins
// first, then whatever the user has added. Reloaded after every save so the
// list the utility shows is the list the engine will publish.
const Catalog& catalog();
void reload_catalog();

// The value a voice would have for one key if the file said nothing about it,
// worked out the way the catalogue works it out: from BasedOn if there is one,
// otherwise from the built-in of the same name, otherwise from the first
// built-in voice.
std::wstring inherited_value(const VoiceEdit& edit, int key);

// Base pitch to hertz, which is the number a person can actually judge. The
// engine computes 3 x Pitch - 49 and clamps the result to 30..250 Hz.
int pitch_to_hertz(int pitch);
int hertz_to_pitch(int hertz);

// "0 -- standard male", and so on, for the five vocal tract shapes.
const wchar_t* formant_description(int formant);

// Narrow and widen using the code page the catalogue uses, so a name survives
// the round trip through the file unchanged.
std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);

}  // namespace config
}  // namespace ivx
