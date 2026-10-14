#include "win_util.h"

#include <strsafe.h>

#include <cstdarg>

namespace ws {

void LogError(const wchar_t* what, DWORD code) {
    wchar_t sys[256] = L"";
    ::FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, sys,
                     static_cast<DWORD>(_countof(sys)), nullptr);
    // Trim the trailing CR/LF that FormatMessage appends.
    for (size_t n = wcslen(sys); n > 0 && (sys[n - 1] == L'\r' || sys[n - 1] == L'\n' || sys[n - 1] == L' '); --n)
        sys[n - 1] = L'\0';
    wchar_t line[512];
    if (SUCCEEDED(::StringCchPrintfW(line, _countof(line), L"WallpaperSwitcher: %ls failed: 0x%08lX %ls\n", what,
                                     static_cast<unsigned long>(code), sys)))
        ::OutputDebugStringW(line);
}

std::wstring Format(const wchar_t* fmt, ...) {
    wchar_t buf[1024];
    va_list args;
    va_start(args, fmt);
    const HRESULT hr = ::StringCchVPrintfW(buf, _countof(buf), fmt, args);
    va_end(args);
    (void)hr;                       // STRSAFE_E_INSUFFICIENT_BUFFER still leaves a terminated, truncated string
    buf[_countof(buf) - 1] = L'\0';
    return buf;
}

std::wstring ModulePath() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = ::GetModuleFileNameW(nullptr, &buf[0], static_cast<DWORD>(buf.size()));
        if (n == 0) {
            LogError(L"GetModuleFileNameW", ::GetLastError());
            return std::wstring();
        }
        if (n < buf.size()) {       // n == size means the path was truncated
            buf.resize(n);
            return buf;
        }
        if (buf.size() >= 32768) return std::wstring();
        buf.resize(buf.size() * 2);
    }
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    const wchar_t last = dir.back();
    if (last == L'\\' || last == L'/') return dir + name;
    return dir + L"\\" + name;
}

HMODULE LoadSystemLibrary(const wchar_t* name) {
    HMODULE m = ::LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m) LogError(name, ::GetLastError());
    return m;
}

}  // namespace ws
