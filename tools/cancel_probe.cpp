// Hammers the engine with utterances the host abandons part way through, which is what a
// screen reader does whenever the user keeps moving. A cancel that leaves the helper
// protocol out of step shows up here as a hang, a memory explosion, or silence on the
// utterances that follow.
#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>
#include <cstdio>
#include <string>

namespace {
// --- a data key holding just the attributes SetObjectToken reads -------------
class FakeDataKey : public ISpDataKey
{
public:
    FakeDataKey(const std::wstring& engine, const std::wstring& voice)
        : engine_(engine), voice_(voice) {}

    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == __uuidof(ISpDataKey)) {
            *ppv = static_cast<ISpDataKey*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override { return InterlockedIncrement(&ref_); }
    STDMETHOD_(ULONG, Release)() override
    {
        const LONG n = InterlockedDecrement(&ref_);
        if (n == 0) delete this;
        return n;
    }

    STDMETHOD(GetStringValue)(LPCWSTR name, LPWSTR* value) override
    {
        const wchar_t* found = nullptr;
        if (name && _wcsicmp(name, L"BstEngine") == 0) found = engine_.c_str();
        else if (name && _wcsicmp(name, L"BstVoice") == 0) found = voice_.c_str();
        if (!found) return SPERR_NOT_FOUND;

        const size_t bytes = (wcslen(found) + 1) * sizeof(wchar_t);
        *value = static_cast<LPWSTR>(CoTaskMemAlloc(bytes));
        if (!*value) return E_OUTOFMEMORY;
        memcpy(*value, found, bytes);
        return S_OK;
    }

    STDMETHOD(SetData)(LPCWSTR, ULONG, const BYTE*) override { return E_NOTIMPL; }
    STDMETHOD(GetData)(LPCWSTR, ULONG*, BYTE*) override { return E_NOTIMPL; }
    STDMETHOD(SetStringValue)(LPCWSTR, LPCWSTR) override { return E_NOTIMPL; }
    STDMETHOD(SetDWORD)(LPCWSTR, DWORD) override { return E_NOTIMPL; }
    STDMETHOD(GetDWORD)(LPCWSTR, DWORD*) override { return E_NOTIMPL; }
    STDMETHOD(OpenKey)(LPCWSTR, ISpDataKey**) override { return SPERR_NOT_FOUND; }
    STDMETHOD(CreateKey)(LPCWSTR, ISpDataKey**) override { return E_NOTIMPL; }
    STDMETHOD(DeleteKey)(LPCWSTR) override { return E_NOTIMPL; }
    STDMETHOD(DeleteValue)(LPCWSTR) override { return E_NOTIMPL; }
    STDMETHOD(EnumKeys)(ULONG, LPWSTR*) override { return SPERR_NO_MORE_ITEMS; }
    STDMETHOD(EnumValues)(ULONG, LPWSTR*) override { return SPERR_NO_MORE_ITEMS; }

private:
    LONG ref_ = 1;
    std::wstring engine_, voice_;
};

// --- a token whose only job is to hand back that attributes key --------------
class FakeToken : public ISpObjectToken
{
public:
    FakeToken(const std::wstring& engine, const std::wstring& voice)
        : engine_(engine), voice_(voice) {}

    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == __uuidof(ISpDataKey) ||
            riid == __uuidof(ISpObjectToken)) {
            *ppv = static_cast<ISpObjectToken*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override { return InterlockedIncrement(&ref_); }
    STDMETHOD_(ULONG, Release)() override
    {
        const LONG n = InterlockedDecrement(&ref_);
        if (n == 0) delete this;
        return n;
    }

    STDMETHOD(OpenKey)(LPCWSTR name, ISpDataKey** key) override
    {
        if (!name || _wcsicmp(name, L"Attributes") != 0) return SPERR_NOT_FOUND;
        *key = new FakeDataKey(engine_, voice_);
        return S_OK;
    }

    STDMETHOD(SetData)(LPCWSTR, ULONG, const BYTE*) override { return E_NOTIMPL; }
    STDMETHOD(GetData)(LPCWSTR, ULONG*, BYTE*) override { return E_NOTIMPL; }
    STDMETHOD(SetStringValue)(LPCWSTR, LPCWSTR) override { return E_NOTIMPL; }
    STDMETHOD(GetStringValue)(LPCWSTR, LPWSTR*) override { return SPERR_NOT_FOUND; }
    STDMETHOD(SetDWORD)(LPCWSTR, DWORD) override { return E_NOTIMPL; }
    STDMETHOD(GetDWORD)(LPCWSTR, DWORD*) override { return E_NOTIMPL; }
    STDMETHOD(CreateKey)(LPCWSTR, ISpDataKey**) override { return E_NOTIMPL; }
    STDMETHOD(DeleteKey)(LPCWSTR) override { return E_NOTIMPL; }
    STDMETHOD(DeleteValue)(LPCWSTR) override { return E_NOTIMPL; }
    STDMETHOD(EnumKeys)(ULONG, LPWSTR*) override { return SPERR_NO_MORE_ITEMS; }
    STDMETHOD(EnumValues)(ULONG, LPWSTR*) override { return SPERR_NO_MORE_ITEMS; }

    STDMETHOD(SetId)(LPCWSTR, LPCWSTR, BOOL) override { return E_NOTIMPL; }
    STDMETHOD(GetId)(LPWSTR*) override { return E_NOTIMPL; }
    STDMETHOD(GetCategory)(ISpObjectTokenCategory**) override { return E_NOTIMPL; }
    STDMETHOD(CreateInstance)(IUnknown*, DWORD, REFIID, void**) override { return E_NOTIMPL; }
    STDMETHOD(GetStorageFileName)(REFCLSID, LPCWSTR, LPCWSTR, ULONG, LPWSTR*) override { return E_NOTIMPL; }
    STDMETHOD(RemoveStorageFileName)(REFCLSID, LPCWSTR, BOOL) override { return E_NOTIMPL; }
    STDMETHOD(Remove)(const CLSID*) override { return E_NOTIMPL; }
    STDMETHOD(IsUISupported)(LPCWSTR, void*, ULONG, IUnknown*, BOOL*) override { return E_NOTIMPL; }
    STDMETHOD(DisplayUI)(HWND, LPCWSTR, LPCWSTR, void*, ULONG, IUnknown*) override { return E_NOTIMPL; }
    STDMETHOD(MatchesAttributes)(LPCWSTR, BOOL*) override { return E_NOTIMPL; }

private:
    LONG ref_ = 1;
    std::wstring engine_, voice_;
};


const CLSID CLSID_Bestspeech =
    { 0xfd39c483, 0x34ae, 0x411b, { 0xb4, 0x05, 0xdb, 0x8b, 0x21, 0x05, 0x1a, 0xbc } };
using GetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);

// A site that abandons the utterance after a set number of chunks.
class AbortingSite : public ISpTTSEngineSite {
public:
    explicit AbortingSite(int chunks_before_abort) : limit_(chunks_before_abort) {}
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == __uuidof(ISpEventSink) || riid == __uuidof(ISpTTSEngineSite)) {
            *ppv = static_cast<ISpTTSEngineSite*>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override { return InterlockedIncrement(&ref_); }
    STDMETHOD_(ULONG, Release)() override {
        const LONG n = InterlockedDecrement(&ref_); if (!n) delete this; return n;
    }
    STDMETHOD(AddEvents)(const SPEVENT*, ULONG) override { return S_OK; }
    STDMETHOD(GetEventInterest)(ULONGLONG* i) override { *i = 0; return S_OK; }
    STDMETHOD_(DWORD, GetActions)() override {
        return (limit_ >= 0 && chunks_ >= limit_) ? SPVES_ABORT : SPVES_CONTINUE;
    }
    STDMETHOD(Write)(const void*, ULONG count, ULONG* written) override {
        ++chunks_; bytes_ += count; if (written) *written = count; return S_OK;
    }
    STDMETHOD(GetRate)(long* r) override { *r = 0; return S_OK; }
    STDMETHOD(GetVolume)(USHORT* v) override { *v = 100; return S_OK; }
    STDMETHOD(GetSkipInfo)(SPVSKIPTYPE* t, long* n) override { *t = SPVST_SENTENCE; *n = 0; return S_OK; }
    STDMETHOD(CompleteSkip)(long) override { return S_OK; }
    ULONGLONG bytes() const { return bytes_; }
private:
    LONG ref_ = 1; int limit_; int chunks_ = 0; ULONGLONG bytes_ = 0;
};
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) { wprintf(L"usage: cancel_probe <dll> <engine-id> [rounds]\n"); return 2; }
    const int rounds = (argc > 3) ? _wtoi(argv[3]) : 40;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    HMODULE mod = LoadLibraryW(argv[1]);
    if (!mod) { wprintf(L"load failed\n"); return 1; }
    auto gco = reinterpret_cast<GetClassObjectFn>(GetProcAddress(mod, "DllGetClassObject"));
    IClassFactory* factory = nullptr;
    if (FAILED(gco(CLSID_Bestspeech, IID_IClassFactory, (void**)&factory))) { wprintf(L"no factory\n"); return 1; }

    ISpTTSEngine* engine = nullptr;
    factory->CreateInstance(nullptr, __uuidof(ISpTTSEngine), (void**)&engine);
    factory->Release();

    // Token: reuse sapi_probe's stand-in by going through the registry-free route.
    ISpObjectWithToken* with_token = nullptr;
    if (FAILED(engine->QueryInterface(__uuidof(ISpObjectWithToken), (void**)&with_token))) {
        wprintf(L"no ISpObjectWithToken\n");
        return 1;
    }
    auto* token = new FakeToken(argv[2], L"0");
    const HRESULT set = with_token->SetObjectToken(token);
    if (FAILED(set)) {
        wprintf(L"SetObjectToken(%s) failed 0x%08X\n", argv[2], set);
        return 1;
    }
    wprintf(L"engine %s selected; running %d rounds\n", argv[2], rounds);

    const std::wstring text = L"Ola, este e um teste longo do sintetizador de voz em portugues, "
                              L"com varias frases para dar tempo de cancelar no meio.";
    SPVTEXTFRAG frag = {};
    frag.State.eAction = SPVA_Speak;
    frag.State.LangID = 0x816;
    frag.State.Volume = 100;
    frag.pTextStart = text.c_str();
    frag.ulTextLen = static_cast<ULONG>(text.size());

    const DWORD started = GetTickCount();
    ULONGLONG total = 0;
    int silent = 0;
    for (int i = 0; i < rounds; ++i) {
        // Alternate: abandon after 1 chunk, after 3, and let some run to completion.
        auto* site = new AbortingSite((i % 3 == 2) ? -1 : (i % 3) + 1);
        const HRESULT hr = engine->Speak(0, GUID_NULL, nullptr, &frag, site);
        if (FAILED(hr)) { wprintf(L"round %d: Speak 0x%08X\n", i, hr); }
        if (site->bytes() == 0) ++silent;
        total += site->bytes();
        site->Release();
        if (GetTickCount() - started > 120000) { wprintf(L"TIMED OUT at round %d\n", i); return 1; }
    }
    wprintf(L"%d rounds in %lu ms, %llu bytes total, %d produced nothing\n",
            rounds, GetTickCount() - started, total, silent);
    engine->Release();
    CoUninitialize();
    return 0;
}
