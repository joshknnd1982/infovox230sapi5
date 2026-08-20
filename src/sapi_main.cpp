#include <new>
#include <sapi.h>
#include "com.hpp"
#include "registry.hpp"
#include "voice_attributes.hpp"
#include "ISpTTSEngineImpl.hpp"
#include "IEnumSpObjectTokensImpl.hpp"
#include "voice_registry.hpp"

#ifdef BUILD_X64
#include "pipe_client.h"
#else
#include "b32_wrapper.h"
#endif


namespace {

HINSTANCE g_dll_handle = nullptr;
Bestspeech::com::class_object_factory g_cls_obj_factory;

[[nodiscard]] std::wstring clsid_to_string(const GUID& clsid)
{
    wchar_t buf[64];
    StringFromGUID2(clsid, buf, 64);
    return std::wstring(buf);
}

// Which registry view these land in is decided by the architecture of the dll doing the
// registering: the 32-bit build writes under WOW6432Node, where 32-bit SAPI looks, and
// the 64-bit build writes to the native view, where 64-bit hosts look. Both are
// installed and registered, so every host sees the full set.
void register_voice_tokens()
{
    Bestspeech::sapi::write_voice_tokens(
        HKEY_LOCAL_MACHINE,
        clsid_to_string(__uuidof(Bestspeech::sapi::ISpTTSEngineImpl)));
}

void unregister_voice_tokens() noexcept
{
    Bestspeech::sapi::remove_voice_tokens(HKEY_LOCAL_MACHINE);
}
}

BOOL APIENTRY DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID /*lpReserved*/)
{
    if (dwReason == DLL_PROCESS_ATTACH) {
        g_dll_handle = hInstance;
        DisableThreadLibraryCalls(hInstance);

#ifdef BUILD_X64
        // The engine dlls are 32-bit, so a 64-bit host reaches them through the worker.
        Bestspeech::sapi::InitPipeClient();
#endif

        try {
            g_cls_obj_factory.register_class<Bestspeech::sapi::IEnumSpObjectTokensImpl>();
            g_cls_obj_factory.register_class<Bestspeech::sapi::ISpTTSEngineImpl>();
        }
        catch (...) {
            return FALSE;
        }
    }
#ifdef BUILD_X64
    else if (dwReason == DLL_PROCESS_DETACH) {
        Bestspeech::sapi::CleanupPipeClient();
    }
#endif
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    return g_cls_obj_factory.create(rclsid, riid, ppv);
}

STDAPI DllCanUnloadNow()
{
    return Bestspeech::com::object_counter::is_zero() ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()
{
    try {
        Bestspeech::com::class_registrar r(g_dll_handle);
        r.register_class<Bestspeech::sapi::IEnumSpObjectTokensImpl>();
        r.register_class<Bestspeech::sapi::ISpTTSEngineImpl>();
        register_voice_tokens();
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

STDAPI DllUnregisterServer()
{
    try {
#ifdef BUILD_X64
        Bestspeech::sapi::ShutdownPipeServer();
#endif
        unregister_voice_tokens();
        Bestspeech::com::class_registrar r(g_dll_handle);
        r.unregister_class<Bestspeech::sapi::IEnumSpObjectTokensImpl>();
        r.unregister_class<Bestspeech::sapi::ISpTTSEngineImpl>();
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}
