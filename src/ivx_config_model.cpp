#include "ivx_config_model.h"

#include <windows.h>

#include <algorithm>

#include "ivx_log.h"
#include "ivx_paths.h"

namespace ivx {
namespace config {
namespace {

const wchar_t* const kVoiceKeyNames[KEY_COUNT] = {
    L"BasedOn",     L"LanguageFile", L"LanguageID",  L"LCID",
    L"Pitch",       L"Dynamic",      L"Aspiration",  L"FormantNo",
    L"Gender",      L"Age",          L"SpeakerName", L"SpeakerStyle",
    L"LibraryFile", L"PhSymFile",    L"DiphoneFile", L"MappingFile",
};

// A value no file can hold, so a key that is absent stays distinct from a key
// that is present and empty.
const wchar_t kAbsent[] = L"\x01";

std::wstring trim(const std::wstring& s)
{
    const size_t b = s.find_first_not_of(L" \t\r\n");
    if (b == std::wstring::npos) {
        return std::wstring();
    }
    const size_t e = s.find_last_not_of(L" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::wstring read_key(const std::wstring& path, const std::wstring& section, const wchar_t* key)
{
    wchar_t buf[1024];
    GetPrivateProfileStringW(section.c_str(), key, kAbsent, buf, _countof(buf), path.c_str());
    if (wcscmp(buf, kAbsent) == 0) {
        return std::wstring();
    }
    return trim(buf);
}

bool file_exists(const std::wstring& path)
{
    return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring parent_of(const std::wstring& path)
{
    const size_t cut = path.find_last_of(L'\\');
    return cut == std::wstring::npos ? std::wstring() : path.substr(0, cut);
}

// New files are created as UTF-16, which is what makes the profile API write
// UTF-16 into them afterwards. An ANSI file would lose any letter outside the
// machine's own code page from a voice name.
bool create_ini(const std::wstring& path, std::wstring* error)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        if (code == ERROR_FILE_EXISTS) {
            return true;
        }
        if (error) {
            *error = (code == ERROR_ACCESS_DENIED)
                         ? L"This file can only be written by an administrator."
                         : L"The file could not be created.";
        }
        return false;
    }

    static const wchar_t kHeader[] =
        L"\xFEFF"
        L"; voices.ini -- written by the Infovox 230 configuration utility.\r\n"
        L";\r\n"
        L"; You can edit this file by hand as well; the utility keeps your\r\n"
        L"; comments and any settings it does not know about. See\r\n"
        L"; voices.example.ini in the installation folder for what every setting\r\n"
        L"; means.\r\n"
        L"\r\n";
    DWORD written = 0;
    WriteFile(h, kHeader, static_cast<DWORD>(wcslen(kHeader) * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(h);
    return true;
}

bool write_key(const std::wstring& path, const std::wstring& section, const wchar_t* key,
               const wchar_t* value, std::wstring* error)
{
    if (WritePrivateProfileStringW(section.c_str(), key, value, path.c_str())) {
        return true;
    }
    const DWORD code = GetLastError();
    IVX_ERROR("config: cannot write [%S] %S to %S: %s", section.c_str(), key, path.c_str(),
              win_error(code));
    if (error && error->empty()) {
        *error = (code == ERROR_ACCESS_DENIED)
                     ? L"This file can only be written by an administrator."
                     : L"The settings could not be written to the file.";
    }
    return false;
}

std::wstring number(int value)
{
    wchar_t buf[24];
    _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%d", value);
    return buf;
}

}  // namespace

const wchar_t* voice_key_name(int key)
{
    return (key >= 0 && key < KEY_COUNT) ? kVoiceKeyNames[key] : L"";
}

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

std::wstring ini_path_for(Scope scope)
{
    if (scope == SCOPE_MACHINE) {
        return install_dir() + L"\\voices.ini";
    }
    return user_data_dir() + L"\\voices.ini";
}

bool scope_is_writable(Scope scope)
{
    const std::wstring path = ini_path_for(scope);
    if (path.empty()) {
        return false;
    }
    if (file_exists(path)) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            return false;
        }
        CloseHandle(h);
        return true;
    }

    // Not there yet, so the question is whether the folder will accept it.
    const std::wstring probe = parent_of(path) + L"\\ivx_write_test.tmp";
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    CloseHandle(h);
    return true;
}

void VoiceFile::load(Scope scope)
{
    scope_ = scope;
    path_ = ini_path_for(scope);
    entries_.clear();
    removed_.clear();
    dirty_ = false;

    // Settings are shown as the engine will see them: the machine-wide file
    // first, then the personal one, exactly as the catalogue merges them.
    settings_ = EngineSettings();
    settings_.merge_from(ini_path_for(SCOPE_MACHINE));
    settings_.merge_from(ini_path_for(SCOPE_USER));
    if (settings_.preview_text.empty()) {
        settings_.preview_text = kDefaultPreviewText;
    }

    if (!file_exists(path_)) {
        IVX_INFO("config: %S does not exist yet", path_.c_str());
        return;
    }

    std::vector<wchar_t> names(32768);
    const DWORD n = GetPrivateProfileSectionNamesW(names.data(), static_cast<DWORD>(names.size()),
                                                   path_.c_str());
    if (n == 0) {
        return;
    }

    for (const wchar_t* section = names.data(); *section; section += wcslen(section) + 1) {
        const std::wstring name = trim(section);
        if (name.empty() || _wcsicmp(name.c_str(), settings_key::kSection) == 0) {
            continue;
        }
        VoiceEdit edit;
        edit.name = name;
        edit.from_file = true;
        edit.builtin = catalog().find_by_name(name) >= 0 &&
                       !catalog().voices()[static_cast<size_t>(catalog().find_by_name(name))]
                            .user_defined;
        for (int key = 0; key < KEY_COUNT; ++key) {
            edit.values[key] = read_key(path_, name, kVoiceKeyNames[key]);
        }
        entries_.push_back(edit);
    }
    IVX_INFO("config: %u voice sections read from %S", static_cast<unsigned>(entries_.size()),
             path_.c_str());
}

int VoiceFile::find(const std::wstring& name) const
{
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (_wcsicmp(entries_[i].name.c_str(), name.c_str()) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void VoiceFile::put(const VoiceEdit& edit)
{
    const int index = find(edit.name);
    if (index >= 0) {
        VoiceEdit updated = edit;
        updated.from_file = entries_[static_cast<size_t>(index)].from_file;
        entries_[static_cast<size_t>(index)] = updated;
    } else {
        entries_.push_back(edit);
    }
    dirty_ = true;
}

void VoiceFile::remove(const std::wstring& name)
{
    const int index = find(name);
    if (index < 0) {
        return;
    }
    if (entries_[static_cast<size_t>(index)].from_file) {
        removed_.push_back(entries_[static_cast<size_t>(index)].name);
    }
    entries_.erase(entries_.begin() + index);
    dirty_ = true;
}

bool VoiceFile::ensure_file_exists(std::wstring* error)
{
    if (file_exists(path_)) {
        return true;
    }
    const std::wstring folder = parent_of(path_);
    if (!folder.empty()) {
        CreateDirectoryW(folder.c_str(), nullptr);
    }
    return create_ini(path_, error);
}

bool VoiceFile::save(std::wstring* error)
{
    std::wstring problem;
    if (!ensure_file_exists(&problem)) {
        if (error) {
            *error = problem;
        }
        return false;
    }

    bool ok = true;
    for (const std::wstring& name : removed_) {
        // A null key removes the whole section, comments inside it included.
        ok = write_key(path_, name, nullptr, nullptr, &problem) && ok;
    }

    for (VoiceEdit& edit : entries_) {
        for (int key = 0; key < KEY_COUNT; ++key) {
            const std::wstring& value = edit.values[key];
            // A key the user has cleared is removed rather than written empty,
            // so the voice goes back to inheriting it.
            ok = write_key(path_, edit.name, kVoiceKeyNames[key],
                           value.empty() ? nullptr : value.c_str(), &problem) &&
                 ok;
        }
        edit.from_file = true;
    }

    const std::wstring section = settings_key::kSection;
    struct Numbered {
        const wchar_t* key;
        int value;
    };
    const Numbered numbers[] = {
        {settings_key::kSilenceThreshold, settings_.silence_threshold},
        {settings_key::kTimeoutBaseMs, settings_.timeout_base_ms},
        {settings_key::kTimeoutPerCharMs, settings_.timeout_per_char_ms},
        {settings_key::kRateMin, settings_.rate_min},
        {settings_key::kRateMax, settings_.rate_max},
        {settings_key::kRateDefault, settings_.rate_default},
        {settings_key::kPitchMin, settings_.pitch_min},
        {settings_key::kPitchMax, settings_.pitch_max},
        {settings_key::kPitchDefault, settings_.pitch_default},
    };
    for (const Numbered& item : numbers) {
        ok = write_key(path_, section, item.key, number(item.value).c_str(), &problem) && ok;
    }

    struct Flag {
        const wchar_t* key;
        bool value;
    };
    const Flag flags[] = {
        {settings_key::kTrimTrailingSilence, settings_.trim_trailing_silence},
        {settings_key::kWordEvents, settings_.word_events},
        {settings_key::kSentenceEvents, settings_.sentence_events},
    };
    for (const Flag& item : flags) {
        ok = write_key(path_, section, item.key, item.value ? L"1" : L"0", &problem) && ok;
    }
    ok = write_key(path_, section, settings_key::kPreviewText, settings_.preview_text.c_str(),
                   &problem) &&
         ok;

    // The profile API caches; this flushes it so the engine reads what was just
    // written rather than what was there before.
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path_.c_str());

    if (ok) {
        removed_.clear();
        dirty_ = false;
        reload_catalog();
        IVX_INFO("config: saved %u voices to %S", static_cast<unsigned>(entries_.size()),
                 path_.c_str());
    } else if (error) {
        *error = problem;
    }
    return ok;
}

const std::vector<Language>& languages()
{
    static std::vector<Language> list = [] {
        std::vector<Language> out;
        for (const Voice& v : catalog().voices()) {
            if (v.user_defined) {
                continue;
            }
            const std::wstring name = widen(v.language_name);
            bool seen = false;
            for (const Language& l : out) {
                if (_wcsicmp(l.name.c_str(), name.c_str()) == 0) {
                    seen = true;
                    break;
                }
            }
            if (seen) {
                continue;
            }
            Language language;
            language.name = name;
            language.rule_file = widen(v.language_file);
            language.language_id = widen(v.language_id);
            language.lcid = v.lcid;
            out.push_back(language);
        }
        std::sort(out.begin(), out.end(), [](const Language& a, const Language& b) {
            return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
        });
        return out;
    }();
    return list;
}

namespace {

Catalog& mutable_catalog()
{
    static Catalog c = [] {
        Catalog loaded;
        loaded.load(install_dir());
        return loaded;
    }();
    return c;
}

}  // namespace

const Catalog& catalog()
{
    return mutable_catalog();
}

void reload_catalog()
{
    mutable_catalog().load(install_dir());
}

std::wstring inherited_value(const VoiceEdit& edit, int key)
{
    const Catalog& c = catalog();

    int base = -1;
    if (edit.has(KEY_BASED_ON)) {
        base = c.find_by_name(edit.get(KEY_BASED_ON));
    }
    if (base < 0) {
        // A voice the catalogue already knows under this name: a built-in, or
        // one defined in the voices file that is not the one being edited.
        // Without this, changing a voice defined for all users while editing
        // the personal file would show the first built-in's settings instead of
        // that voice's own.
        base = c.find_by_name(edit.name);
    }
    if (base < 0) {
        base = 0;  // what the catalogue itself falls back to
    }
    const Voice& v = c.voices()[static_cast<size_t>(base)];

    switch (key) {
        case KEY_LANGUAGE_FILE:
            return widen(v.language_file);
        case KEY_LANGUAGE_ID:
            return widen(v.language_id);
        case KEY_LCID: {
            wchar_t buf[16];
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%04x", static_cast<unsigned>(v.lcid));
            return buf;
        }
        case KEY_PITCH:
            return widen(v.pitch);
        case KEY_DYNAMIC:
            return widen(v.dynamic);
        case KEY_ASPIRATION:
            return widen(v.aspiration);
        case KEY_FORMANT_NO:
            return widen(v.formant_no);
        case KEY_GENDER:
            return widen(v.gender);
        case KEY_AGE:
            return widen(v.age);
        case KEY_SPEAKER_NAME:
            return widen(v.speaker_name);
        case KEY_SPEAKER_STYLE:
            return widen(v.speaker_style);
        case KEY_LIBRARY_FILE:
            return widen(v.library_file);
        case KEY_PHSYM_FILE:
            return widen(v.phsym_file);
        case KEY_DIPHONE_FILE:
            return widen(v.diphone_file);
        case KEY_MAPPING_FILE:
            return widen(v.mapping_file);
        default:
            return std::wstring();
    }
}

int pitch_to_hertz(int pitch)
{
    const int hertz = 3 * pitch - 49;
    if (hertz < 30) {
        return 30;
    }
    if (hertz > 250) {
        return 250;
    }
    return hertz;
}

int hertz_to_pitch(int hertz)
{
    const int pitch = (hertz + 49 + 1) / 3;
    if (pitch < 27) {
        return 27;
    }
    if (pitch > 99) {
        return 99;
    }
    return pitch;
}

const wchar_t* formant_description(int formant)
{
    switch (formant) {
        case 0:
            return L"0 -- standard, as the male voices use";
        case 1:
            return L"1 -- lighter, as the female voices use";
        case 2:
            return L"2 -- large, as the giant voices use";
        case 3:
            return L"3 -- small, as the child voices use";
        case 4:
            return L"4 -- rough, as the zombie voices use";
        default:
            return L"";
    }
}

}  // namespace config
}  // namespace ivx
