// End-to-end test host for the SAPI engine.
//
// Loads BestspeechSAPI.dll, creates the real ISpTTSEngine object through
// DllGetClassObject, hands it a voice token, and drives Speak() with a stand-in
// ISpTTSEngineSite that captures the pcm to a wav file. That exercises exactly the code
// path a screen reader takes -- including, in the 64-bit build, the pipe to the 32-bit
// worker -- without needing the engine registered or the shell elevated.
//
//   sapi_probe <BestspeechSAPI.dll> <engine-id> <voice-index> <out.wav> [text]
//              [--rate N] [--pitch N] [--volume N] [--spell]

#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

// --- the site the engine writes its audio and events into -------------------
class CaptureSite : public ISpTTSEngineSite
{
public:
    CaptureSite(long rate, unsigned short volume) : rate_(rate), volume_(volume) {}

    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == __uuidof(ISpEventSink) ||
            riid == __uuidof(ISpTTSEngineSite)) {
            *ppv = static_cast<ISpTTSEngineSite*>(this);
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

    // ISpEventSink
    STDMETHOD(AddEvents)(const SPEVENT* events, ULONG count) override
    {
        for (ULONG i = 0; i < count; ++i) {
            switch (events[i].eEventId) {
                case SPEI_WORD_BOUNDARY:     ++words_; break;
                case SPEI_SENTENCE_BOUNDARY: ++sentences_; break;
                case SPEI_TTS_BOOKMARK:      ++bookmarks_; break;
                default: break;
            }
        }
        return S_OK;
    }
    STDMETHOD(GetEventInterest)(ULONGLONG* interest) override
    {
        *interest = SPFEI(SPEI_WORD_BOUNDARY) | SPFEI(SPEI_SENTENCE_BOUNDARY) |
                    SPFEI(SPEI_TTS_BOOKMARK);
        return S_OK;
    }

    // ISpTTSEngineSite
    STDMETHOD_(DWORD, GetActions)() override { return SPVES_CONTINUE; }
    STDMETHOD(Write)(const void* data, ULONG count, ULONG* written) override
    {
        const auto* p = static_cast<const BYTE*>(data);
        pcm_.insert(pcm_.end(), p, p + count);
        if (written) *written = count;
        return S_OK;
    }
    STDMETHOD(GetRate)(long* rate) override { *rate = rate_; return S_OK; }
    STDMETHOD(GetVolume)(USHORT* volume) override { *volume = volume_; return S_OK; }
    STDMETHOD(GetSkipInfo)(SPVSKIPTYPE* type, long* items) override
    {
        *type = SPVST_SENTENCE;
        *items = 0;
        return S_OK;
    }
    STDMETHOD(CompleteSkip)(long) override { return S_OK; }

    const std::vector<BYTE>& pcm() const { return pcm_; }
    int words() const { return words_; }
    int sentences() const { return sentences_; }
    int bookmarks() const { return bookmarks_; }

private:
    LONG ref_ = 1;
    long rate_;
    unsigned short volume_;
    std::vector<BYTE> pcm_;
    int words_ = 0, sentences_ = 0, bookmarks_ = 0;
};

bool write_wav(const wchar_t* path, const std::vector<BYTE>& pcm, DWORD rate)
{
    FILE* f = _wfopen(path, L"wb");
    if (!f) return false;

    const DWORD data_size = static_cast<DWORD>(pcm.size());
    const DWORD byte_rate = rate * 2;
    struct { char id[4]; DWORD size; } chunk;
    fwrite("RIFF", 1, 4, f);
    DWORD riff = 36 + data_size; fwrite(&riff, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    DWORD fmt_size = 16;      fwrite(&fmt_size, 4, 1, f);
    WORD fmt = 1;             fwrite(&fmt, 2, 1, f);
    WORD ch = 1;              fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    WORD align = 2;           fwrite(&align, 2, 1, f);
    WORD bits = 16;           fwrite(&bits, 2, 1, f);
    memcpy(chunk.id, "data", 4); chunk.size = data_size;
    fwrite(&chunk, 8, 1, f);
    if (data_size) fwrite(pcm.data(), 1, data_size, f);
    fclose(f);
    return true;
}

void pcm_stats(const std::vector<BYTE>& pcm, DWORD rate, double& secs, int& peak, int& rms)
{
    const size_t n = pcm.size() / 2;
    const auto* s = reinterpret_cast<const short*>(pcm.data());
    peak = 0;
    double sum = 0;
    for (size_t i = 0; i < n; ++i) {
        const int v = s[i] < 0 ? -s[i] : s[i];
        if (v > peak) peak = v;
        sum += double(s[i]) * s[i];
    }
    secs = rate ? double(n) / rate : 0.0;
    rms = n ? int(sqrt(sum / n)) : 0;
}

const CLSID CLSID_BestspeechEngine =
    { 0xfd39c483, 0x34ae, 0x411b, { 0xb4, 0x05, 0xdb, 0x8b, 0x21, 0x05, 0x1a, 0xbc } };

using DllGetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);

}  // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc < 5) {
        wprintf(L"usage: sapi_probe <dll> <engine-id> <voice-index> <out.wav> [text] "
                L"[--rate N] [--pitch N] [--volume N] [--spell]\n");
        return 2;
    }

    const wchar_t* dll_path = argv[1];
    const std::wstring engine_id = argv[2];
    const std::wstring voice_index = argv[3];
    const wchar_t* wav_path = argv[4];

    std::wstring text = L"Hello world, this is a test of the speech engine.";
    long rate = 0, pitch = 0;
    unsigned short volume = 100;
    bool spell = false;

    for (int i = 5; i < argc; ++i) {
        if (wcscmp(argv[i], L"--rate") == 0 && i + 1 < argc)        rate = _wtol(argv[++i]);
        else if (wcscmp(argv[i], L"--pitch") == 0 && i + 1 < argc)  pitch = _wtol(argv[++i]);
        else if (wcscmp(argv[i], L"--volume") == 0 && i + 1 < argc) volume = (unsigned short)_wtol(argv[++i]);
        else if (wcscmp(argv[i], L"--spell") == 0)                  spell = true;
        else if (i == 5)                                            text = argv[i];
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    HMODULE mod = LoadLibraryW(dll_path);
    if (!mod) {
        wprintf(L"FAIL: LoadLibrary(%s) -> %lu\n", dll_path, GetLastError());
        return 1;
    }
    auto get_class = reinterpret_cast<DllGetClassObjectFn>(GetProcAddress(mod, "DllGetClassObject"));
    if (!get_class) {
        wprintf(L"FAIL: DllGetClassObject not exported\n");
        return 1;
    }

    IClassFactory* factory = nullptr;
    HRESULT hr = get_class(CLSID_BestspeechEngine, IID_IClassFactory, (void**)&factory);
    if (FAILED(hr) || !factory) {
        wprintf(L"FAIL: DllGetClassObject -> 0x%08X\n", hr);
        return 1;
    }

    ISpTTSEngine* engine = nullptr;
    hr = factory->CreateInstance(nullptr, __uuidof(ISpTTSEngine), (void**)&engine);
    factory->Release();
    if (FAILED(hr) || !engine) {
        wprintf(L"FAIL: CreateInstance -> 0x%08X\n", hr);
        return 1;
    }

    ISpObjectWithToken* with_token = nullptr;
    hr = engine->QueryInterface(__uuidof(ISpObjectWithToken), (void**)&with_token);
    if (FAILED(hr)) {
        wprintf(L"FAIL: QueryInterface(ISpObjectWithToken) -> 0x%08X\n", hr);
        return 1;
    }
    auto* token = new FakeToken(engine_id, voice_index);
    hr = with_token->SetObjectToken(token);
    if (FAILED(hr)) {
        wprintf(L"FAIL: SetObjectToken -> 0x%08X\n", hr);
        return 1;
    }

    GUID fmt_id = {};
    WAVEFORMATEX* wfex = nullptr;
    hr = engine->GetOutputFormat(nullptr, nullptr, &fmt_id, &wfex);
    if (FAILED(hr) || !wfex) {
        wprintf(L"FAIL: GetOutputFormat -> 0x%08X\n", hr);
        return 1;
    }
    const DWORD sample_rate = wfex->nSamplesPerSec;

    SPVTEXTFRAG frag = {};
    frag.pNext = nullptr;
    frag.State.eAction = spell ? SPVA_SpellOut : SPVA_Speak;
    frag.State.LangID = 0x409;
    frag.State.EmphAdj = 0;
    frag.State.RateAdj = 0;
    frag.State.Volume = 100;
    frag.State.PitchAdj.MiddleAdj = pitch;
    frag.State.PitchAdj.RangeAdj = 0;
    frag.State.SilenceMSecs = 0;
    frag.pTextStart = text.c_str();
    frag.ulTextLen = static_cast<ULONG>(text.size());
    frag.ulTextSrcOffset = 0;

    auto* site = new CaptureSite(rate, volume);
    hr = engine->Speak(0, GUID_NULL, wfex, &frag, site);

    double secs = 0; int peak = 0, rms = 0;
    pcm_stats(site->pcm(), sample_rate, secs, peak, rms);
    write_wav(wav_path, site->pcm(), sample_rate);

    wprintf(L"%s voice %s | Speak -> 0x%08X | %lu hz | %.2fs peak %d rms %d | "
            L"events: %d word, %d sentence, %d bookmark\n",
            engine_id.c_str(), voice_index.c_str(), hr, sample_rate, secs, peak, rms,
            site->words(), site->sentences(), site->bookmarks());

    const bool ok = SUCCEEDED(hr) && peak > 200 && secs > 0.05;
    site->Release();
    CoTaskMemFree(wfex);
    with_token->Release();
    engine->Release();
    token->Release();
    CoUninitialize();
    return ok ? 0 : 1;
}
