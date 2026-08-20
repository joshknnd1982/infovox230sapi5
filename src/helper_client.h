#pragma once

#include <windows.h>
#include <string>

// Drives one engine through a dedicated b32_helper.exe process.
//
// The in-process shim captures the engine's audio by hooking winmm, and that capture
// only happens when two conditions hold at once: the hook state is set for the calling
// thread (it lives in a thread_local), and the engine echoes the exact state pointer
// back through waveOutOpen's callback argument. Miss either and the hook passes the call
// straight to the real winmm -- no audio reaches us, and nothing reports an error. Inside
// a host application that goes wrong intermittently, which is what makes a voice load
// cleanly and then say nothing.
//
// b32_helper.exe sidesteps all of it: a dedicated process, one engine, synthesis on the
// thread that opened it, audio handed back over a pipe. It is the same path the NVDA
// add-on uses, which is the configuration known to work reliably on affected machines.
//
// Every read here is bounded. A speech engine runs inside whatever application is
// talking, so a helper that wedges must never be able to block that application: a
// blocking read in this class is a frozen screen reader.
class HelperClient
{
public:
    using AudioCallback = bool(*)(const char* data, long size, void* user);

    HelperClient();
    ~HelperClient();

    HelperClient(const HelperClient&) = delete;
    HelperClient& operator=(const HelperClient&) = delete;

    // Starts a helper for one engine dll. helper_path and engine_dll are full paths.
    [[nodiscard]] bool start(const std::wstring& helper_path, const std::wstring& engine_dll);

    [[nodiscard]] bool running() const { return process_ != nullptr; }

    // The engine's true output rate, from the helper's startup handshake.
    [[nodiscard]] DWORD sample_rate() const { return sample_rate_; }

    // Synthesizes one utterance, handing every chunk to the callback. Returns false if
    // the helper died, wedged, or lost protocol sync; the caller starts a new one.
    [[nodiscard]] bool speak(const char* text, size_t length, float rate_multiplier,
                             float gain_scale, AudioCallback callback, void* user);

    void stop();

private:
    [[nodiscard]] bool write_all(const void* data, DWORD size);

    // Reads exactly size bytes, giving up if nothing arrives for timeout_ms. The clock
    // restarts whenever bytes come in, so a long utterance streaming steadily is never
    // cut off; only a helper that has stopped answering trips it.
    [[nodiscard]] bool read_all(void* data, DWORD size, DWORD timeout_ms);

    [[nodiscard]] bool alive() const;
    void stop_locked();

    HANDLE process_ = nullptr;
    HANDLE to_helper_ = nullptr;    // helper's stdin
    HANDLE from_helper_ = nullptr;  // helper's stdout
    DWORD  sample_rate_ = 0;

    // SAPI is free to call into one engine object from more than one thread; two
    // utterances interleaving on this pipe would corrupt the protocol framing.
    CRITICAL_SECTION lock_;
};
