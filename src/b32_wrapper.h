#pragma once

#include <windows.h>
#include <memory>

// Thin loader over b32_wrapper.dll, the 32-bit shim that knows how to drive both
// BestSpeech engine families.
//
// The classic 1994 engine (b32_tts.dll) and the 2006 language dlls (dll_*.dll) do not
// share an API at all: the classic one exports bstCreate/TtsWav/bstSetParams, while
// every language dll exports Init_TTS/Say_TTS/DeInit_TTS. b32_wrapper.dll hides that
// difference behind one interface, captures the engine's audio by hooking its wave
// output, and reports the true output sample rate -- which is not the same for every
// dll. It is the same shim the NVDA driver in bin/ uses, so all thirteen engines are
// driven through code that is already proven against them.
namespace b32 {

struct State;

struct StateDeleter {
    void operator()(State* s) const noexcept;
};

using StatePtr = std::unique_ptr<State, StateDeleter>;

// Return false to stop synthesis early.
using AsyncCallback = bool(*)(const char* data, long size, void* user);

// Everything one utterance needs. The text is already encoded for the engine's codepage
// and already carries its inline command prefix, so nothing here inspects it.
struct SpeakParams {
    const char* text = nullptr;

    // Speed change applied by the shim's time stretcher. Exactly 1.0 bypasses it, so
    // the ordinary path stays bit-for-bit the bare engine output. Used only for the
    // engines that ignore the inline rate command; the others change rate natively.
    float sonic_speed = 1.0f;

    // Linear volume applied to the samples on the way out. Exactly 1.0 bypasses it.
    // Used only for the engines that ignore the inline gain command.
    float gain_scale = 1.0f;
};

// Loads b32_wrapper.dll from the given directory. Safe to call repeatedly; the shim is
// loaded once per process. Returns false if it is missing or does not export the
// expected entry points.
[[nodiscard]] bool load_shim(const wchar_t* directory) noexcept;

[[nodiscard]] StatePtr init(const wchar_t* engine_dll_path) noexcept;

// The engine's true output sample rate. The shim runs a warmup utterance during init,
// so this is valid immediately. Zero if it could not be determined.
[[nodiscard]] DWORD get_sample_rate(const State* s) noexcept;

// True when the loaded engine is one of the 2006 language dlls rather than the classic
// engine. Their frontends differ enough that the caller prepares text differently.
[[nodiscard]] bool is_v2(const State* s) noexcept;

void speak_async(State* state, AsyncCallback callback, void* user,
                 const SpeakParams& params) noexcept;
}
