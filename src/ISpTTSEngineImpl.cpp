#include <new>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

#include "utils.hpp"
#include "text_pipeline.hpp"
#include "ISpTTSEngineImpl.hpp"
#include "debug_log.h"
#include "helper_client.h"

#include "pipe_client.h"
#ifndef BUILD_X64
#include "b32_wrapper.h"
#endif

namespace Bestspeech {
namespace sapi {

namespace {

constexpr WORD AUDIO_CHANNELS = 1;
constexpr WORD AUDIO_BITS_PER_SAMPLE = 16;

// SAPI hands out rate, pitch and volume on these scales.
constexpr int SAPI_RATE_MIN   = -10;
constexpr int SAPI_RATE_MAX   =  10;
constexpr int SAPI_PITCH_MIN  = -10;
constexpr int SAPI_PITCH_MAX  =  10;

PipeClient* g_pipeClient = nullptr;

// b32_helper.exe, resolved once from beside this dll.
std::wstring helper_executable(const wchar_t* install_dir)
{
    return std::wstring(install_dir) + L"b32_helper.exe";
}

// Engines that produced nothing in process and have been moved onto the worker. One
// flag per engine, set once and never cleared: if an engine is silent here it stays
// silent, and every later utterance should take the path that works.
volatile LONG g_worker_engines = 0;

// The engine's measured response to its ~r command, as a speed factor relative to ~r0.
// The documented "percentage of normal speed" only holds at the slow end; going fast the
// engine compresses hard (~r-90 is 2.7x, nowhere near the 10x a naive reading suggests),
// so both directions of this mapping follow the measured curve instead of a formula.
struct rate_point { int param; float speed; };
constexpr rate_point RATE_CURVE[] = {
    { 200, 0.371f }, { 100, 0.539f }, { 0, 1.000f },
    { -45, 1.660f }, { -61, 2.113f }, { -90, 2.711f },
};
constexpr int RATE_CURVE_COUNT = static_cast<int>(sizeof(RATE_CURVE) / sizeof(RATE_CURVE[0]));

constexpr float RATE_SLOWEST = RATE_CURVE[0].speed;
constexpr float RATE_FASTEST = RATE_CURVE[RATE_CURVE_COUNT - 1].speed;

// Speed factor -> engine rate parameter, by interpolating the measured curve.
[[nodiscard]] int speed_to_rate_param(float speed)
{
    if (speed <= RATE_SLOWEST) {
        return RATE_CURVE[0].param;
    }
    if (speed >= RATE_FASTEST) {
        return RATE_CURVE[RATE_CURVE_COUNT - 1].param;
    }
    for (int i = 0; i + 1 < RATE_CURVE_COUNT; ++i) {
        const rate_point& a = RATE_CURVE[i];
        const rate_point& b = RATE_CURVE[i + 1];
        if (speed >= a.speed && speed <= b.speed) {
            const float t = (speed - a.speed) / (b.speed - a.speed);
            return static_cast<int>(std::lround(a.param + t * (b.param - a.param)));
        }
    }
    return 0;
}

// SAPI rate -10..+10 spans a quarter speed to four times speed, which covers both a
// beginner's pace and the very fast rates experienced screen reader users prefer.
[[nodiscard]] float sapi_rate_to_speed(int sapi_rate)
{
    return std::pow(2.0f, static_cast<float>(std::clamp(sapi_rate, SAPI_RATE_MIN, SAPI_RATE_MAX)) / 5.0f);
}

[[nodiscard]] float sapi_pitch_to_factor(int sapi_pitch)
{
    return std::pow(2.0f, static_cast<float>(std::clamp(sapi_pitch, SAPI_PITCH_MIN, SAPI_PITCH_MAX)) / 10.0f);
}

// SAPI volume is a percentage of full loudness; the engine's gain command is in dB.
[[nodiscard]] int volume_to_gain_db(unsigned short volume)
{
    if (volume == 0) {
        return GAIN_MIN_DB;
    }
    const double db = 20.0 * std::log10(static_cast<double>(std::min<unsigned short>(volume, 100)) / 100.0);
    return static_cast<int>(std::lround(db));
}

// Everything one fragment needs the engine to do, after the SAPI values have been
// combined with the fragment's own adjustments and the engine's capabilities.
struct synth_params {
    std::wstring text;
    int   native_rate = 0;
    int   native_gain = 0;
    float sonic_speed = 1.0f;
    float gain_scale  = 1.0f;
};

[[nodiscard]] synth_params build_params(const engine_info& eng, const voice_info& voice,
                                        int sapi_rate, int sapi_pitch, unsigned short volume)
{
    synth_params out;

    const float wanted_speed = sapi_rate_to_speed(sapi_rate);
    const float pitch_factor = sapi_pitch_to_factor(sapi_pitch);
    const int gain_db = volume_to_gain_db(volume);

    if (eng.commands == cmd_mode::none) {
        // These frontends ignore inline commands completely, so rate goes through the
        // shim's time stretcher and volume is applied to the samples. Pitch has nowhere
        // to go on these three engines and is simply not available for them.
        out.sonic_speed = wanted_speed;
        out.gain_scale = static_cast<float>(
            std::pow(10.0, (gain_db + eng.gain_trim_db) / 20.0));
        return out;
    }

    // Prefer the engine's own rate control, which sounds better than time stretching,
    // and only bring sonic in for speeds beyond what the engine can reach on its own.
    out.native_rate = speed_to_rate_param(wanted_speed);
    if (wanted_speed > RATE_FASTEST) {
        out.sonic_speed = wanted_speed / RATE_FASTEST;
    } else if (wanted_speed < RATE_SLOWEST) {
        out.sonic_speed = wanted_speed / RATE_SLOWEST;
    }

    out.native_gain = std::clamp(gain_db + eng.gain_trim_db, GAIN_MIN_DB, GAIN_MAX_DB);

    // Pitch reaches these engines as a frequency in the command prefix, which they
    // render far more naturally than a pitch shifter would.
    (void)voice;
    (void)pitch_factor;
    return out;
}

struct SpeakContext {
    ISpTTSEngineSite* caller = nullptr;
    ULONGLONG bytes_written = 0;
    bool aborted = false;
};

bool write_to_site(const char* data, long size, void* user) {
    auto* ctx = static_cast<SpeakContext*>(user);
    if (!ctx || !ctx->caller) {
        return false;
    }

    auto ptr = reinterpret_cast<const BYTE*>(data);
    ULONG remaining = static_cast<ULONG>(size);

    while (remaining > 0) {
        const DWORD actions = ctx->caller->GetActions();
        if (actions & SPVES_ABORT) {
            ctx->aborted = true;
            return false;
        }
        if (actions & SPVES_SKIP) {
            ctx->caller->CompleteSkip(0);
            ctx->aborted = true;
            return false;
        }

        ULONG written = 0;
        const HRESULT hr = ctx->caller->Write(ptr, remaining, &written);
        if (FAILED(hr) || written == 0 || written > remaining) {
            DEBUG_LOG("SAPI Write failed: hr=0x%08X written=%lu remaining=%lu", hr, written, remaining);
            return false;
        }
        ctx->bytes_written += written;
        remaining -= written;
        ptr += written;
    }
    return true;
}

// The in-process engine reports sizes as long, the worker as uint32_t; both end up here.
bool speak_callback(const char* data, long size, void* user) {
    return write_to_site(data, size, user);
}

bool pipe_callback(const char* data, uint32_t size, void* user) {
    return write_to_site(data, static_cast<long>(size), user);
}

void add_event(ISpTTSEngineSite* site, SPEVENTENUM id, ULONGLONG offset,
               WPARAM wparam, LPARAM lparam, SPEVENTLPARAMTYPE ltype)
{
    SPEVENT ev = {};
    ev.eEventId = id;
    ev.elParamType = ltype;
    ev.ullAudioStreamOffset = offset;
    ev.ulStreamNum = 0;
    ev.wParam = wparam;
    ev.lParam = lparam;
    site->AddEvents(&ev, 1);
}

// Writes a run of silence straight to SAPI, for SPVA_Silence fragments.
void write_silence(SpeakContext& ctx, DWORD sample_rate, ULONG msecs)
{
    if (msecs == 0) {
        return;
    }
    const size_t samples = static_cast<size_t>(sample_rate) * msecs / 1000u;
    std::vector<short> zeros(std::min<size_t>(samples, 4096), 0);
    size_t remaining = samples;
    while (remaining > 0 && !ctx.aborted) {
        const size_t chunk = std::min(remaining, zeros.size());
        if (!speak_callback(reinterpret_cast<const char*>(zeros.data()),
                            static_cast<long>(chunk * 2), &ctx)) {
            break;
        }
        remaining -= chunk;
    }
}

}  // namespace

void InitPipeClient()
{
    if (!g_pipeClient) {
        g_pipeClient = new PipeClient();
    }
}

void CleanupPipeClient()
{
    delete g_pipeClient;
    g_pipeClient = nullptr;
}

void ShutdownPipeServer()
{
    if (g_pipeClient) {
        g_pipeClient->shutdownServer();
    }
}

// Engines the user has pinned to the worker by hand, read once per process:
//   HKCU\Software\BestSpeech  REG_SZ  WorkerEngines = "por,rus"   (or "*" for all)
// The automatic fallback below covers this on its own, but a pinned engine never has to
// waste a first silent utterance discovering that it needs the worker.
LONG configured_worker_engines()
{
    static LONG cached = -1;
    if (cached >= 0) {
        return cached;
    }
    cached = 0;

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\BestSpeech", 0, KEY_READ, &key)
        == ERROR_SUCCESS) {
        wchar_t value[256] = {};
        DWORD size = sizeof(value);
        DWORD type = 0;
        if (RegQueryValueExW(key, L"WorkerEngines", nullptr, &type,
                             reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS &&
            type == REG_SZ) {
            const std::wstring list = value;
            if (list == L"*") {
                cached = ~0L;
            } else {
                for (int i = 0; i < engine_count && i < 32; ++i) {
                    std::wstring id;
                    for (const char* c = engines[i].id; *c; ++c) {
                        id += static_cast<wchar_t>(*c);
                    }
                    if (list.find(id) != std::wstring::npos) {
                        cached |= (1L << i);
                    }
                }
            }
        }
        RegCloseKey(key);
    }
    if (cached != 0) {
        DEBUG_LOG("Engines pinned to the worker by configuration: 0x%08lX", cached);
    }
    return cached;
}

bool ISpTTSEngineImpl::engine_needs_worker(int engine_index)
{
    if (engine_index < 0 || engine_index >= 32) {
        return false;
    }
    const LONG mask = InterlockedCompareExchange(&g_worker_engines, 0, 0)
                    | configured_worker_engines();
    return (mask & (1L << engine_index)) != 0;
}

void ISpTTSEngineImpl::mark_engine_needs_worker(int engine_index)
{
    if (engine_index < 0 || engine_index >= 32) {
        return;
    }
    LONG previous, updated;
    do {
        previous = InterlockedCompareExchange(&g_worker_engines, 0, 0);
        updated = previous | (1L << engine_index);
    } while (InterlockedCompareExchange(&g_worker_engines, updated, previous) != previous);
}

ISpTTSEngineImpl::ISpTTSEngineImpl() = default;
ISpTTSEngineImpl::~ISpTTSEngineImpl() = default;

namespace {
// The engine dlls, the shim and the helper all sit beside this dll.
bool install_directory(wchar_t* out, DWORD size)
{
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&install_directory), &self)) {
        return false;
    }
    if (!GetModuleFileNameW(self, out, size)) {
        return false;
    }
    wchar_t* last_slash = wcsrchr(out, L'\\');
    if (!last_slash) {
        return false;
    }
    *(last_slash + 1) = L'\0';

    // The 64-bit engine is installed one level down, in an x64 subfolder, while the
    // engine dlls, the shim and b32_helper.exe all sit beside the 32-bit build. If the
    // helper is not here, the runtime files are in the parent directory.
    std::wstring probe = std::wstring(out) + L"b32_helper.exe";
    if (GetFileAttributesW(probe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        *last_slash = L'\0';                 // drop the trailing separator
        if (wchar_t* parent = wcsrchr(out, L'\\')) {
            *(parent + 1) = L'\0';
        } else {
            *last_slash = L'\\';             // nothing above it; put it back
            *(last_slash + 1) = L'\0';
        }
    }
    return true;
}
}

bool ISpTTSEngineImpl::ensure_helper_started()
{
    const int wanted = voice_.get_engine_index();
    if (helper_.running() && helper_engine_ == wanted) {
        return true;
    }

    wchar_t dir[MAX_PATH] = {};
    if (!install_directory(dir, MAX_PATH)) {
        return false;
    }

    const std::wstring engine_dll = std::wstring(dir) + voice_.engine().dll;
    if (!helper_.start(helper_executable(dir), engine_dll)) {
        DEBUG_LOG("Could not start a helper for engine %s", voice_.engine().id);
        helper_engine_ = -1;
        return false;
    }
    helper_engine_ = wanted;
    return true;
}

#ifndef BUILD_X64
namespace {
long g_probe_bytes = 0;
bool probe_sink(const char*, long n, void*) { g_probe_bytes += n; return true; }
}

// A short utterance whose audio is thrown away, used only to tell a working engine from
// one that will silently produce nothing.
bool ISpTTSEngineImpl::probe_engine_output()
{
    const engine_info& eng = voice_.engine();

    std::wstring probe = text::command_prefix(eng, voices[0], 0, voices[0].pitch, 0);
    probe += L"a";
    if (eng.commands != cmd_mode::none) {
        probe += L" ~|";
    }
    const std::string encoded = text::encode(probe, eng);

    b32::SpeakParams params;
    params.text = encoded.c_str();

    g_probe_bytes = 0;
    b32::speak_async(bst_state_.get(), probe_sink, nullptr, params);
    return g_probe_bytes > 0;
}

bool ISpTTSEngineImpl::ensure_engine_loaded()
{
    const int wanted = voice_.get_engine_index();
    const DWORD thread = GetCurrentThreadId();

    // The shim hooks winmm to capture what the engine plays, and that hook state is per
    // thread: an engine opened on one thread produces nothing at all when synthesized
    // from another, with no error anywhere. SAPI is free to move work between threads,
    // so the engine is reopened whenever the calling thread changes.
    if (bst_state_ && loaded_engine_ == wanted && loaded_thread_ == thread) {
        return true;
    }
    if (bst_state_ && loaded_engine_ == wanted && loaded_thread_ != thread) {
        DEBUG_LOG("Engine %s was opened on thread %lu but is being driven from %lu; reopening",
                  voice_.engine().id, loaded_thread_, thread);
    }

    bst_state_.reset();
    loaded_engine_ = -1;
    loaded_thread_ = 0;

    wchar_t dll_path[MAX_PATH] = {};
    if (!install_directory(dll_path, MAX_PATH)) {
        return false;
    }

    if (!b32::load_shim(dll_path)) {
        DEBUG_LOG("FAILED to load b32_wrapper.dll from %ls (error %lu) -- no voice can "
                  "speak in process without it", dll_path, GetLastError());
        return false;
    }

    std::wstring path = dll_path;
    path += voice_.engine().dll;

    bst_state_ = b32::init(path.c_str());
    if (!bst_state_) {
        const DWORD err = GetLastError();
        const bool present = GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
        DEBUG_LOG("FAILED to open engine %s: %ls (file %s, last error %lu) -- this voice "
                  "will be listed but silent",
                  voice_.engine().id, path.c_str(),
                  present ? "exists" : "IS MISSING", err);
        return false;
    }
    loaded_engine_ = wanted;
    loaded_thread_ = thread;
    DEBUG_LOG("Loaded engine %s from %ls on thread %lu (sample rate %lu, v2=%d)",
              voice_.engine().id, path.c_str(), thread,
              b32::get_sample_rate(bst_state_.get()), (int)b32::is_v2(bst_state_.get()));

    // Prove the engine can actually synthesize before trusting it with real speech.
    // Some engines load cleanly and then return nothing at all when driven in process
    // inside a host application; catching that here means the very first thing the user
    // asks for already goes to the worker, instead of being lost while we find out.
    if (!probe_engine_output()) {
        DEBUG_LOG("Engine %s loaded but produced no audio when probed; using the worker "
                  "for it from now on", voice_.engine().id);
        mark_engine_needs_worker(wanted);
        bst_state_.reset();
        loaded_engine_ = -1;
        return false;
    }
    return true;
}
#endif

STDMETHODIMP ISpTTSEngineImpl::SetObjectToken(ISpObjectToken* pToken)
{
    if (!pToken) {
        return E_INVALIDARG;
    }

    try {
        ISpDataKeyPtr attr;
        if (FAILED(pToken->OpenKey(L"Attributes", &attr))) {
            return E_INVALIDARG;
        }

        // The token carries the engine id and voice index directly, so the exact voice
        // is recovered without having to parse a localized display name back apart.
        int engine_index = 0;
        int voice_index = 0;

        utils::out_ptr<wchar_t> engine_id(CoTaskMemFree);
        if (SUCCEEDED(attr->GetStringValue(L"BstEngine", engine_id.address())) && engine_id.get()) {
            engine_index = engine_by_id(utils::wstring_to_string(engine_id.get()).c_str());
            if (engine_index < 0) {
                engine_index = 0;
            }
        }

        utils::out_ptr<wchar_t> voice_id(CoTaskMemFree);
        if (SUCCEEDED(attr->GetStringValue(L"BstVoice", voice_id.address())) && voice_id.get()) {
            voice_index = _wtoi(voice_id.get());
        }

        voice_ = voice_attributes(engine_index, voice_index);
        token_ = pToken;

        DEBUG_LOG("SetObjectToken: engine=%s voice=%d", voice_.engine().id, voice_.get_voice_index());
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

STDMETHODIMP ISpTTSEngineImpl::GetObjectToken(ISpObjectToken** ppToken)
{
    if (!ppToken) {
        return E_POINTER;
    }
    *ppToken = nullptr;

    if (!token_) {
        return E_UNEXPECTED;
    }
    token_.AddRef();
    *ppToken = token_.GetInterfacePtr();
    return S_OK;
}

STDMETHODIMP ISpTTSEngineImpl::GetOutputFormat(
    const GUID* /*pTargetFmtId*/,
    const WAVEFORMATEX* /*pTargetWaveFormatEx*/,
    GUID* pOutputFormatId,
    WAVEFORMATEX** ppCoMemOutputWaveFormatEx)
{
    if (!pOutputFormatId || !ppCoMemOutputWaveFormatEx) {
        return E_POINTER;
    }

    *pOutputFormatId = SPDFID_WaveFormatEx;
    *ppCoMemOutputWaveFormatEx = nullptr;

    auto* pwfex = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
    if (!pwfex) {
        return E_OUTOFMEMORY;
    }

    // Declared from the table rather than by loading the engine here: the shim keeps
    // its audio-capture state per thread, and SAPI does not promise that GetOutputFormat
    // and Speak run on the same one. Opening the engine on this thread could bind it to
    // a thread that never synthesizes, which is silence.
    pwfex->wFormatTag = WAVE_FORMAT_PCM;
    pwfex->nChannels = AUDIO_CHANNELS;
    pwfex->nSamplesPerSec = voice_.engine().sample_rate;
    pwfex->wBitsPerSample = AUDIO_BITS_PER_SAMPLE;
    pwfex->nBlockAlign = static_cast<WORD>(pwfex->nChannels * pwfex->wBitsPerSample / 8);
    pwfex->nAvgBytesPerSec = pwfex->nSamplesPerSec * pwfex->nBlockAlign;
    pwfex->cbSize = 0;

    *ppCoMemOutputWaveFormatEx = pwfex;
    return S_OK;
}

STDMETHODIMP ISpTTSEngineImpl::Speak(
    DWORD /*dwSpeakFlags*/,
    REFGUID /*rguidFormatId*/,
    const WAVEFORMATEX* /*pWaveFormatEx*/,
    const SPVTEXTFRAG* pTextFragList,
    ISpTTSEngineSite* pOutputSite)
{
    if (!pTextFragList || !pOutputSite) {
        return E_INVALIDARG;
    }

#ifdef BUILD_X64
    if (!g_pipeClient) {
        return E_FAIL;
    }
#endif

    try {
        const engine_info& eng = voice_.engine();
        const voice_info& voice = voice_.voice();

        long sapi_rate = 0;
        pOutputSite->GetRate(&sapi_rate);

        unsigned short sapi_volume = 100;
        pOutputSite->GetVolume(&sapi_volume);

        ULONGLONG event_interest = 0;
        pOutputSite->GetEventInterest(&event_interest);
        const bool want_sentence = (event_interest & SPFEI(SPEI_SENTENCE_BOUNDARY)) != 0;
        const bool want_word = (event_interest & SPFEI(SPEI_WORD_BOUNDARY)) != 0;

        DEBUG_LOG("=== Speak: engine=%s voice=%ls, SAPI rate=%d volume=%u ===",
                  eng.id, voice.name, static_cast<int>(sapi_rate), sapi_volume);

        SpeakContext ctx;
        ctx.caller = pOutputSite;

        for (const SPVTEXTFRAG* frag = pTextFragList; frag; frag = frag->pNext) {
            const DWORD actions = pOutputSite->GetActions();
            if (actions & SPVES_ABORT) {
                break;
            }
            if (actions & SPVES_SKIP) {
                pOutputSite->CompleteSkip(0);
                break;
            }
            // Rate and volume can be changed while an utterance is already playing.
            if (actions & SPVES_RATE) {
                pOutputSite->GetRate(&sapi_rate);
            }
            if (actions & SPVES_VOLUME) {
                pOutputSite->GetVolume(&sapi_volume);
            }

            if (frag->State.eAction == SPVA_Bookmark) {
                const std::wstring mark = (frag->ulTextLen && frag->pTextStart)
                    ? std::wstring(frag->pTextStart, frag->ulTextLen) : std::wstring();
                long id = 0;
                if (!mark.empty()) {
                    id = _wtol(mark.c_str());
                }
                add_event(pOutputSite, SPEI_TTS_BOOKMARK, ctx.bytes_written,
                          static_cast<WPARAM>(id),
                          reinterpret_cast<LPARAM>(mark.c_str()),
                          mark.empty() ? SPET_LPARAM_IS_UNDEFINED : SPET_LPARAM_IS_STRING);
                continue;
            }

            if (frag->State.eAction == SPVA_Silence) {
                write_silence(ctx, eng.sample_rate, frag->State.SilenceMSecs);
                if (ctx.aborted) {
                    break;
                }
                continue;
            }

            // SPVA_Pronounce carries a phoneme string this engine has no way to honour,
            // so its text is spoken normally rather than dropped.
            if (frag->State.eAction != SPVA_Speak &&
                frag->State.eAction != SPVA_SpellOut &&
                frag->State.eAction != SPVA_Pronounce) {
                continue;
            }
            if (frag->ulTextLen == 0 || !frag->pTextStart) {
                continue;
            }

            const std::wstring raw(frag->pTextStart, frag->ulTextLen);

            if (want_sentence) {
                add_event(pOutputSite, SPEI_SENTENCE_BOUNDARY, ctx.bytes_written,
                          frag->ulTextLen, frag->ulTextSrcOffset, SPET_LPARAM_IS_UNDEFINED);
            }
            if (want_word) {
                bool in_word = false;
                ULONG word_start = 0;
                for (ULONG i = 0; i <= frag->ulTextLen; ++i) {
                    const bool word_char = (i < frag->ulTextLen) &&
                        (iswalnum(raw[i]) || raw[i] == L'\'' || raw[i] == L'-');
                    if (word_char && !in_word) {
                        word_start = i;
                        in_word = true;
                    } else if (!word_char && in_word) {
                        add_event(pOutputSite, SPEI_WORD_BOUNDARY, ctx.bytes_written,
                                  i - word_start, frag->ulTextSrcOffset + word_start,
                                  SPET_LPARAM_IS_UNDEFINED);
                        in_word = false;
                    }
                }
            }

            // Fragment adjustments ride on top of the stream-wide settings.
            const int frag_rate = std::clamp<int>(static_cast<int>(sapi_rate) + frag->State.RateAdj,
                                                 SAPI_RATE_MIN, SAPI_RATE_MAX);
            const int frag_pitch = std::clamp<int>(static_cast<int>(frag->State.PitchAdj.MiddleAdj),
                                                  SAPI_PITCH_MIN, SAPI_PITCH_MAX);
            const unsigned short frag_volume = static_cast<unsigned short>(
                std::clamp<int>(sapi_volume * frag->State.Volume / 100, 0, 100));

            synth_params sp = build_params(eng, voice, frag_rate, frag_pitch, frag_volume);

            const std::wstring body = (frag->State.eAction == SPVA_SpellOut)
                ? text::prepare_spelled(raw, eng)
                : text::prepare(raw, eng);
            if (body.empty()) {
                continue;
            }

            // Pitch reaches the tilde engines as a frequency, which they render far more
            // naturally than a pitch shifter could.
            const int pitch_hz = std::clamp(
                static_cast<int>(std::lround(voice.pitch * sapi_pitch_to_factor(frag_pitch))),
                PITCH_MIN_HZ, PITCH_MAX_HZ);

            std::wstring full = text::command_prefix(eng, voice, sp.native_rate,
                                                     pitch_hz, sp.native_gain);
            full += body;
            if (eng.commands != cmd_mode::none) {
                full += L" ~|";  // flush the engine's phrase buffer
            }

            const std::string encoded = text::encode(full, eng);
            if (encoded.empty()) {
                DEBUG_LOG("  fragment encoded to nothing, skipped");
                continue;
            }

            DEBUG_LOG("  engine=%s voice=%ls action=%d rate=%d(native %d, stretch %.2fx) "
                      "pitch=%d(%d hz) volume=%u(gain %d dB, scale %.2fx)",
                      eng.id, voice.name, static_cast<int>(frag->State.eAction),
                      frag_rate, sp.native_rate, sp.sonic_speed,
                      frag_pitch, pitch_hz, frag_volume, sp.native_gain, sp.gain_scale);
            DEBUG_LOG("  -> engine bytes: %s", encoded.c_str());

            const int engine_index = voice_.get_engine_index();
            const ULONGLONG before = ctx.bytes_written;

#ifndef BUILD_X64
            // Every engine dll is 32-bit, so only a 32-bit host can run one in process.
            // A 64-bit host skips straight to the helper below.
            if (!engine_needs_worker(engine_index) && ensure_engine_loaded()) {
                b32::SpeakParams params;
                params.text = encoded.c_str();
                params.sonic_speed = sp.sonic_speed;
                params.gain_scale = sp.gain_scale;
                b32::speak_async(bst_state_.get(), speak_callback, &ctx, params);
            }
#endif

            // Out of process, through a dedicated b32_helper.exe running one engine on
            // the thread that opened it. For a 64-bit host this is the only route. For a
            // 32-bit one it is the recovery path: an engine that returns without a single
            // sample, and was not interrupted, has failed silently in the shim's audio
            // capture, and stays out of process for the rest of this process's life.
            if (ctx.bytes_written == before && !ctx.aborted) {
#ifndef BUILD_X64
                if (!engine_needs_worker(engine_index)) {
                    DEBUG_LOG("  engine %s produced no audio in process; moving it out of "
                              "process for the rest of this session", eng.id);
                    mark_engine_needs_worker(engine_index);
                    bst_state_.reset();
                    loaded_engine_ = -1;
                }
#endif
                if (ensure_helper_started()) {
                    if (!helper_.speak(encoded.c_str(), encoded.size(),
                                       sp.sonic_speed, sp.gain_scale,
                                       speak_callback, &ctx)) {
                        // The helper died; drop it so the next utterance starts a new one.
                        helper_.stop();
                        helper_engine_ = -1;
                    }
                    DEBUG_LOG("  helper produced %llu bytes", ctx.bytes_written - before);
                }
            }

            DEBUG_LOG("  <- %llu bytes this fragment, %llu total%s",
                      ctx.bytes_written - before, ctx.bytes_written,
                      ctx.aborted ? " (host cancelled)" : "");

            if (ctx.aborted) {
                break;
            }
        }

        return S_OK;
    }
    catch (const std::bad_alloc&) {
        DEBUG_LOG("Speak failed: out of memory");
        return E_OUTOFMEMORY;
    }
    catch (...) {
        DEBUG_LOG("Speak failed: unexpected exception");
        return E_UNEXPECTED;
    }
}
}
}
