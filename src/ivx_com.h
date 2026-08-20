#pragma once

// Just enough COM to be an in-process server: a reference-counted object
// template, a class factory, and the registration a SAPI5 engine needs.
//
// Written out rather than pulled from ATL so the engine dll has no dependency
// beyond the C runtime it is statically linked against -- one less thing that
// can be missing on a user's machine when a screen reader tries to talk.

#include <windows.h>
// IUnknown, IClassFactory, CoTaskMemAlloc, StringFromGUID2. WIN32_LEAN_AND_MEAN
// keeps windows.h from pulling OLE in, so ask for it directly.
#include <objbase.h>

#include <atomic>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace ivx {
namespace com {

// Live object count, so DllCanUnloadNow can answer honestly.
class ObjectCount {
public:
    static void up() noexcept { count().fetch_add(1, std::memory_order_relaxed); }
    static void down() noexcept { count().fetch_sub(1, std::memory_order_acq_rel); }
    static bool zero() noexcept { return count().load(std::memory_order_acquire) == 0; }

private:
    static std::atomic<long>& count() noexcept
    {
        static std::atomic<long> n{0};
        return n;
    }
};

template <class I, class O>
inline void* as_primary(O* self, REFIID riid) noexcept
{
    if (riid == __uuidof(IUnknown)) {
        return static_cast<IUnknown*>(static_cast<I*>(self));
    }
    if (riid == __uuidof(I)) {
        return static_cast<I*>(self);
    }
    return nullptr;
}

template <class I, class O>
inline void* as(O* self, REFIID riid) noexcept
{
    return riid == __uuidof(I) ? static_cast<I*>(self) : nullptr;
}

// T supplies `void* interface_for(REFIID)`; everything else comes from here.
template <class T>
class Object final : public T {
public:
    template <typename... Args>
    explicit Object(Args&&... args) : T(std::forward<Args>(args)...)
    {
        ObjectCount::up();
    }

    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;

    STDMETHODIMP_(ULONG) AddRef() noexcept override
    {
        return static_cast<ULONG>(refs_.fetch_add(1, std::memory_order_relaxed) + 1);
    }

    STDMETHODIMP_(ULONG) Release() noexcept override
    {
        const long n = refs_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (n == 0) {
            delete this;
        }
        return static_cast<ULONG>(n);
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) noexcept override
    {
        if (!ppv) {
            return E_POINTER;
        }
        void* p = this->interface_for(riid);
        *ppv = p;
        if (!p) {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

private:
    ~Object() { ObjectCount::down(); }

    std::atomic<long> refs_{1};
};

// Owns one reference and hands it out, so a raw out-parameter is never left
// dangling on an exception path.
template <class T>
class Ref {
public:
    template <typename... Args>
    static Ref make(Args&&... args)
    {
        Ref r;
        r.p_ = new Object<T>(std::forward<Args>(args)...);
        return r;
    }

    Ref() = default;
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    Ref(Ref&& other) noexcept : p_(other.p_) { other.p_ = nullptr; }
    Ref& operator=(Ref&& other) noexcept
    {
        if (this != &other) {
            reset();
            p_ = other.p_;
            other.p_ = nullptr;
        }
        return *this;
    }
    ~Ref() { reset(); }

    Object<T>* operator->() const noexcept { return p_; }
    Object<T>* get() const noexcept { return p_; }

    // Transfers this reference to the caller.
    template <class I>
    HRESULT detach_as(I** out) noexcept
    {
        if (!out) {
            return E_POINTER;
        }
        HRESULT hr = p_->QueryInterface(__uuidof(I), reinterpret_cast<void**>(out));
        reset();
        return hr;
    }

    void reset() noexcept
    {
        if (p_) {
            p_->Release();
            p_ = nullptr;
        }
    }

private:
    Object<T>* p_ = nullptr;
};

template <class T>
class ClassFactory : public IClassFactory {
public:
    void* interface_for(REFIID riid) noexcept { return as_primary<IClassFactory>(this, riid); }

    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) noexcept override
    {
        if (outer) {
            return CLASS_E_NOAGGREGATION;
        }
        if (!ppv) {
            return E_POINTER;
        }
        *ppv = nullptr;
        try {
            // make() starts at one reference, which is ours; QueryInterface adds
            // the caller's, and the destructor drops ours.
            Ref<T> obj = Ref<T>::make();
            return obj->QueryInterface(riid, ppv);
        } catch (const std::bad_alloc&) {
            return E_OUTOFMEMORY;
        } catch (...) {
            return E_UNEXPECTED;
        }
    }

    STDMETHODIMP LockServer(BOOL lock) noexcept override
    {
        if (lock) {
            ObjectCount::up();
        } else {
            ObjectCount::down();
        }
        return S_OK;
    }
};

inline std::wstring clsid_string(REFCLSID clsid)
{
    wchar_t buf[64] = L"";
    StringFromGUID2(clsid, buf, 64);
    return buf;
}

inline wchar_t* task_strdup(const std::wstring& s)
{
    const size_t bytes = (s.size() + 1) * sizeof(wchar_t);
    auto* p = static_cast<wchar_t*>(CoTaskMemAlloc(bytes));
    if (!p) {
        throw std::bad_alloc();
    }
    memcpy(p, s.c_str(), bytes);
    return p;
}

// --- registration -----------------------------------------------------------

// Registry writes here are the ones SAPI5 itself requires: a COM server has to
// be findable by class id. They say nothing about the Infovox engine, which
// reads no registry at all.
LSTATUS set_value(HKEY root, const std::wstring& path, const wchar_t* name,
                  const std::wstring& value);
LSTATUS delete_tree(HKEY root, const std::wstring& path);

// `root` is HKEY_LOCAL_MACHINE for a machine-wide install, or HKEY_CURRENT_USER
// for a per-user one. Both work: COM and SAPI5 read Software\Classes and the
// speech categories out of HKCU as well as HKLM, so an installation without
// administrator rights is still a working installation, just for one user.
bool register_class(HKEY root, REFCLSID clsid, const std::wstring& dll_path,
                    const wchar_t* friendly_name);
bool unregister_class(HKEY root, REFCLSID clsid);

}  // namespace com
}  // namespace ivx
