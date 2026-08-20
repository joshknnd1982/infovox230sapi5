#pragma once

// Client side of the worker protocol, shared by the 32-bit and 64-bit SAPI5
// engines. One instance per SAPI voice object: it owns a pipe connection and a
// cancel event of its own, so two applications speaking at once never disturb
// each other's stream.

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "ivx_protocol.h"

namespace ivx {

// Returning false stops the utterance: the SAPI host has asked to abort, or
// writing to it failed.
using AudioHandler = std::function<bool(const void* data, unsigned long bytes)>;
using EventHandler = std::function<void(const EventResponse&)>;
using FormatHandler = std::function<void(const FormatResponse&)>;

class WorkerClient {
public:
    WorkerClient();
    ~WorkerClient();

    WorkerClient(const WorkerClient&) = delete;
    WorkerClient& operator=(const WorkerClient&) = delete;

    // Connects, starting the worker if it is not already running.
    bool connect();
    void disconnect();
    bool connected() const { return pipe_ != INVALID_HANDLE_VALUE; }

    bool hello(HelloResponse* out);
    bool voices(std::vector<VoiceRecord>* out);

    // `text` is control-tagged text for speak(), phonemes for speak_phonemes().
    // The request's cancel_event field is filled in by these.
    bool speak(SpeakRequest request, const std::wstring& text, const AudioHandler& on_audio,
               const EventHandler& on_event, const FormatHandler& on_format, DoneResponse* done);
    bool speak_phonemes(PhonemeRequest request, const std::wstring& phonemes,
                        const AudioHandler& on_audio, const FormatHandler& on_format,
                        DoneResponse* done);

    // Asks the worker to exit. Used by uninstall, so the files it holds open can
    // be replaced or removed.
    void shutdown_worker();

    const wchar_t* cancel_event_name() const { return cancel_name_.c_str(); }

private:
    bool ensure_connected();
    bool launch_worker();
    static bool worker_running();
    bool send(uint32_t type, const void* payload = nullptr, uint32_t size = 0);
    bool read_frame(FrameHeader* header, std::vector<char>* payload);
    bool stream_utterance(const AudioHandler& on_audio, const EventHandler& on_event,
                          const FormatHandler& on_format, DoneResponse* done);

    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    HANDLE cancel_ = nullptr;
    std::wstring cancel_name_;
    CRITICAL_SECTION cs_;
};

}  // namespace ivx
