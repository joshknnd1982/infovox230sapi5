#pragma once

#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <comdef.h>
#include <comip.h>
#include "com.hpp"
#include "voice_attributes.hpp"
#include "helper_client.h"

#ifndef BUILD_X64
#include "b32_wrapper.h"
#endif
#include "pipe_client.h"

namespace Bestspeech {
namespace sapi {

class __declspec(uuid("{fd39c483-34ae-411b-b405-db8b21051abc}")) ISpTTSEngineImpl :
    public ISpTTSEngine, public ISpObjectWithToken
{
public:
    ISpTTSEngineImpl();
    ~ISpTTSEngineImpl();

    ISpTTSEngineImpl(const ISpTTSEngineImpl&) = delete;
    ISpTTSEngineImpl& operator=(const ISpTTSEngineImpl&) = delete;

    STDMETHOD(Speak)(DWORD dwSpeakFlags, REFGUID rguidFormatId,
                     const WAVEFORMATEX* pWaveFormatEx, const SPVTEXTFRAG* pTextFragList,
                     ISpTTSEngineSite* pOutputSite) override;
    STDMETHOD(GetOutputFormat)(const GUID* pTargetFmtId, const WAVEFORMATEX* pTargetWaveFormatEx,
                               GUID* pOutputFormatId, WAVEFORMATEX** ppCoMemOutputWaveFormatEx) override;

    STDMETHOD(SetObjectToken)(ISpObjectToken* pToken) override;
    STDMETHOD(GetObjectToken)(ISpObjectToken** ppToken) override;

protected:
    [[nodiscard]] void* get_interface(REFIID riid) noexcept
    {
        void* ptr = com::try_primary_interface<ISpTTSEngine>(this, riid);
        return ptr ? ptr : com::try_interface<ISpObjectWithToken>(this, riid);
    }

private:
    _COM_SMARTPTR_TYPEDEF(ISpObjectToken, __uuidof(ISpObjectToken));
    _COM_SMARTPTR_TYPEDEF(ISpDataKey, __uuidof(ISpDataKey));

    ISpObjectTokenPtr token_;
    voice_attributes voice_;

#ifndef BUILD_X64
    // Loaded lazily on the thread that synthesizes, never in the constructor: the
    // engine binds its buffer-release message window to whichever thread first runs
    // synthesis, and release messages dispatched from any other thread corrupt its state.
    b32::StatePtr bst_state_;
    int loaded_engine_ = -1;
    // Thread the engine was opened on; the shim's capture state is per thread.
    DWORD loaded_thread_ = 0;

    [[nodiscard]] bool ensure_engine_loaded();

    // Throwaway utterance used to tell a working engine from one that loads
    // cleanly and then produces nothing.
    [[nodiscard]] bool probe_engine_output();
#endif

    // One dedicated b32_helper.exe per engine, running it in a process of its own on the
    // thread that opened it. That is the only route a 64-bit host has to these 32-bit
    // engines, and the recovery path a 32-bit host takes when the in-process shim fails
    // to capture the engine's audio.
    HelperClient helper_;
    int helper_engine_ = -1;

    [[nodiscard]] bool ensure_helper_started();

    // Engines moved out of process after failing in process. The shim's audio capture
    // depends on a thread_local hook state and on the engine echoing back the shim's own
    // pointer; when either misses, the call passes through to real winmm and no audio
    // ever arrives, with no error reported. Once an engine has done that it stays out of
    // process for the life of this one.
    [[nodiscard]] static bool engine_needs_worker(int engine_index);
    static void mark_engine_needs_worker(int engine_index);
};

#ifdef BUILD_X64
void InitPipeClient();
void CleanupPipeClient();
void ShutdownPipeServer();
#endif
}
}
