#include "ivx_engine.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "ivx_log.h"
#include "ivx_vregistry.h"

namespace ivx {

using namespace ivx::sapi4;

namespace {

std::string narrow(const std::wstring& s)
{
    if (s.empty()) {
        return std::string();
    }
    const int n = WideCharToMultiByte(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0,
                                      nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_ACP, 0, s.c_str(), static_cast<int>(s.size()), &out[0], n, nullptr,
                        nullptr);
    return out;
}

std::string guid_to_string(const GUID& g)
{
    wchar_t buf[64];
    StringFromGUID2(g, buf, 64);
    std::wstring w(buf);
    // StringFromGUID2 upper-cases; the catalogue is lower-case. Comparisons are
    // case-insensitive everywhere, but keeping one spelling makes logs readable.
    for (wchar_t& ch : w) {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    return narrow(w);
}

bool string_to_guid(const std::string& s, GUID* out)
{
    const std::wstring w(s.begin(), s.end());
    return SUCCEEDED(CLSIDFromString(w.c_str(), out));
}

// SAPI4 posts its callbacks to the thread message queue, so waiting means
// pumping rather than sleeping.
void pump_messages()
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// How long to wait for the engine's next callback before looking at the cancel
// flag and the deadline again. Only reached when no message is waiting, so it
// bounds how quickly a cancel is noticed rather than how fast synthesis runs.
constexpr DWORD kPollMs = 2;

// Blocks until the engine posts its next callback.
//
// This used to be Sleep(1), which was the single biggest thing standing between
// a keypress and hearing it: Windows' default timer resolution makes Sleep(1)
// take about 15.6 ms, and the engine posts one message per chunk of audio, so a
// two-second utterance spent over half a second doing nothing but sleeping
// between chunks. The process burned almost no CPU while it did it. Waiting on
// the message queue instead returns the instant the engine has something to say.
void await_engine()
{
    MsgWaitForMultipleObjectsEx(0, nullptr, kPollMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
}

template <typename T>
void safe_release(T*& p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// The sinks we hand the engine. Hand-rolled IUnknown rather than ATL: these
// objects only ever live inside this process and are only ever seen by the
// engine, and a dependency-free worker is easier to ship.
// ---------------------------------------------------------------------------

class CaptureAudio : public IAudio, public IAudioDest {
public:
    CaptureAudio() = default;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) {
            return E_POINTER;
        }
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudio)) {
            *ppv = static_cast<IAudio*>(this);
        } else if (riid == __uuidof(IAudioDest)) {
            *ppv = static_cast<IAudioDest*>(this);
        } else {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override
    {
        const LONG n = InterlockedDecrement(&refs_);
        if (n == 0) {
            delete this;
        }
        return static_cast<ULONG>(n);
    }

    // IAudio
    STDMETHODIMP Flush() override { return S_OK; }
    STDMETHODIMP LevelGet(DWORD* pdwLevel) override
    {
        if (pdwLevel) {
            *pdwLevel = level_;
        }
        return S_OK;
    }
    STDMETHODIMP LevelSet(DWORD dwLevel) override
    {
        // Loudness is something the engine hands to the audio device rather
        // than something it applies to the samples: a \Vol=..\ tag and
        // ITTSAttributes::VolumeSet both arrive here, in the dual-channel
        // layout waveOutSetVolume uses. A capture sink is the device as far as
        // the engine is concerned, so it has to do the scaling itself --
        // storing the level and nothing else makes every volume control a
        // silent no-op.
        level_ = dwLevel;
        const DWORD left = dwLevel & 0xFFFF;
        const DWORD right = (dwLevel >> 16) & 0xFFFF;
        gain_ = (std::max)(left, right) / double(0xFFFF);
        return S_OK;
    }
    STDMETHODIMP PassNotify(void* pNotifyInterface, GUID) override
    {
        notify_ = static_cast<IAudioDestNotifySink*>(pNotifyInterface);
        return S_OK;
    }
    STDMETHODIMP PosnGet(QWORD* pq) override
    {
        // Everything handed to us counts as already played: there is no device
        // to wait for, and reporting otherwise makes the engine stall.
        if (pq) {
            *pq = written_;
        }
        return S_OK;
    }
    STDMETHODIMP Claim() override
    {
        if (notify_) {
            notify_->AudioStart();
        }
        return S_OK;
    }
    STDMETHODIMP UnClaim() override
    {
        if (notify_) {
            notify_->AudioStop(0);
        }
        return S_OK;
    }
    STDMETHODIMP Start() override { return S_OK; }
    STDMETHODIMP Stop() override { return S_OK; }
    STDMETHODIMP TotalGet(QWORD* pq) override
    {
        if (pq) {
            *pq = written_;
        }
        return S_OK;
    }
    STDMETHODIMP ToFileTime(QWORD*, FILETIME* pft) override
    {
        if (pft) {
            pft->dwLowDateTime = 0;
            pft->dwHighDateTime = 0;
        }
        return S_OK;
    }
    STDMETHODIMP WaveFormatGet(SDATA* pdWFEX) override
    {
        if (!pdWFEX) {
            return E_POINTER;
        }
        if (!have_format_) {
            return E_FAIL;
        }
        void* mem = CoTaskMemAlloc(sizeof(WAVEFORMATEX));
        if (!mem) {
            return E_OUTOFMEMORY;
        }
        memcpy(mem, &wfx_, sizeof(WAVEFORMATEX));
        pdWFEX->pData = mem;
        pdWFEX->dwSize = sizeof(WAVEFORMATEX);
        return S_OK;
    }
    STDMETHODIMP WaveFormatSet(SDATA dWFEX) override
    {
        if (dWFEX.pData && dWFEX.dwSize) {
            const DWORD n = (std::min)(dWFEX.dwSize, static_cast<DWORD>(sizeof(WAVEFORMATEX)));
            memset(&wfx_, 0, sizeof(wfx_));
            memcpy(&wfx_, dWFEX.pData, n);
            have_format_ = true;
            IVX_INFO("audio: engine chose %lu Hz, %u channel(s), %u-bit (tag %u)",
                     static_cast<unsigned long>(wfx_.nSamplesPerSec), wfx_.nChannels,
                     wfx_.wBitsPerSample, wfx_.wFormatTag);
            // Claiming a generous buffer keeps the engine from throttling on
            // FreeSpace; nothing is actually queued, so there is no cost.
            free_space_ = (std::max)(wfx_.nAvgBytesPerSec * 4, 1UL << 20);
        }
        return S_OK;
    }

    // IAudioDest
    STDMETHODIMP FreeSpace(DWORD* pdwBytes, BOOL* pfEOF) override
    {
        if (pdwBytes) {
            *pdwBytes = free_space_;
        }
        if (pfEOF) {
            *pfEOF = FALSE;
        }
        return S_OK;
    }
    STDMETHODIMP DataSet(void* pBuffer, DWORD dwSize) override
    {
        IVX_TRACE("audio: DataSet %lu bytes at t+%lu ms",
                  static_cast<unsigned long>(dwSize),
                  static_cast<unsigned long>(GetTickCount64() - begin_tick_));
        if (pBuffer && dwSize) {
            const void* data = pBuffer;
            if (gain_ < 0.999) {
                data = apply_gain(pBuffer, dwSize);
            }
            if (sink_ && !sink_(data, dwSize)) {
                consumer_gone_ = true;
            }
            written_ += dwSize;
        }
        return S_OK;
    }
    STDMETHODIMP BookMark(DWORD dwMarkID) override
    {
        // The engine is registering an audio-position mark with its own
        // sequential id. Since our "device" plays instantly, report it reached
        // straight away; the engine then raises the real \mrk=N\ number on the
        // buffer notify sink, which is the one the caller asked about.
        if (notify_) {
            notify_->BookMark(dwMarkID, FALSE);
        }
        return S_OK;
    }

    // Tells the engine its audio destination has room for more. A real sound
    // card raises this as its buffers complete; without it the engine falls back
    // to topping the device up on a timer of its own, roughly five times slower
    // than it can actually synthesise.
    void request_more()
    {
        if (notify_) {
            notify_->FreeSpace(free_space_, FALSE);
        }
    }

    void begin(const PcmSink& sink)
    {
        sink_ = sink;
        written_ = 0;
        consumer_gone_ = false;
        begin_tick_ = GetTickCount64();
    }
    void end() { sink_ = nullptr; }

    unsigned long written() const { return static_cast<unsigned long>(written_); }
    bool consumer_gone() const { return consumer_gone_; }
    bool have_format() const { return have_format_; }
    const WAVEFORMATEX& wfx() const { return wfx_; }

private:
    ~CaptureAudio() = default;

    // Scales a chunk into a scratch buffer. The engine reuses the buffer it
    // hands us, so the samples cannot be modified in place; the length never
    // changes, which keeps the byte offsets used for word and bookmark
    // positions valid.
    const void* apply_gain(void* buffer, DWORD size)
    {
        scratch_.assign(static_cast<const BYTE*>(buffer),
                        static_cast<const BYTE*>(buffer) + size);

        if (gain_ <= 0.0005) {
            // True silence. 8-bit PCM is unsigned and centred on 0x80.
            const BYTE quiet = (have_format_ && wfx_.wBitsPerSample == 8) ? 0x80 : 0x00;
            memset(scratch_.data(), quiet, scratch_.size());
            return scratch_.data();
        }

        if (have_format_ && wfx_.wBitsPerSample == 8) {
            for (BYTE& b : scratch_) {
                const int scaled = 128 + static_cast<int>((static_cast<int>(b) - 128) * gain_);
                b = static_cast<BYTE>((std::max)(0, (std::min)(255, scaled)));
            }
            return scratch_.data();
        }

        auto* samples = reinterpret_cast<short*>(scratch_.data());
        const size_t count = scratch_.size() / sizeof(short);
        for (size_t i = 0; i < count; ++i) {
            const int scaled = static_cast<int>(samples[i] * gain_);
            samples[i] = static_cast<short>((std::max)(-32768, (std::min)(32767, scaled)));
        }
        return scratch_.data();
    }

    LONG refs_ = 1;
    IAudioDestNotifySink* notify_ = nullptr;
    WAVEFORMATEX wfx_ = {};
    bool have_format_ = false;
    DWORD level_ = 0xFFFFFFFF;
    double gain_ = 1.0;
    DWORD free_space_ = 1UL << 20;
    ULONGLONG written_ = 0;
    ULONGLONG begin_tick_ = 0;
    bool consumer_gone_ = false;
    PcmSink sink_;
    std::vector<BYTE> scratch_;
};

class BufNotifySink : public ITTSBufNotifySink {
public:
    explicit BufNotifySink(CaptureAudio* audio) : audio_(audio) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) {
            return E_POINTER;
        }
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ITTSBufNotifySink)) {
            *ppv = static_cast<ITTSBufNotifySink*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override
    {
        const LONG n = InterlockedDecrement(&refs_);
        if (n == 0) {
            delete this;
        }
        return static_cast<ULONG>(n);
    }

    STDMETHODIMP TextDataDone(QWORD, DWORD dwFlags) override
    {
        IVX_TRACE("sink: TextDataDone at written=%lu",
                  audio_ ? audio_->written() : 0);
        done_ = true;
        done_flags_ = dwFlags;
        return S_OK;
    }
    STDMETHODIMP TextDataStarted(QWORD) override
    {
        started_ = true;
        return S_OK;
    }
    STDMETHODIMP BookMark(QWORD qTimeStamp, DWORD dwMarkNum) override
    {
        emit(SpeakEvent::Bookmark, qTimeStamp, dwMarkNum);
        return S_OK;
    }
    STDMETHODIMP WordPosition(QWORD qTimeStamp, DWORD dwByteOffset) override
    {
        emit(SpeakEvent::Word, qTimeStamp, dwByteOffset);
        return S_OK;
    }

    void begin(const EventSink& events, unsigned long bytes_per_sec)
    {
        events_ = events;
        window_ = bytes_per_sec ? bytes_per_sec : 32000;
        done_ = false;
        started_ = false;
        last_offset_ = 0;
    }
    void end() { events_ = nullptr; }
    bool done() const { return done_; }

private:
    ~BufNotifySink() = default;

    // The engine's qTimeStamp is a byte position inside the current one-second
    // window of output, not an absolute one: across an utterance it repeatedly
    // counts up and restarts. Adding the window the sink was in when the
    // callback arrived reconstructs the absolute position, which measurement
    // against evenly spaced bookmarks confirms to the byte.
    void emit(SpeakEvent::Kind kind, QWORD qts, DWORD value)
    {
        const unsigned long written = audio_ ? audio_->written() : 0;
        IVX_TRACE("sink: %s qts=%llu written=%lu value=%lu window=%lu",
                  kind == SpeakEvent::Bookmark ? "mark" : "word",
                  static_cast<unsigned long long>(qts), written,
                  static_cast<unsigned long>(value), window_);
        if (!events_) {
            return;
        }
        const unsigned long base = (written / window_) * window_;
        unsigned long offset = base + static_cast<unsigned long>(qts);
        if (static_cast<unsigned long>(qts) >= window_) {
            // Not the shape we measured; fall back to what has been produced.
            offset = written;
        }
        offset = (std::max)(offset, last_offset_);
        last_offset_ = offset;

        SpeakEvent ev;
        ev.kind = kind;
        ev.audio_offset = offset;
        ev.value = value;
        events_(ev);
    }

    LONG refs_ = 1;
    CaptureAudio* audio_ = nullptr;
    EventSink events_;
    unsigned long window_ = 32000;
    unsigned long last_offset_ = 0;
    bool done_ = false;
    bool started_ = false;
    DWORD done_flags_ = 0;
};

class NotifySink : public ITTSNotifySinkW {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) {
            return E_POINTER;
        }
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ITTSNotifySinkW)) {
            *ppv = static_cast<ITTSNotifySinkW*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override
    {
        const LONG n = InterlockedDecrement(&refs_);
        if (n == 0) {
            delete this;
        }
        return static_cast<ULONG>(n);
    }

    STDMETHODIMP AttribChanged(DWORD) override { return S_OK; }
    STDMETHODIMP AudioStart(QWORD) override { return S_OK; }
    STDMETHODIMP AudioStop(QWORD) override { return S_OK; }
    STDMETHODIMP Visual(QWORD, WCHAR, WCHAR, DWORD, TTSMOUTH*) override
    {
        // This engine reports no TTSFEATURE_VISUAL and never calls this; the
        // method exists so the vtable is complete.
        return S_OK;
    }

private:
    ~NotifySink() = default;
    LONG refs_ = 1;
};

// ---------------------------------------------------------------------------

Engine::Engine() = default;

Engine::~Engine()
{
    unload();
}

bool Engine::load(const std::wstring& engine_dir, const Catalog& catalog)
{
    engine_dir_ = engine_dir;

    // The engine resolves its data files against the current directory in some
    // code paths and against its configured LanguageDir in others, so set both.
    if (!SetCurrentDirectoryW(engine_dir.c_str())) {
        IVX_WARN("engine: could not change to %S: %s", engine_dir.c_str(),
                 win_error(GetLastError()));
    }

    catalog.seed_virtual_registry(narrow(engine_dir));

    std::wstring dll = engine_dir + L"\\Ivx230nt.dll";
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        dll = engine_dir + L"\\Ivx230.dll";
    }
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        IVX_ERROR("engine: Ivx230nt.dll not found in %S", engine_dir.c_str());
        return false;
    }

    // LOAD_WITH_ALTERED_SEARCH_PATH makes the engine's own folder the first
    // place its dependencies are looked for, which is how sx32w.dll and
    // darules.dll resolve without touching PATH.
    module_ = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module_) {
        IVX_ERROR("engine: LoadLibrary(%S) failed: %s", dll.c_str(), win_error(GetLastError()));
        return false;
    }
    IVX_INFO("engine: loaded %S", dll.c_str());

    // Redirect the engine's registry imports BEFORE anything asks it for a
    // class object, which is the first point it reads its configuration.
    if (!VirtualRegistry::instance().install(module_)) {
        wchar_t hive[MAX_PATH];
        wchar_t base[MAX_PATH];
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH) > 0) {
            _snwprintf_s(hive, MAX_PATH, _TRUNCATE, L"%s\\Infovox230SAPI\\engine.hive", base);
            CreateDirectoryW((std::wstring(base) + L"\\Infovox230SAPI").c_str(), nullptr);
            if (!VirtualRegistry::instance().seed_hive(hive)) {
                IVX_ERROR("engine: no way to give the engine its configuration");
                unload();
                return false;
            }
        }
    }

    using DllGetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
    auto get_class_object =
        reinterpret_cast<DllGetClassObjectFn>(GetProcAddress(module_, "DllGetClassObject"));
    if (!get_class_object) {
        IVX_ERROR("engine: the dll does not export DllGetClassObject");
        unload();
        return false;
    }

    IClassFactory* factory = nullptr;
    HRESULT hr = get_class_object(CLSID_InfovoxEngine, IID_IClassFactory,
                                  reinterpret_cast<void**>(&factory));
    if (FAILED(hr) || !factory) {
        IVX_ERROR("engine: DllGetClassObject failed: %s", hr_error(hr));
        unload();
        return false;
    }

    hr = factory->CreateInstance(nullptr, __uuidof(ITTSEnumW), reinterpret_cast<void**>(&enum_));
    factory->Release();
    if (FAILED(hr) || !enum_) {
        IVX_ERROR("engine: the class factory would not make a mode enumerator: %s", hr_error(hr));
        unload();
        return false;
    }
    IVX_INFO("engine: mode enumerator created without the SAPI4 runtime");

    if (!enumerate_modes()) {
        unload();
        return false;
    }

    IVX_INFO("engine: %u modes enumerated after %lu configuration reads, all served from memory",
             static_cast<unsigned>(modes_.size()), registry_calls());
    return true;
}

void Engine::unload()
{
    release_current();
    safe_release(enum_);
    if (module_) {
        VirtualRegistry::instance().uninstall();
        VirtualRegistry::instance().release_hive();
        // The engine is deliberately NOT FreeLibrary'd: it registers window
        // classes and a thread-local state that it does not tear down, and
        // unloading it after that has been seen to fault on exit. The worker
        // process ending releases it.
        module_ = nullptr;
    }
}

bool Engine::enumerate_modes()
{
    modes_.clear();
    if (!enum_) {
        return false;
    }
    enum_->Reset();
    for (;;) {
        TTSMODEINFOW info = {};
        ULONG fetched = 0;
        const HRESULT hr = enum_->Next(1, &info, &fetched);
        if (FAILED(hr)) {
            IVX_ERROR("engine: mode enumeration failed: %s", hr_error(hr));
            break;
        }
        if (fetched == 0) {
            break;
        }
        ModeInfo m;
        m.mode_guid = guid_to_string(info.gModeID);
        m.mode_name = info.szModeName;
        m.product = info.szProductName;
        m.speaker = info.szSpeaker;
        m.langid = info.language.LanguageID;
        m.gender = info.wGender;
        m.age = info.wAge;
        m.features = info.dwFeatures;
        IVX_DEBUG("engine: mode \"%S\" lang=0x%04X gender=%u age=%u features=0x%lX id=%s",
                  m.mode_name.c_str(), m.langid, m.gender, m.age, m.features, m.mode_guid.c_str());
        modes_.push_back(std::move(m));
    }
    if (modes_.empty()) {
        IVX_ERROR("engine: no voices enumerated. The engine loaded but found no usable modes -- "
                  "check that sx32w.dll sits beside Ivx230nt.dll and that the rule files are "
                  "present in %S",
                  engine_dir_.c_str());
        return false;
    }
    return true;
}

void Engine::release_current()
{
    if (central_ && notify_key_) {
        central_->UnRegister(notify_key_);
        notify_key_ = 0;
    }
    safe_release(attrs_);
    safe_release(central_);
    safe_release(buf_sink_);
    safe_release(notify_sink_);
    safe_release(audio_);
    selected_guid_.clear();
}

bool Engine::select(const std::string& mode_guid)
{
    if (!enum_) {
        return false;
    }
    GUID guid;
    if (!string_to_guid(mode_guid, &guid)) {
        IVX_ERROR("engine: \"%s\" is not a mode id", mode_guid.c_str());
        return false;
    }

    // Some engines of this generation allow only one ITTSCentral at a time.
    release_current();

    selected_features_ = 0;
    for (const ModeInfo& m : modes_) {
        if (_stricmp(m.mode_guid.c_str(), mode_guid.c_str()) == 0) {
            selected_features_ = m.features;
            break;
        }
    }

    audio_ = new CaptureAudio();
    buf_sink_ = new BufNotifySink(audio_);

    const HRESULT hr = enum_->Select(guid, &central_, static_cast<IAudio*>(audio_));
    if (FAILED(hr) || !central_) {
        IVX_ERROR("engine: Select(%s) failed: %s", mode_guid.c_str(), hr_error(hr));
        release_current();
        return false;
    }

    notify_sink_ = new NotifySink();
    const HRESULT reg = central_->Register(static_cast<ITTSNotifySinkW*>(notify_sink_),
                                           __uuidof(ITTSNotifySinkW), &notify_key_);
    if (FAILED(reg)) {
        IVX_WARN("engine: Register(notify sink) failed, continuing without it: %s", hr_error(reg));
        notify_key_ = 0;
    }

    if (FAILED(central_->QueryInterface(__uuidof(ITTSAttributesW),
                                        reinterpret_cast<void**>(&attrs_)))) {
        attrs_ = nullptr;
        IVX_WARN("engine: this mode has no attributes interface; rate and pitch will only be "
                 "settable through control tags");
    }

    if (audio_->have_format()) {
        const WAVEFORMATEX& w = audio_->wfx();
        format_.samples_per_sec = w.nSamplesPerSec;
        format_.channels = w.nChannels;
        format_.bits = w.wBitsPerSample;
        format_.avg_bytes_per_sec = w.nAvgBytesPerSec ? w.nAvgBytesPerSec
                                                      : w.nSamplesPerSec * w.nChannels *
                                                            (w.wBitsPerSample / 8);
    }

    query_ranges();
    selected_guid_ = mode_guid;
    IVX_INFO("engine: selected %s (features 0x%lX), rate %d..%d default %d, pitch %d..%d default %d",
             mode_guid.c_str(), selected_features_, rate_min_, rate_max_, rate_default_, pitch_min_,
             pitch_max_, pitch_default_);
    return true;
}

void Engine::query_ranges()
{
    // Sensible values in case the engine refuses to be asked.
    rate_min_ = 15;
    rate_max_ = 499;
    rate_default_ = 150;
    pitch_min_ = 30;
    pitch_max_ = 250;
    pitch_default_ = 100;

    if (!attrs_) {
        return;
    }

    if (selected_features_ & TTSFEATURE_SPEED) {
        DWORD v = 0;
        if (SUCCEEDED(attrs_->SpeedGet(&v))) {
            rate_default_ = static_cast<int>(v);
        }
        if (SUCCEEDED(attrs_->SpeedSet(TTSATTR_MINSPEED)) && SUCCEEDED(attrs_->SpeedGet(&v))) {
            rate_min_ = static_cast<int>(v);
        }
        if (SUCCEEDED(attrs_->SpeedSet(TTSATTR_MAXSPEED)) && SUCCEEDED(attrs_->SpeedGet(&v))) {
            // The engine clamps MAXSPEED to one past its real ceiling.
            rate_max_ = (std::max)(static_cast<int>(v) - 1, rate_min_ + 1);
        }
        attrs_->SpeedSet(static_cast<DWORD>(rate_default_));
    }

    if (selected_features_ & TTSFEATURE_PITCH) {
        WORD p = 0;
        if (SUCCEEDED(attrs_->PitchGet(&p))) {
            pitch_default_ = p;
        }
        if (SUCCEEDED(attrs_->PitchSet(TTSATTR_MINPITCH)) && SUCCEEDED(attrs_->PitchGet(&p))) {
            pitch_min_ = p;
        }
        if (SUCCEEDED(attrs_->PitchSet(TTSATTR_MAXPITCH)) && SUCCEEDED(attrs_->PitchGet(&p))) {
            pitch_max_ = p;
        }
        attrs_->PitchSet(static_cast<WORD>(pitch_default_));
    }

    // Loudness is set per utterance with a \Vol=\ tag, which measurement shows
    // scales the engine's own output exactly (\Vol=0\ gives digital silence).
    // The attribute is left at maximum so the tag has the full range to work in.
    if (selected_features_ & TTSFEATURE_VOLUME) {
        attrs_->VolumeSet(TTSATTR_MAXVOLUME);
    }
}

bool Engine::speak(const std::wstring& tagged, const PcmSink& on_pcm, const EventSink& on_event,
                   const CancelCheck& cancelled, unsigned timeout_ms)
{
    return run_text_data(CHARSET_TEXT, TTSDATAFLAG_TAGGED, tagged, on_pcm, on_event, cancelled,
                         timeout_ms);
}

bool Engine::speak_phonemes(const std::wstring& phonemes, bool ipa, const PcmSink& on_pcm,
                            const CancelCheck& cancelled, unsigned timeout_ms)
{
    return run_text_data(ipa ? CHARSET_IPAPHONETIC : CHARSET_ENGINEPHONETIC, 0, phonemes, on_pcm,
                         nullptr, cancelled, timeout_ms);
}

bool Engine::run_text_data(VOICECHARSET charset, DWORD flags, const std::wstring& text,
                           const PcmSink& on_pcm, const EventSink& on_event,
                           const CancelCheck& cancelled, unsigned timeout_ms)
{
    if (!central_ || !audio_ || !buf_sink_) {
        IVX_ERROR("engine: speak called with no voice selected");
        return false;
    }

    audio_->begin(on_pcm);
    buf_sink_->begin(on_event, format_.avg_bytes_per_sec);

    // The engine keeps whatever a control tag last set, across utterances and
    // across text data calls, so the caller's prologue must state every value
    // it cares about. Nothing here relies on the previous utterance.
    std::wstring buffer = text;
    SDATA data;
    data.pData = buffer.empty() ? nullptr : &buffer[0];
    data.dwSize = static_cast<DWORD>((buffer.size() + 1) * sizeof(wchar_t));

    IVX_DEBUG("engine: TextData charset=%d flags=0x%lX chars=%u", static_cast<int>(charset), flags,
              static_cast<unsigned>(buffer.size()));
    IVX_TRACE("engine: text = %S", buffer.c_str());

    const HRESULT hr = central_->TextData(charset, flags, data,
                                          static_cast<ITTSBufNotifySink*>(buf_sink_),
                                          __uuidof(ITTSBufNotifySink));
    if (FAILED(hr)) {
        IVX_ERROR("engine: TextData failed: %s", hr_error(hr));
        audio_->end();
        buf_sink_->end();
        return false;
    }

    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    bool aborted = false;
    while (!buf_sink_->done()) {
        pump_messages();
        if (audio_->consumer_gone() || (cancelled && cancelled())) {
            aborted = true;
            IVX_DEBUG("engine: cancel requested; resetting audio");
            central_->AudioReset();
            break;
        }
        if (GetTickCount64() > deadline) {
            IVX_WARN("engine: TextData did not finish within %u ms; resetting", timeout_ms);
            central_->AudioReset();
            break;
        }
        audio_->request_more();
        await_engine();
    }
    // AudioReset can leave a completion in flight; drain it so the next
    // utterance does not see a stale callback.
    pump_messages();

    const unsigned long produced = audio_->written();
    audio_->end();
    buf_sink_->end();

    IVX_DEBUG("engine: utterance produced %lu bytes (%s)", produced,
              aborted ? "cancelled" : (buf_sink_->done() ? "complete" : "timed out"));
    return aborted || buf_sink_->done();
}

bool Engine::recycle()
{
    if (selected_guid_.empty()) {
        return false;
    }
    const std::string guid = selected_guid_;
    IVX_WARN("engine: recycling the selection for %s", guid.c_str());
    return select(guid);
}

unsigned long Engine::registry_calls() const
{
    return VirtualRegistry::instance().call_count();
}

// ---------------------------------------------------------------------------

std::wstring format_prologue(int rate_wpm, int pitch_hz, int volume_pct)
{
    const int clamped_volume = (std::max)(0, (std::min)(100, volume_pct));
    const unsigned short level =
        static_cast<unsigned short>((clamped_volume * 0xFFFF + 50) / 100);
    const unsigned long stereo = (static_cast<unsigned long>(level) << 16) | level;

    wchar_t buf[96];
    _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"\\Spd=%d\\\\Pit=%d\\\\Vol=%lu\\", rate_wpm,
                 pitch_hz, stereo);
    return buf;
}

std::wstring escape_text(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size());
    for (wchar_t ch : text) {
        if (ch == L'\\') {
            out += L"\\\\";
        } else {
            out += ch;
        }
    }
    return out;
}

}  // namespace ivx
