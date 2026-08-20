#pragma once

// Drives the Infovox 230 engine directly: loads Ivx230nt.dll by path, asks its
// own DllGetClassObject for the mode enumerator, and captures the samples
// through an audio sink of ours instead of a sound card.
//
// Nothing from the SAPI4 runtime is involved. The engine is never registered,
// CoCreateInstance is never called on it, and its configuration comes from
// ivx_vregistry rather than from the machine.
//
// Every method must be called from the one thread that called load(): SAPI4
// delivers its callbacks through that thread's message queue, which speak()
// pumps while it waits.

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "ivx_catalog.h"
#include "ivx_sapi4.h"

namespace ivx {

struct ModeInfo {
    std::string mode_guid;  // "{...}" as the catalogue spells it
    std::wstring mode_name;
    std::wstring product;
    std::wstring speaker;
    unsigned short langid = 0;
    unsigned short gender = 0;
    unsigned short age = 0;
    unsigned long features = 0;
};

struct AudioFormat {
    unsigned long samples_per_sec = 0;
    unsigned short channels = 0;
    unsigned short bits = 0;
    unsigned long avg_bytes_per_sec = 0;
    bool valid() const { return samples_per_sec != 0 && avg_bytes_per_sec != 0; }
};

struct SpeakEvent {
    enum Kind {
        Bookmark,  // value is the number from a \mrk=N\ tag
        Word,      // value is a 1-based character offset into the submitted text
    };
    Kind kind;
    unsigned long audio_offset;  // bytes from the start of this utterance
    unsigned long value;
};

// Returning false from on_pcm asks synthesis to stop (the consumer has gone
// away or the caller aborted).
using PcmSink = std::function<bool(const void* data, unsigned long bytes)>;
using EventSink = std::function<void(const SpeakEvent&)>;
using CancelCheck = std::function<bool()>;

class CaptureAudio;
class BufNotifySink;
class NotifySink;

class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // `engine_dir` holds Ivx230nt.dll, sx32w.dll and the .ivx rule files.
    bool load(const std::wstring& engine_dir, const Catalog& catalog);
    void unload();
    bool loaded() const { return enum_ != nullptr; }

    const std::vector<ModeInfo>& modes() const { return modes_; }
    const AudioFormat& format() const { return format_; }

    // Selecting a mode builds a fresh ITTSCentral and a fresh pair of sinks.
    // The engine keeps the sink pointer it is handed in TextData and never
    // releases it, so one sink is reused for the whole life of the selection --
    // building one per utterance leaks a COM object each time and the engine
    // eventually goes quiet while still accepting calls.
    bool select(const std::string& mode_guid);
    const std::string& selected() const { return selected_guid_; }

    // Discovered by pushing the API sentinels at the engine and reading back
    // what it clamped to. There is no other way to learn them.
    int rate_min() const { return rate_min_; }
    int rate_max() const { return rate_max_; }
    int rate_default() const { return rate_default_; }
    int pitch_min() const { return pitch_min_; }
    int pitch_max() const { return pitch_max_; }
    int pitch_default() const { return pitch_default_; }

    // `tagged` is control-tagged text: \Spd=wpm\ \Pit=hz\ \Vol=dword\ \mrk=n\.
    // Returns false if the engine never signalled completion.
    bool speak(const std::wstring& tagged, const PcmSink& on_pcm, const EventSink& on_event,
               const CancelCheck& cancelled, unsigned timeout_ms);

    // Speak phonemes rather than text. The engine accepts both IPA and its own
    // alphabet; SAPI5's <pron sym="..."> arrives this way.
    bool speak_phonemes(const std::wstring& phonemes, bool ipa, const PcmSink& on_pcm,
                        const CancelCheck& cancelled, unsigned timeout_ms);

    // Tear down and rebuild the current selection. A live engine that stops
    // producing audio recovers from this; it is the in-process equivalent of
    // switching synthesizers and back.
    bool recycle();

    unsigned long registry_calls() const;

private:
    bool enumerate_modes();
    void query_ranges();
    void release_current();
    bool run_text_data(ivx::sapi4::VOICECHARSET charset, DWORD flags, const std::wstring& text,
                       const PcmSink& on_pcm, const EventSink& on_event,
                       const CancelCheck& cancelled, unsigned timeout_ms);

    HMODULE module_ = nullptr;
    ivx::sapi4::ITTSEnumW* enum_ = nullptr;
    ivx::sapi4::ITTSCentralW* central_ = nullptr;
    ivx::sapi4::ITTSAttributesW* attrs_ = nullptr;
    CaptureAudio* audio_ = nullptr;
    BufNotifySink* buf_sink_ = nullptr;
    NotifySink* notify_sink_ = nullptr;
    DWORD notify_key_ = 0;

    std::vector<ModeInfo> modes_;
    AudioFormat format_;
    std::string selected_guid_;
    std::wstring engine_dir_;
    unsigned long selected_features_ = 0;

    int rate_min_ = 0;
    int rate_max_ = 0;
    int rate_default_ = 0;
    int pitch_min_ = 0;
    int pitch_max_ = 0;
    int pitch_default_ = 0;
};

// Formats a control-tagged prologue that pins rate, pitch and volume for an
// utterance. Tags persist in the engine across utterances, so every utterance
// states all three rather than relying on what the last one left behind.
std::wstring format_prologue(int rate_wpm, int pitch_hz, int volume_pct);

// Escapes text so a backslash in it cannot be read as the start of a tag.
std::wstring escape_text(const std::wstring& text);

}  // namespace ivx
