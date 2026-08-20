#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "b32_wrapper.h"
#include "debug_log.h"

namespace b32 {

namespace {

// b32_wrapper.dll's exported C interface. The NVDA driver in bin/bestspeech.py calls
// exactly these entry points, which is where the signatures come from.
using BstInitW         = void*(__cdecl*)(const wchar_t* path);
using BstFree          = void (__cdecl*)(void* handle);
using BstGetSampleRate = long (__cdecl*)(void* handle);
using BstIsV2          = int  (__cdecl*)(void* handle);
using BstAsyncCb       = long (__cdecl*)(const char* data, long size, void* user);
using BstSpeakAsync    = void*(__cdecl*)(void* handle, BstAsyncCb cb, void* user,
                                         const char* text, long voice, long native_rate,
                                         float rate_mult, long gain);

HMODULE          g_shim  = nullptr;
BstInitW         g_init  = nullptr;
BstFree          g_free  = nullptr;
BstGetSampleRate g_rate  = nullptr;
BstIsV2          g_is_v2 = nullptr;
BstSpeakAsync    g_speak = nullptr;

// The voice argument selects one of the shim's own built-in voice presets, which would
// prepend a second command prefix on top of the one this engine already builds from
// engines.hpp. Minus one asks it to pass the text through untouched.
constexpr long VOICE_PASSTHROUGH = -1;

// Bridges the shim's C callback back to the caller's, and applies software volume for
// the engines whose frontend ignores the inline gain command.
struct CallbackBridge {
    AsyncCallback      caller = nullptr;
    void*              user = nullptr;
    float              gain_scale = 1.0f;
    std::vector<short> scratch;
};

long __cdecl on_audio(const char* data, long size, void* user)
{
    auto* b = static_cast<CallbackBridge*>(user);
    if (!b || !b->caller || size <= 0) {
        return 1;
    }

    if (b->gain_scale != 1.0f) {
        const long count = size / 2;
        const auto* in = reinterpret_cast<const short*>(data);
        b->scratch.assign(in, in + count);
        for (long i = 0; i < count; ++i) {
            const float v = static_cast<float>(b->scratch[i]) * b->gain_scale;
            b->scratch[i] = static_cast<short>(v > 32767.0f ? 32767.0f
                                              : (v < -32768.0f ? -32768.0f : v));
        }
        return b->caller(reinterpret_cast<const char*>(b->scratch.data()),
                         count * 2, b->user) ? 1 : 0;
    }

    return b->caller(data, size, b->user) ? 1 : 0;
}

}  // namespace

struct State {
    void* handle = nullptr;
};

bool load_shim(const wchar_t* directory) noexcept
{
    if (g_shim) {
        return true;
    }

    std::wstring path = directory ? directory : L"";
    if (!path.empty() && path.back() != L'\\') {
        path += L'\\';
    }
    path += L"b32_wrapper.dll";

    g_shim = LoadLibraryW(path.c_str());
    if (!g_shim) {
        DEBUG_LOG("b32: could not load shim at %S (error %lu)", path.c_str(), GetLastError());
        return false;
    }

    g_init   = reinterpret_cast<BstInitW>(GetProcAddress(g_shim, "bst_init_w"));
    g_free   = reinterpret_cast<BstFree>(GetProcAddress(g_shim, "bst_free"));
    g_rate   = reinterpret_cast<BstGetSampleRate>(GetProcAddress(g_shim, "bst_get_sample_rate"));
    g_is_v2  = reinterpret_cast<BstIsV2>(GetProcAddress(g_shim, "bst_is_v2"));
    g_speak  = reinterpret_cast<BstSpeakAsync>(GetProcAddress(g_shim, "bst_speak_async"));

    if (!g_init || !g_free || !g_speak) {
        DEBUG_LOG("b32: shim is missing expected entry points");
        FreeLibrary(g_shim);
        g_shim = nullptr;
        return false;
    }
    return true;
}

StatePtr init(const wchar_t* engine_dll_path) noexcept
{
    if (!g_init || !engine_dll_path) {
        return nullptr;
    }

    void* handle = g_init(engine_dll_path);
    if (!handle) {
        DEBUG_LOG("b32: engine failed to initialize: %S", engine_dll_path);
        return nullptr;
    }

    auto* s = new (std::nothrow) State;
    if (!s) {
        g_free(handle);
        return nullptr;
    }
    s->handle = handle;
    return StatePtr(s);
}

DWORD get_sample_rate(const State* s) noexcept
{
    if (!s || !s->handle || !g_rate) {
        return 0;
    }
    const long rate = g_rate(s->handle);
    return rate > 0 ? static_cast<DWORD>(rate) : 0;
}

bool is_v2(const State* s) noexcept
{
    if (!s || !s->handle || !g_is_v2) {
        return false;
    }
    return g_is_v2(s->handle) != 0;
}

void StateDeleter::operator()(State* s) const noexcept
{
    if (!s) {
        return;
    }
    if (s->handle && g_free) {
        g_free(s->handle);
    }
    delete s;
}

void speak_async(State* s, AsyncCallback callback, void* user, const SpeakParams& p) noexcept
{
    if (!s || !s->handle || !callback || !p.text || !g_speak) {
        return;
    }

    CallbackBridge bridge;
    bridge.caller = callback;
    bridge.user = user;
    bridge.gain_scale = p.gain_scale;

    DEBUG_LOG("b32: speak (speed %.2fx, gain %.2fx): %.120s",
              p.sonic_speed, p.gain_scale, p.text);

    // Rate and gain travel inside the text as inline commands, so the shim's own rate
    // and gain arguments stay neutral; only its time stretcher is used, and only for
    // the engines that cannot change rate themselves.
    g_speak(s->handle, on_audio, &bridge, p.text, VOICE_PASSTHROUGH, 0, p.sonic_speed, 0);
}
}
