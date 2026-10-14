// Win32 / COM utilities: RAII owners for every handle type the app uses, checked-call
// helpers that log failures, safe string formatting and path helpers.
#pragma once

#if !defined(UNICODE) || !defined(_UNICODE)
#error "WallpaperSwitcher uses the Unicode (W) Win32 APIs explicitly; build with UNICODE and _UNICODE defined."
#endif

#include <windows.h>
#include <objbase.h>

#include <string>
#include <utility>

namespace ws {

// ---- generic RAII owner -----------------------------------------------------------------

template <class T, class Traits>
class Unique {
public:
    Unique() = default;
    explicit Unique(T v) : v_(v) {}
    Unique(const Unique&) = delete;
    Unique& operator=(const Unique&) = delete;
    Unique(Unique&& o) noexcept : v_(o.release()) {}
    Unique& operator=(Unique&& o) noexcept {
        if (this != &o) reset(o.release());
        return *this;
    }
    ~Unique() { reset(); }

    T get() const { return v_; }
    explicit operator bool() const { return v_ != Traits::Null(); }
    T release() { T t = v_; v_ = Traits::Null(); return t; }
    void reset(T v = Traits::Null()) {
        if (v_ != Traits::Null()) Traits::Close(v_);
        v_ = v;
    }
    T* put() { reset(); return &v_; }

private:
    T v_ = Traits::Null();
};

struct KernelHandleTraits { static HANDLE Null() { return nullptr; }              static void Close(HANDLE h) { ::CloseHandle(h); } };
struct FindHandleTraits   { static HANDLE Null() { return INVALID_HANDLE_VALUE; } static void Close(HANDLE h) { ::FindClose(h); } };
struct FileHandleTraits   { static HANDLE Null() { return INVALID_HANDLE_VALUE; } static void Close(HANDLE h) { ::CloseHandle(h); } };
struct RegKeyTraits       { static HKEY   Null() { return nullptr; }              static void Close(HKEY k)   { ::RegCloseKey(k); } };
struct BitmapTraits       { static HBITMAP Null() { return nullptr; }             static void Close(HBITMAP b) { ::DeleteObject(b); } };
struct DcTraits           { static HDC    Null() { return nullptr; }              static void Close(HDC dc)   { ::DeleteDC(dc); } };
struct WindowTraits       { static HWND   Null() { return nullptr; }              static void Close(HWND w)   { ::DestroyWindow(w); } };
struct MenuTraits         { static HMENU  Null() { return nullptr; }              static void Close(HMENU m)  { ::DestroyMenu(m); } };

using UniqueHandle = Unique<HANDLE, KernelHandleTraits>;   // CreateMutex, events, ...
using UniqueFile   = Unique<HANDLE, FileHandleTraits>;     // CreateFile
using UniqueFind   = Unique<HANDLE, FindHandleTraits>;     // FindFirstFile
using UniqueHkey   = Unique<HKEY, RegKeyTraits>;
using UniqueBitmap = Unique<HBITMAP, BitmapTraits>;
using UniqueDc     = Unique<HDC, DcTraits>;
using UniqueHwnd   = Unique<HWND, WindowTraits>;           // must be reset on the creating thread
using UniqueMenu   = Unique<HMENU, MenuTraits>;

// ---- COM ----------------------------------------------------------------------------------

template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) { Reset(); p_ = o.p_; o.p_ = nullptr; }
        return *this;
    }
    ~ComPtr() { Reset(); }

    T* Get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    T** Put() { Reset(); return &p_; }          // for out-parameters (releases any held pointer first)
    void Reset() { if (p_) { p_->Release(); p_ = nullptr; } }

private:
    T* p_ = nullptr;
};

// A string allocated by COM (freed with CoTaskMemFree, never free()/delete).
class CoString {
public:
    CoString() = default;
    CoString(const CoString&) = delete;
    CoString& operator=(const CoString&) = delete;
    ~CoString() { Reset(); }
    LPWSTR* Put() { Reset(); return &p_; }
    void Reset() { if (p_) { ::CoTaskMemFree(p_); p_ = nullptr; } }
    bool Empty() const { return p_ == nullptr || p_[0] == L'\0'; }
    std::wstring Str() const { return p_ ? std::wstring(p_) : std::wstring(); }

private:
    LPWSTR p_ = nullptr;
};

// CoInitializeEx / CoUninitialize pair that only uninitializes when initialization succeeded.
class ComApartment {
public:
    explicit ComApartment(DWORD model = COINIT_APARTMENTTHREADED) : hr_(::CoInitializeEx(nullptr, model)) {}
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;
    ~ComApartment() { if (SUCCEEDED(hr_)) ::CoUninitialize(); }
    bool Ok() const { return SUCCEEDED(hr_); }
    HRESULT Result() const { return hr_; }

private:
    HRESULT hr_;
};

// ---- diagnostics ---------------------------------------------------------------------------

// Writes "WallpaperSwitcher: <what> failed: 0x... <system message>" to the debugger
// (view with DebugView or a debugger). `code` must be captured by the caller straight after
// the failing call.
void LogError(const wchar_t* what, DWORD code);

// Win32 BOOL API: logs GetLastError() immediately when the call failed.
inline bool Check(BOOL ok, const wchar_t* what) {
    if (!ok) LogError(what, ::GetLastError());
    return ok != FALSE;
}

// HRESULT API.
inline bool Succeeded(HRESULT hr, const wchar_t* what) {
    if (FAILED(hr)) LogError(what, static_cast<DWORD>(hr));
    return SUCCEEDED(hr);
}

// ---- strings and paths ---------------------------------------------------------------------

// printf-style formatting into a std::wstring through StringCchVPrintfW (bounded, truncates
// at 1023 characters). Use %ls for wide-string arguments.
std::wstring Format(const wchar_t* fmt, ...);

// Full path of the running executable (handles paths longer than MAX_PATH); empty on failure.
std::wstring ModulePath();

// "dir" + "\" + "name", without doubling the separator when dir already ends with one (drive roots).
std::wstring JoinPath(const std::wstring& dir, const std::wstring& name);

// LoadLibrary restricted to System32 (no application-directory / PATH search: prevents DLL planting).
HMODULE LoadSystemLibrary(const wchar_t* name);

}  // namespace ws
