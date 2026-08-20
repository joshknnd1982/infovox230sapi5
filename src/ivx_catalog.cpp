#include "ivx_catalog.h"

#include <windows.h>

#include <cstdio>

#include "ivx_log.h"
#include "ivx_vregistry.h"

namespace ivx {
namespace {

#include "ivx_voices.inc"

// Where the engine expects its configuration to live. Only the name matters;
// nothing under it ever reaches the real registry.
constexpr char kEngineRoot[] = "Software\\Babel-Infovox AB\\Infovox 230";
constexpr char kModesRoot[] = "Software\\Babel-Infovox AB\\Infovox 230\\Modes";

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

std::string narrow(const std::wstring& s)
{
    if (s.empty()) {
        return std::string();
    }
    const int n = WideCharToMultiByte(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0,
                                      nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n, nullptr,
                        nullptr);
    return out;
}

std::wstring trim(const std::wstring& s)
{
    size_t b = s.find_first_not_of(L" \t\r\n");
    if (b == std::wstring::npos) {
        return std::wstring();
    }
    size_t e = s.find_last_not_of(L" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::wstring ini_string(const wchar_t* path, const wchar_t* section, const wchar_t* key,
                        const wchar_t* fallback)
{
    wchar_t buf[512];
    GetPrivateProfileStringW(section, key, fallback, buf, 512, path);
    return trim(buf);
}

// Trims a display name down to something that can follow a language name: the
// "Infovox" prefix and any language name already in it are redundant, and only
// letters, digits and single spaces survive.
std::string mode_suffix(const std::string& display, const std::string& language)
{
    std::string text = display;
    auto strip_prefix = [&text](const std::string& prefix) {
        if (prefix.empty() || text.size() <= prefix.size()) {
            return;
        }
        if (_strnicmp(text.c_str(), prefix.c_str(), prefix.size()) == 0 &&
            text[prefix.size()] == ' ') {
            text.erase(0, prefix.size() + 1);
        }
    };
    strip_prefix("Infovox");
    strip_prefix(language);

    std::string out;
    bool space_pending = false;
    for (char ch : text) {
        if (isalnum(static_cast<unsigned char>(ch))) {
            if (space_pending && !out.empty()) {
                out += ' ';
            }
            space_pending = false;
            out += ch;
        } else {
            space_pending = true;
        }
    }
    return out.empty() ? std::string("Custom") : out;
}

// The id for a user-defined voice.
//
// The engine will not take just any GUID: the stock ids are all
// {c9c5eda0-7c89-11d0-03LL-SS0000000000}, and a mode carrying an id of a
// different shape is dropped without a word. Measurement (probe_custom_modes.py)
// shows the engine accepts LL=0xFF -- a language slot it does not use itself --
// with SS free, which gives 256 ids that cannot collide with a stock voice.
std::string synth_mode_guid(unsigned slot)
{
    char buf[64];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "{c9c5eda0-7c89-11d0-03ff-%02x0000000000}",
                slot & 0xFF);
    return buf;
}

// Where in those 256 slots a voice starts looking. Derived from the name so a
// voice keeps the same id as long as its name does not change, which keeps a
// token an application has remembered pointing at the same voice.
unsigned slot_for(const std::wstring& name)
{
    unsigned long long hash = 1469598103934665603ULL;  // FNV-1a
    for (wchar_t ch : name) {
        hash ^= static_cast<unsigned long long>(ch);
        hash *= 1099511628211ULL;
    }
    return static_cast<unsigned>(hash & 0xFF);
}

bool file_exists(const std::wstring& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring local_appdata_ini()
{
    wchar_t base[MAX_PATH];
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH) == 0) {
        return std::wstring();
    }
    return std::wstring(base) + L"\\Infovox230SAPI\\voices.ini";
}

}  // namespace

std::wstring Voice::sapi_name() const
{
    return widen(display_name);
}

std::wstring Voice::sapi_gender() const
{
    // The engine numbers genders the SAPI4 way: 1 female, 2 male.
    if (gender == "1") {
        return L"Female";
    }
    if (gender == "2") {
        return L"Male";
    }
    return L"Neutral";
}

std::wstring Voice::sapi_age() const
{
    const int years = age.empty() ? 30 : atoi(age.c_str());
    if (years <= 12) {
        return L"Child";
    }
    if (years <= 19) {
        return L"Teen";
    }
    if (years >= 60) {
        return L"Senior";
    }
    return L"Adult";
}

std::wstring Voice::sapi_language() const
{
    wchar_t buf[16];
    _snwprintf_s(buf, 16, _TRUNCATE, L"%x", static_cast<unsigned>(lcid));
    return buf;
}

void Catalog::load(const std::wstring& module_dir)
{
    voices_.clear();
    voices_.reserve(_countof(kBuiltinVoices));

    for (const BuiltinVoice& b : kBuiltinVoices) {
        Voice v;
        v.mode_key = b.mode_key;
        v.display_name = b.display_name;
        v.language_name = b.language_name;
        v.mode_guid = b.mode_guid;
        v.language_id = b.language_id;
        v.lcid = b.lcid;
        v.language_file = b.language_file;
        v.library_file = b.library_file;
        v.phsym_file = b.phsym_file;
        v.diphone_file = b.diphone_file;
        v.mapping_file = b.mapping_file;
        v.speaker_name = b.speaker_name;
        v.speaker_style = b.speaker_style;
        v.gender = b.gender;
        v.age = b.age;
        v.pitch = b.pitch;
        v.dynamic = b.dynamic;
        v.aspiration = b.aspiration;
        v.formant_no = b.formant_no;
        voices_.push_back(std::move(v));
    }
    IVX_INFO("catalog: %u built-in voices", static_cast<unsigned>(voices_.size()));

    if (!module_dir.empty()) {
        load_user_voices(module_dir + L"\\voices.ini");
    }
    load_user_voices(local_appdata_ini());
}

void Catalog::load_user_voices(const std::wstring& ini_path)
{
    if (ini_path.empty() || !file_exists(ini_path)) {
        IVX_DEBUG("catalog: no user voices at %S", ini_path.c_str());
        return;
    }
    IVX_INFO("catalog: reading user voices from %S", ini_path.c_str());

    // GetPrivateProfileSectionNames returns a double-NUL terminated list.
    std::vector<wchar_t> names(32768);
    const DWORD n =
        GetPrivateProfileSectionNamesW(names.data(), static_cast<DWORD>(names.size()), ini_path.c_str());
    if (n == 0) {
        IVX_WARN("catalog: %S has no sections", ini_path.c_str());
        return;
    }

    int added = 0;
    int overridden = 0;
    for (const wchar_t* section = names.data(); *section; section += wcslen(section) + 1) {
        const std::wstring name = trim(section);
        if (name.empty() || _wcsicmp(name.c_str(), L"Settings") == 0) {
            continue;  // [Settings] is reserved for engine-wide options
        }

        int index = find_by_name(name);
        const bool is_new = index < 0;
        if (is_new) {
            // A new voice inherits from a template so a short section only has
            // to name the parameters it wants to change. Template defaults to
            // the first built-in voice of the same language, if BasedOn names
            // one; otherwise the very first built-in.
            const std::wstring based_on =
                ini_string(ini_path.c_str(), name.c_str(), L"BasedOn", L"");
            int base = based_on.empty() ? -1 : find_by_name(based_on);
            if (base < 0 && !based_on.empty()) {
                IVX_WARN("catalog: [%S] BasedOn=\"%S\" does not name a known voice; "
                         "using the first built-in instead",
                         name.c_str(), based_on.c_str());
            }
            Voice v = voices_[base >= 0 ? static_cast<size_t>(base) : 0];
            v.user_defined = true;
            v.display_name = narrow(name);
            v.speaker_name = v.display_name;
            voices_.push_back(std::move(v));
            index = static_cast<int>(voices_.size()) - 1;
            ++added;
        } else {
            ++overridden;
        }

        Voice& v = voices_[static_cast<size_t>(index)];
        struct Field {
            const wchar_t* key;
            std::string* target;
        };
        const Field fields[] = {
            {L"LanguageID", &v.language_id},   {L"LanguageFile", &v.language_file},
            {L"LibraryFile", &v.library_file}, {L"PhSymFile", &v.phsym_file},
            {L"DiphoneFile", &v.diphone_file}, {L"MappingFile", &v.mapping_file},
            {L"SpeakerName", &v.speaker_name}, {L"SpeakerStyle", &v.speaker_style},
            {L"Gender", &v.gender},            {L"Age", &v.age},
            {L"Pitch", &v.pitch},              {L"Dynamic", &v.dynamic},
            {L"Aspiration", &v.aspiration},    {L"FormantNo", &v.formant_no},
        };
        for (const Field& f : fields) {
            const std::wstring value = ini_string(ini_path.c_str(), name.c_str(), f.key, L"\x01");
            if (value != L"\x01") {
                *f.target = narrow(value);
            }
        }

        const std::wstring lcid_text = ini_string(ini_path.c_str(), name.c_str(), L"LCID", L"");
        if (!lcid_text.empty()) {
            v.lcid = static_cast<unsigned short>(wcstoul(lcid_text.c_str(), nullptr, 16));
        } else if (!v.language_id.empty()) {
            const unsigned long lid = strtoul(v.language_id.c_str(), nullptr, 10);
            v.lcid = static_cast<unsigned short>(lid > 0x3FF ? lid : (0x0400 | lid));
        }

        if (is_new) {
            // The name in the voice list is free, but the name the ENGINE is
            // given is not: it only accepts a mode whose key begins with the
            // name of that mode's own language, and silently drops anything
            // else -- which is why a voice called "Infovox Something" never
            // reached the enumerator. Built after the settings are applied, so
            // that a section which changes LanguageFile gets the right prefix.
            const int lang = find_by_language_file(v.language_file);
            if (lang >= 0) {
                v.language_name = voices_[static_cast<size_t>(lang)].language_name;
            }
            const std::string suffix = mode_suffix(v.display_name, v.language_name);
            v.mode_key = v.language_name + " " + suffix;
            for (int attempt = 2; find_by_mode_key(v.mode_key, index) >= 0; ++attempt) {
                char tail[16];
                _snprintf_s(tail, sizeof(tail), _TRUNCATE, " %d", attempt);
                v.mode_key = v.language_name + " " + suffix + tail;
            }
            // SpeakerName is the value the engine actually checks. Watching it
            // read a mode's settings shows it stop dead at SpeakerName when the
            // value does not begin with a language it knows -- it never gets as
            // far as Pitch -- and the mode is then dropped without a word. So
            // the engine is given the language-prefixed name, and the user's own
            // name is what the voice list shows.
            v.speaker_name = v.mode_key;

            const unsigned start = slot_for(name);
            for (unsigned probe = 0; probe < 256; ++probe) {
                v.mode_guid = synth_mode_guid(start + probe);
                if (find_by_mode_guid(v.mode_guid, index) < 0) {
                    break;
                }
            }
            IVX_DEBUG("catalog: \"%s\" is presented to the engine as \"%s\" %s",
                      v.display_name.c_str(), v.mode_key.c_str(), v.mode_guid.c_str());
        }

        IVX_INFO("catalog: %s voice \"%s\" lang=%s file=%s pitch=%s dyn=%s asp=%s formant=%s",
                 is_new ? "added" : "overrode", v.display_name.c_str(), v.language_id.c_str(),
                 v.language_file.c_str(), v.pitch.c_str(), v.dynamic.c_str(), v.aspiration.c_str(),
                 v.formant_no.c_str());
    }
    IVX_INFO("catalog: %d user voices added, %d built-ins overridden, %u total", added, overridden,
             static_cast<unsigned>(voices_.size()));
}

int Catalog::find_by_name(const std::wstring& display_name) const
{
    const std::string want = narrow(display_name);
    for (size_t i = 0; i < voices_.size(); ++i) {
        if (_stricmp(voices_[i].display_name.c_str(), want.c_str()) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Catalog::find_by_mode_key(const std::string& mode_key, int except) const
{
    for (size_t i = 0; i < voices_.size(); ++i) {
        if (static_cast<int>(i) == except) {
            continue;
        }
        if (_stricmp(voices_[i].mode_key.c_str(), mode_key.c_str()) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Catalog::find_by_language_file(const std::string& language_file) const
{
    for (size_t i = 0; i < voices_.size(); ++i) {
        if (!voices_[i].user_defined &&
            _stricmp(voices_[i].language_file.c_str(), language_file.c_str()) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Catalog::find_by_mode_guid(const std::string& guid, int except) const
{
    for (size_t i = 0; i < voices_.size(); ++i) {
        if (static_cast<int>(i) == except) {
            continue;
        }
        if (_stricmp(voices_[i].mode_guid.c_str(), guid.c_str()) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void Catalog::seed_virtual_registry(const std::string& engine_dir) const
{
    VirtualRegistry& vr = VirtualRegistry::instance();

    // Every directory the engine looks in. It uses different names in different
    // code paths, so all of them are set to the folder holding the rule files.
    const char* const dir_values[] = {"LanguageDir",       "LanguageDirectory", "LexiconDir",
                                      "LicenseDir",        "LogStartupDir",     "Path"};
    for (const char* name : dir_values) {
        vr.set_string(kEngineRoot, name, engine_dir);
    }
    IVX_INFO("vreg: engine directories point at %s", engine_dir.c_str());

    for (const Voice& v : voices_) {
        const std::string path = std::string(kModesRoot) + "\\" + v.mode_key;
        vr.set_string(path, "ModeGUID", v.mode_guid);
        vr.set_string(path, "LanguageID", v.language_id);
        vr.set_string(path, "LanguageFile", v.language_file);
        vr.set_string(path, "LibraryFile", v.library_file);
        vr.set_string(path, "PhSymFile", v.phsym_file);
        vr.set_string(path, "DiphoneFile", v.diphone_file);
        vr.set_string(path, "MappingFile", v.mapping_file);
        vr.set_string(path, "SpeakerName", v.speaker_name);
        vr.set_string(path, "SpeakerStyle", v.speaker_style);
        vr.set_string(path, "Gender", v.gender);
        vr.set_string(path, "Age", v.age);
        vr.set_string(path, "Pitch", v.pitch);
        vr.set_string(path, "Dynamic", v.dynamic);
        vr.set_string(path, "Aspiration", v.aspiration);
        vr.set_string(path, "FormantNo", v.formant_no);
    }
    IVX_INFO("vreg: %u modes written into the in-memory configuration",
             static_cast<unsigned>(voices_.size()));
}

}  // namespace ivx
