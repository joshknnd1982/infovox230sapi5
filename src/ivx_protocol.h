#pragma once

// Wire protocol between the SAPI5 engine (32- or 64-bit, running inside
// whatever application is speaking) and the 32-bit worker that owns the
// Infovox engine.
//
// Both the 32-bit and the 64-bit SAPI5 dlls are clients. Keeping the engine out
// of the speaking application matters most for screen readers: a legacy engine
// that faults or wedges must not take the screen reader down with it, and it
// must not be able to disturb the host process, which an in-process load would
// risk since the engine expects to own things like the current directory.
//
// Frames are <FrameHeader><payload>. Every field is little-endian, every
// structure is packed, and text is UTF-16LE with no terminator (the length is
// in the header), so 32- and 64-bit clients see identical bytes.

#include <windows.h>

#include <stdint.h>

#include <string>

namespace ivx {

constexpr uint32_t kProtocolVersion = 3;

// The pipe is per session, so two users logged in at once get a worker each
// rather than fighting over one name.
constexpr wchar_t kPipePrefix[] = L"\\\\.\\pipe\\Infovox230TTS_";
constexpr wchar_t kServerMutexPrefix[] = L"Local\\Infovox230TTS_Server_";
constexpr wchar_t kLaunchMutexPrefix[] = L"Local\\Infovox230TTS_Launch_";

enum RequestType : uint32_t {
    REQ_HELLO = 1,
    REQ_VOICES = 2,
    REQ_SPEAK = 3,
    REQ_PHONEMES = 4,
    REQ_PING = 5,
    REQ_SHUTDOWN = 6,
};

enum ResponseType : uint32_t {
    RSP_HELLO = 101,
    RSP_VOICES = 102,
    RSP_FORMAT = 103,
    RSP_AUDIO = 104,
    RSP_EVENT = 105,
    RSP_DONE = 106,
    RSP_ERROR = 107,
    RSP_PONG = 108,
    RSP_OK = 109,
};

enum EventKind : uint32_t {
    EV_BOOKMARK = 1,  // value is the number from a \mrk=N\ tag
    EV_WORD = 2,      // value is a 1-based character offset into the sent text
};

// Status in RSP_DONE.
enum DoneStatus : uint32_t {
    DONE_COMPLETE = 0,
    DONE_CANCELLED = 1,
    DONE_TIMEOUT = 2,
    DONE_ENGINE_ERROR = 3,
};

#pragma pack(push, 1)

struct FrameHeader {
    uint32_t type;
    uint32_t size;  // payload bytes that follow
};

struct HelloResponse {
    uint32_t version;
    uint32_t voice_count;
    uint32_t samples_per_sec;
    uint32_t avg_bytes_per_sec;
    uint16_t channels;
    uint16_t bits;
};

// One per voice in RSP_VOICES. Fixed size so the client can index straight in.
struct VoiceRecord {
    wchar_t display_name[96];
    char mode_guid[40];
    uint16_t lcid;
    uint16_t gender;  // 1 female, 2 male, as the engine numbers them
    uint16_t age;     // years
    uint16_t user_defined;
    int32_t rate_min;
    int32_t rate_max;
    int32_t rate_default;
    int32_t pitch_min;
    int32_t pitch_max;
    int32_t pitch_default;
};

// Rate and pitch travel as SAPI steps (-10..10), not as words per minute and
// hertz. Only the worker knows what a given voice's real default and limits
// are -- they come from the engine after the mode is selected, and differ per
// speaker -- so it is the worker that turns a step into a number.
struct SpeakRequest {
    char mode_guid[40];        // voice to speak with; selected if not current
    int32_t rate_step;         // -10..10, 0 = the voice's own default
    int32_t pitch_step;        // -10..10, 0 = the voice's own default
    int32_t volume_pct;        // 0-100
    uint32_t timeout_ms;
    uint32_t text_chars;       // UTF-16 units following this structure
    uint32_t flags;            // reserved
    wchar_t cancel_event[64];  // name of an event the client sets to abort
};

struct PhonemeRequest {
    char mode_guid[40];
    int32_t rate_step;
    int32_t pitch_step;
    int32_t volume_pct;
    uint32_t timeout_ms;
    uint32_t text_chars;
    uint32_t ipa;  // 1 = IPA, 0 = the engine's own phoneme alphabet
    wchar_t cancel_event[64];
};

struct FormatResponse {
    uint32_t samples_per_sec;
    uint32_t avg_bytes_per_sec;
    uint16_t channels;
    uint16_t bits;
};

struct EventResponse {
    uint32_t kind;
    uint32_t audio_offset;  // bytes from the start of this utterance
    uint32_t value;
};

struct DoneResponse {
    uint32_t status;
    uint32_t total_bytes;
};

#pragma pack(pop)

// Both names include the session id so a second logged-in user gets a worker of
// their own instead of colliding with the first.
inline std::wstring session_suffix()
{
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    wchar_t buf[24];
    _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%lu", static_cast<unsigned long>(session));
    return buf;
}

inline std::wstring pipe_name()
{
    return std::wstring(kPipePrefix) + session_suffix();
}

inline std::wstring server_mutex_name()
{
    return std::wstring(kServerMutexPrefix) + session_suffix();
}

inline std::wstring launch_mutex_name()
{
    return std::wstring(kLaunchMutexPrefix) + session_suffix();
}

}  // namespace ivx
