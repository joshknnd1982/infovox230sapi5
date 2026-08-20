// Infovox230SAPI.dll -- the SAPI5 engine, built once for 32-bit hosts and once
// for 64-bit ones. Both are thin clients of the 32-bit worker.

#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <olectl.h>  // SELFREG_E_CLASS

#include <new>
#include <string>

#include "ivx_client.h"
#include "ivx_com.h"
#include "ivx_log.h"
#include "ivx_paths.h"
#include "ivx_sapi_engine.h"
#include "ivx_sapi_tokens.h"

namespace {

HINSTANCE g_instance = nullptr;

std::wstring this_dll_path()
{
    wchar_t path[MAX_PATH] = L"";
    GetModuleFileNameW(g_instance, path, MAX_PATH);
    return path;
}

template <class T>
HRESULT make_class_object(REFIID riid, void** ppv)
{
    try {
        ivx::com::Ref<ivx::com::ClassFactory<T>> factory =
            ivx::com::Ref<ivx::com::ClassFactory<T>>::make();
        return factory->QueryInterface(riid, ppv);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_UNEXPECTED;
    }
}

}  // namespace

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_instance = instance;
        DisableThreadLibraryCalls(instance);
        ivx::log_init(sizeof(void*) == 8 ? "sapi64" : "sapi32");
    } else if (reason == DLL_PROCESS_DETACH) {
        ivx::log_shutdown();
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    if (!ppv) {
        return E_POINTER;
    }
    *ppv = nullptr;

    if (rclsid == __uuidof(ivx::sapi5::TtsEngine)) {
        return make_class_object<ivx::sapi5::TtsEngine>(riid, ppv);
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow()
{
    return ivx::com::ObjectCount::zero() ? S_OK : S_FALSE;
}

namespace {

bool register_under(HKEY root, const std::wstring& dll)
{
    if (!ivx::com::register_class(root, __uuidof(ivx::sapi5::TtsEngine), dll,
                                  L"Infovox 230 SAPI5 engine")) {
        return false;
    }
    return ivx::sapi5::register_voices(root);
}

}  // namespace

STDAPI DllRegisterServer()
{
    ivx::log_init(sizeof(void*) == 8 ? "sapi64" : "sapi32");

    const std::wstring dll = this_dll_path();
    IVX_INFO("register: registering %S", dll.c_str());

    if (register_under(HKEY_LOCAL_MACHINE, dll)) {
        IVX_INFO("register: registered for all users; %u voices published",
                 static_cast<unsigned>(ivx::sapi5::shared_catalog().size()));
        return S_OK;
    }

    // There is deliberately no per-user fallback. Voice tokens written under
    // HKEY_CURRENT_USER are accepted by the registry but never enumerated by
    // SAPI5 -- measured on Windows 11, where a full set of HKCU tokens produced
    // exactly zero visible voices. Registering there would report success and
    // leave the user with silence, so failing loudly is the kinder answer.
    IVX_ERROR("register: HKEY_LOCAL_MACHINE is not writable. SAPI5 only enumerates voices "
              "registered for all users, so this must be run as administrator.");
    return SELFREG_E_CLASS;
}

STDAPI DllUnregisterServer()
{
    ivx::log_init(sizeof(void*) == 8 ? "sapi64" : "sapi32");
    IVX_INFO("register: unregistering");

    // The worker holds the engine files open on behalf of every client, so it
    // has to be gone before an uninstaller can replace or delete them.
    {
        ivx::WorkerClient client;
        client.shutdown_worker();
    }

    // Both roots, because an earlier install may have used either.
    for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        ivx::sapi5::unregister_voices(root);
        ivx::com::unregister_class(root, __uuidof(ivx::sapi5::TtsEngine));
    }
    return S_OK;
}
