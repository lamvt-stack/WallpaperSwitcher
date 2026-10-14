# Code review: WallpaperSwitcher v0 (single-file build)

Scope: `WallpaperSwitcher.cpp` (1,927 lines) as submitted before the refactor. Line numbers refer to that file.
Every finding below was addressed in the modular refactor unless its status says otherwise.

## 1. Executive Summary Table

| ID | Severity | Module/Function | Vulnerability/Issue Type | Short Description |
|---|---|---|---|---|
| WS-001 | [Blocker] | `WaitForVBlank`, `wWinMain` | Security: DLL planting | `LoadLibraryW(L"dwmapi.dll")` searches the application directory first; no default DLL directory restriction. |
| WS-002 | [Major] | `FindWallpaperHost`, `FadeSession::Begin` | Robustness: repeated blocking IPC | Failed Explorer-layout lookup is retried on every switch, with two 1-second `SendMessageTimeout`s on the UI thread. |
| WS-003 | [Major] | `WndProc`, `FadeProc`, `wWinMain` | Exception safety: callback boundary | No `try/catch` anywhere; C++ exceptions can unwind through OS window-procedure frames. |
| WS-004 | [Major] | `FadeSession::Begin`, `ScanFolder`, `GetMonitors`, `ShowMenu`, `ClassifyImage` | RAII: handle / COM leaks on exceptions | HWND, HBITMAP, HMENU, find handles, COM strings and interfaces are released by hand. |
| WS-005 | [Major] | `ComputePlacement`, `RenderWallpaper`, `MakeSmartCrop` | Security: untrusted-input integer overflow | `std::lround` (32-bit `long` on Windows) results from hostile image sizes are cast to `int` and used as bitmap sizes. |
| WS-006 | [Major] | `Step` call chain | Concurrency: UI-thread blocking I/O | Decoding, PNG encoding, folder probing and the fade loop all run in the window procedure. *(Deferred; mitigated.)* |
| WS-007 | [Minor] | `CropFileName`, `ShowMenu` | Security: legacy string API | `wsprintfW` has no destination size. |
| WS-008 | [Minor] | `SetRunAtStartup` | Error handling: path truncation | `GetModuleFileNameW(MAX_PATH)` result unchecked; a truncated path can be written to `HKCU\...\Run`. |
| WS-009 | [Minor] | `RegReadString`, `RegWrite*`, `SaveConfig` | Error handling: silent persistence failures | Fixed 520-char read buffer, unchecked `RegSetValueExW`, repeated open/close. |
| WS-010 | [Minor] | `wWinMain` | Resource lifecycle | Unchecked `CreateMutexW`/`CoInitializeEx`, unconditional `CoUninitialize`, `GetMessageW` -1 ignored. |
| WS-011 | [Minor] | `wWinMain`, `ApplyTimer`, `AddTrayIcon`, `FadeSession` | Error handling: unchecked returns | `RegisterHotKey`, `SetTimer`, `Shell_NotifyIconW`, `SetLayeredWindowAttributes`, `CreateDirectoryW`... fail silently. |
| WS-012 | [Minor] | `WndProc` (`WM_SETTINGCHANGE`) | Security: unvalidated message pointer | `lParam` is dereferenced as a string. |
| WS-013 | [Minor] | `ShowMenu`, `ChooseFolder`, `Step` | Concurrency: re-entrancy | Modal loops dispatch `WM_TIMER`/`WM_HOTKEY` into `Step()` while menu / dialog state is live. |
| WS-014 | [Minor] | `PickFolder` | API usage: legacy, MAX_PATH-limited | `SHBrowseForFolderW` with a `MAX_PATH` result. |
| WS-015 | [Minor] | `AddTrayIcon`, `WndProc` (`WM_TRAY`) | Accessibility / API usage | Legacy mouse-only tray callback; no keyboard activation. |
| WS-016 | [Minor] | process startup | Hardening: no manifest | DPI awareness set at run time; no `longPathAware`, `supportedOS`, `asInvoker`. |
| WS-017 | [Minor] | `CropCacheDir`, `ScanFolder`, `PruneCropCache` | Path handling | Double separator on drive roots, `MAX_PATH` shell-folder API, unchecked `CreateDirectoryW`, stale `.tmp` files. |
| WS-018 | [Minor] | whole file | Maintainability / testability | 1.9k-line translation unit with ~40 mutable globals; tests had to extract regions with `awk`. |

## 2. Detailed Findings

### WS-001 [Blocker] - DLL planting through an unqualified `LoadLibraryW`
- **Location:** `WaitForVBlank()` (L1277 `LoadLibraryW(L"dwmapi.dll")`); `wWinMain` (no `SetDefaultDllDirectories`).
- **Why It Matters:** A bare DLL name uses the standard search order, which starts with the application directory. `dwmapi.dll` cannot be relied on to be a KnownDLL, and this exe is typically run from a user-writable folder (Downloads, a USB stick). A planted `dwmapi.dll` executes in-process, with the user's rights, the first time a fade runs.
- **Suggested Fix:** Restrict the process default to System32 as the first statement of `wWinMain` and load every optional DLL with `LOAD_LIBRARY_SEARCH_SYSTEM32`. **Status:** fixed (`main.cpp`, `win_util.cpp: LoadSystemLibrary`).
- **Code Remediation:**
```cpp
// Before
HMODULE m = LoadLibraryW(L"dwmapi.dll");

// After - wWinMain, before anything else:
Check(::SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32), L"SetDefaultDllDirectories");
// ...and for each optional DLL:
HMODULE LoadSystemLibrary(const wchar_t* name) {
    HMODULE m = ::LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m) LogError(name, ::GetLastError());
    return m;
}
```

### WS-002 [Major] - Explorer-layout lookup is retried (and blocks) on every switch
- **Location:** `FindWallpaperHost()` (L1246, `SendMessageTimeoutW(..., SMTO_NORMAL, 1000, ...)` called up to twice); `FadeSession::hostTried` (L1408) is a per-session member, so the failure is forgotten after each `Step()`.
- **Why It Matters:** On a machine where the wallpaper WorkerW is not found (new Windows layout, Explorer restarting, a replacement shell), every Next / timer tick re-enumerates windows and sends `0x052C` twice with 1-second timeouts on the UI thread: up to ~2 s of frozen tray icon per switch. `SMTO_NORMAL` also does not abort when Explorer is hung. The return value of `SendMessageTimeoutW` is never checked.
- **Suggested Fix:** Remember the failure process-wide (60 s back-off, cleared on `TaskbarCreated`), use `SMTO_ABORTIFHUNG` and a short timeout, and check the result. **Status:** fixed (`fade.cpp`).
- **Code Remediation:**
```cpp
// Before
SendMessageTimeoutW(progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, &res);   // result ignored
bool hostTried = false;                                                     // per Step()

// After
if (!::SendMessageTimeoutW(progman, 0x052C, 0xD, 0x1, SMTO_NORMAL | SMTO_ABORTIFHUNG, 250, &res))
    LogError(L"SendMessageTimeoutW(Progman 0x052C)", ::GetLastError());
...
static ULONGLONG g_retryAfter = 0;                          // process-wide
if (::GetTickCount64() < g_retryAfter) return false;        // failed recently: don't stall every switch
host_ = FindWallpaperHost();
if (!host_) g_retryAfter = ::GetTickCount64() + 60'000;
```

### WS-003 [Major] - No exception barrier at OS callback boundaries
- **Location:** `WndProc`, `FadeProc`, `EnumFindWallpaperHost` (window / enumeration callbacks), `wWinMain`. The code uses `std::vector`, `std::wstring`, `std::sort`, `std::shuffle` and `std::unordered_map`, and contains no `try`/`catch`.
- **Why It Matters:** A `std::bad_alloc` / `std::length_error` thrown from `Step()` or `ShowMenu()` propagates into user32's dispatch frames. Unwinding through foreign frames is undefined behaviour (in practice the process terminates), leaving a ghost tray icon until the mouse passes over it. For a tray app that runs for weeks, this is the worst possible place to crash.
- **Suggested Fix:** Catch everything at each boundary, log, and fall back to `DefWindowProcW`. **Status:** fixed (`App::WndProcThunk`, `FadeProc`, `wWinMain`).
- **Code Remediation:**
```cpp
// Before
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) { switch (m) { /* ... may throw ... */ } }

// After
LRESULT CALLBACK App::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* self = reinterpret_cast<App*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return ::DefWindowProcW(hwnd, msg, wp, lp);
    try {
        return self->HandleMessage(hwnd, msg, wp, lp);
    } catch (...) {                       // never unwind through OS frames
        LogError(L"unhandled C++ exception in WndProc", ERROR_INTERNAL_ERROR);
        return ::DefWindowProcW(hwnd, msg, wp, lp);
    }
}
```

### WS-004 [Major] - Win32 / COM resources released by hand (leak on exception)
- **Location:** `FadeSession::Begin` (L1429-1490: `CreateWindowExW` + `CreateDIBSection`, then `ov.push_back(o)` can throw); `ScanFolder` (L1058: `FindFirstFileExW` ... `FindClose` around `push_back`); `GetMonitors` (L1124: `CoTaskMemFree(id)` after `out.push_back`); `FadeSession::Begin` (L1449: `std::wstring oldPath = raw;` can throw before `CoTaskMemFree(raw)`); `ShowMenu` (five `HMENU`s created up front, destroyed at the end); `ClassifyImage` (raw `IWICBitmapDecoder*` / `IWICBitmapFrameDecode*`).
- **Why It Matters:** Every one of these leaks a kernel, USER or GDI object when an allocation throws. GDI/USER objects are capped per process (10,000 by default); a leak per failed switch in a long-running process eventually makes bitmap and window creation fail.
- **Suggested Fix:** One RAII owner per handle kind (`Unique<>` with traits, `ComPtr`, `CoString`, an owning `PopupMenu`) and never call `CloseHandle` / `DeleteObject` / `Release` manually. **Status:** fixed (`win_util.h` and all modules).
- **Code Remediation:**
```cpp
// Before
HWND win = CreateWindowExW(...);  HBITMAP bmp = RenderWallpaper(...);
...
ov.push_back(o);                  // may throw -> win and bmp leak

// After
struct Overlay { std::wstring id; UniqueBitmap bitmap; UniqueHwnd hwnd; };   // window is destroyed first
Overlay o;
o.id = m.id;  o.bitmap = std::move(bmp);  o.hwnd = std::move(win);
overlays_.push_back(std::move(o));    // if this throws, the destructors still release both
```

### WS-005 [Major] - Integer overflow / unbounded sizes derived from untrusted image dimensions
- **Location:** `ComputePlacement` (L350-351), `RenderWallpaper` (L1291+), `MakeSmartCrop` (L980-981, L1007-1008), `ClassifyImage` (`static_cast<int>(w)`).
- **Why It Matters:** `std::lround` returns `long`, which is 32 bits on 64-bit Windows (LLP64). For a crafted 1 x 2,000,000,000 PNG in the wallpaper folder, Fill computes a scale of ~3840, so `ih * s` is ~7.7e12; converting that to `long`/`int` is undefined behaviour and yields garbage that is then passed to `IWICBitmapScaler::Initialize`, `CreateDIBSection` and byte-count arithmetic (`UINT cb`). The impact ranges from a failed switch to a huge allocation (DoS), triggered merely by a file being present.
- **Suggested Fix:** Saturating conversion (`llround` + clamp), a hard render cap, and rejecting dimensions above `INT_MAX`; cover hostile sizes with unit tests. **Status:** fixed (`core/numeric.h`, `core/fade_math.h`, `wic.cpp`, tests).
- **Code Remediation:**
```cpp
// Before
p.dw = std::max(1, static_cast<int>(std::lround(iw * s)));      // long is 32-bit on Windows: UB when out of range

// After
inline int RoundToInt(double v) {
    if (!(v == v)) return 0;                                    // NaN
    if (v >= static_cast<double>(INT_MAX)) return INT_MAX;
    if (v <= static_cast<double>(INT_MIN)) return INT_MIN;
    return static_cast<int>(std::llround(v));
}
p.dw = ClampInt(RoundToInt(iw * s), 1, kMaxRenderDim);          // kMaxRenderDim = 32768
```

### WS-006 [Major] - Heavy synchronous I/O and a blocking animation loop on the UI thread
- **Location:** `Step()` -> `MakeSmartCrop` (full decode + PNG encode), `ClassifyFile` / `PrefetchRandom` (also called from `ShowMenu` before the menu is shown), `FadeSession::Run` (~500 ms loop), `FindWallpaperHost` (cross-process `SendMessage`).
- **Why It Matters:** The thread that owns the tray window freezes for the duration: seconds on a spinning disk or network share or with a 50 MP photo. The app uses no locks and no secondary threads, so there is no deadlock or data-race exposure; the risk is latency and the re-entrancy covered by WS-013. Windows marks a window unresponsive after ~5 s without pumping messages.
- **Suggested Fix:** Move classification and cropping to a worker thread (MTA COM apartment), keep `Playlist` owned by the UI thread, hand results back with `PostMessageW` and discard stale results with a generation counter; run the fade as a timer-driven animation instead of a blocking loop. **Status:** deferred to the roadmap. Mitigated now by bounded probing (16 reads per display), a re-entrancy guard, the fade back-off, and suppressing the timer while a menu / dialog is open.
- **Code Remediation:**
```cpp
// Planned shape (sketch): ownership of the result travels in LPARAM through std::unique_ptr, not naked new/delete.
struct CropResult { std::uint64_t generation; std::wstring source, png; };

// worker thread
auto r = std::make_unique<CropResult>(CropResult{ job.generation, job.file.path, MakeSmartCrop(job.file, job.tw, job.th) });
if (::PostMessageW(hwnd, kCropDone, 0, reinterpret_cast<LPARAM>(r.get()))) r.release();   // posted: the UI thread owns it now

// UI thread (WM_APP+n)
std::unique_ptr<CropResult> r(reinterpret_cast<CropResult*>(lp));
if (r->generation != generation_) return;      // the folder / mode changed meanwhile
```

### WS-007 [Minor] - `wsprintfW` (no destination size)
- **Location:** `CropFileName` (L867), `ShowMenu` (L1730 and L1744, plus the monitor-count label).
- **Why It Matters:** `wsprintfW` cannot be told the buffer size. Today the 48 / 96 / 128-character stack buffers are sized by hand for constant formats; the first format or argument change that outgrows one is a stack overflow. It also lacks 64-bit and floating-point support.
- **Suggested Fix:** A bounded `Format()` built on `StringCchVPrintfW`; use `%ls` for wide strings (correct under both MSVC and MinGW stdio semantics). **Status:** fixed (`win_util.cpp`).
- **Code Remediation:**
```cpp
// Before
wchar_t label[96];
wsprintfW(label, L"Next on monitor %u (%d x %d, %s)", k + 1, w, h, orient);

// After
const std::wstring label = Format(L"Next on monitor %u (%d x %d, %ls)", k + 1, w, h, orient);   // truncates, never overflows
```

### WS-008 [Minor] - `GetModuleFileNameW` truncation
- **Location:** `SetRunAtStartup` / `IsRunAtStartup` (L567-574: `GetModuleFileNameW(nullptr, exe, MAX_PATH)`, result ignored).
- **Why It Matters:** If the path is `MAX_PATH` or longer it is silently truncated (and not NUL-terminated on old Windows); the truncated string is then written to `HKCU\...\Run`, so "Run at startup" silently never works. `IsRunAtStartup` also reported true for *any* existing value, even one pointing at a moved executable.
- **Suggested Fix:** Grow-and-retry helper; compare the stored command with the current executable. **Status:** fixed (`win_util.cpp: ModulePath`, `config.cpp`).
- **Code Remediation:**
```cpp
// Before
wchar_t exe[MAX_PATH];  GetModuleFileNameW(nullptr, exe, MAX_PATH);

// After
std::wstring ModulePath() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = ::GetModuleFileNameW(nullptr, &buf[0], static_cast<DWORD>(buf.size()));
        if (n == 0) { LogError(L"GetModuleFileNameW", ::GetLastError()); return {}; }
        if (n < buf.size()) { buf.resize(n); return buf; }      // n == size means "truncated"
        if (buf.size() >= 32768) return {};
        buf.resize(buf.size() * 2);
    }
}
```

### WS-009 [Minor] - Registry layer: truncation and unchecked writes
- **Location:** `RegReadString` (L507-513: `buf[MAX_PATH * 2]`), `RegWriteString` / `RegWriteDword` (L522-540), `SaveConfig` (L554).
- **Why It Matters:** A value longer than 520 characters makes `RegGetValueW` return `ERROR_MORE_DATA`, which is reported as "no value": a long folder path is silently dropped and the app asks for the folder on every start. `RegCreateKeyExW` / `RegSetValueExW` results are discarded, so a failed save is invisible, and every save opens and closes the key up to eight times.
- **Suggested Fix:** Two-call read with a size sanity limit; one RAII write key per save; check and log every status. **Status:** fixed (`config.cpp`).
- **Code Remediation:**
```cpp
// Before
wchar_t buf[MAX_PATH * 2]; DWORD size = sizeof(buf);
if (RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS) return buf;
return L"";

// After
DWORD bytes = 0;
if (::RegGetValueW(root, sub, name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS || bytes > 64 * 1024) return false;
std::wstring buf(bytes / sizeof(wchar_t), L'\0');
if (::RegGetValueW(root, sub, name, RRF_RT_REG_SZ, nullptr, &buf[0], &bytes) != ERROR_SUCCESS) return false;
buf.resize(wcslen(buf.c_str()));
```

### WS-010 [Minor] - Process lifecycle: mutex, COM apartment, message loop
- **Location:** `wWinMain` (L1875-1879, loop at the end).
- **Why It Matters:** `CreateMutexW` returning `NULL` is not handled; the mutex handle is not closed on the early `return 0` / `return 1` paths; `CoInitializeEx`'s HRESULT is ignored while `CoUninitialize()` runs unconditionally (unbalanced when initialization failed); `GetMessageW` returning `-1` leaves the loop and then `m.wParam` is read uninitialised.
- **Suggested Fix:** RAII for the mutex and the apartment; read `GetLastError()` straight after `CreateMutexW`; loop on `!= 0` and handle `-1`. **Status:** fixed (`main.cpp`, `App::Run`).
- **Code Remediation:**
```cpp
// Before
HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\WallpaperSwitcher_SingleInstance");
if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;     // handle leaked, NULL not checked
CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);        // ... CoUninitialize() later, always

// After
ws::UniqueHandle mutex(::CreateMutexW(nullptr, FALSE, L"Local\\WallpaperSwitcher_SingleInstance"));
const DWORD mutexError = ::GetLastError();                // immediately after the call
if (!mutex) { ws::LogError(L"CreateMutexW", mutexError); return 1; }
if (mutexError == ERROR_ALREADY_EXISTS) return 0;
ws::ComApartment com;                                     // CoUninitialize only if CoInitializeEx succeeded
if (!com.Ok()) { ws::LogError(L"CoInitializeEx", static_cast<DWORD>(com.Result())); return 1; }
```

### WS-011 [Minor] - Unchecked Win32 return values, no diagnostics
- **Location:** `RegisterHotKey` (L1905-1906), `SetTimer` (L1615), `Shell_NotifyIconW(NIM_ADD)` (L1692), `SetLayeredWindowAttributes`, `SetWindowPos`, `CreateDirectoryW`, `RegisterClassW`, `TrackPopupMenu`, `AppendMenuW`.
- **Why It Matters:** A hotkey owned by another app, a failed auto-change timer or a failed tray registration (Explorer not up yet at logon) all look like "the app does nothing", with no trace to debug.
- **Suggested Fix:** `Check(BOOL, what)` / `Succeeded(HRESULT, what)` helpers that capture `GetLastError()` immediately and write to the debugger; surface the user-visible cases (hotkeys) in the tooltip. **Status:** fixed (`win_util.h`, all modules).
- **Code Remediation:**
```cpp
// Before
RegisterHotKey(g_hwnd, HK_NEXT, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_RIGHT);

// After
inline bool Check(BOOL ok, const wchar_t* what) { if (!ok) LogError(what, ::GetLastError()); return ok != FALSE; }
hotkeysOk_ = Check(::RegisterHotKey(hwnd_, kHotkeyNext, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_RIGHT), L"RegisterHotKey(next)");
if (!hotkeysOk_) SetTooltip(L"Wallpaper Switcher - hotkeys unavailable (already in use)");
```

### WS-012 [Minor] - `lParam` dereferenced without validation in `WM_SETTINGCHANGE`
- **Location:** `WndProc`, `case WM_SETTINGCHANGE` (L1853-1855): `wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet")`.
- **Why It Matters:** For `WM_SETTINGCHANGE` `lParam` is a string pointer only for some senders (0 for many `SPI_*` broadcasts). Any process in the session can post the message with an arbitrary `lParam` to the app's top-level window; dereferencing it is an access violation (WS-003 turns that into a crash).
- **Suggested Fix:** Do not dereference. Re-resolve the icon on any setting change; it is one registry read and only touches the tray when the result changed. **Status:** fixed (`App::ApplyIcon(false)`).
- **Code Remediation:**
```cpp
// Before
if (g_cfg.iconChoice == IC_AUTO && lp && wcscmp(reinterpret_cast<const wchar_t*>(lp), L"ImmersiveColorSet") == 0) ApplyIcon();

// After
case WM_SETTINGCHANGE:                       // lParam is not dereferenced
    if (cfg_.icon == IconChoice::Auto) ApplyIcon(false);   // no-op unless the resolved icon changed
    return 0;
```

### WS-013 [Minor] - Re-entrancy through modal message loops
- **Location:** `ShowMenu` (`TrackPopupMenu`), `ChooseFolder` (`SHBrowseForFolderW`), `Step`.
- **Why It Matters:** Both calls run nested message loops that keep dispatching `WM_TIMER` and `WM_HOTKEY`. `Step()` then reshuffles `g_files` / `g_perm` / `g_cursor` while the menu was built from older state, and the wallpaper changes underneath an open folder dialog. There is no lock to misuse, but the invariants of the global state are not protected against nested entry.
- **Suggested Fix:** A scoped re-entrancy guard on `Step()` and a `modal_` flag that makes the timer skip while a menu or dialog is open. **Status:** fixed (`App::Step`, `App::ShowMenu`, `App::ChooseFolder`).
- **Code Remediation:**
```cpp
void App::Step(int dir, int only) {
    if (stepping_) return;               // nested entry from a modal loop
    ScopedFlag guard(stepping_);
    try { StepImpl(dir, only); } catch (const std::exception&) { LogError(L"exception while changing the wallpaper", ERROR_INTERNAL_ERROR); }
}
case WM_TIMER: if (wp == kTimerId && !modal_) Step(+1); return 0;
```

### WS-014 [Minor] - Legacy, `MAX_PATH`-limited folder picker
- **Location:** `PickFolder` (L1625: `SHBrowseForFolderW` + `BIF_NEWDIALOGSTYLE`, result in `wchar_t[MAX_PATH]`).
- **Why It Matters:** Deep folders cannot be chosen, the dialog cannot start in the current folder, and the old tree-view UI is not the modern Explorer picker.
- **Suggested Fix:** `IFileOpenDialog` with `FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM`, result as `CoString`. **Status:** fixed (`App::PickFolder`).
- **Code Remediation:**
```cpp
ComPtr<IFileOpenDialog> dlg;
Succeeded(::CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dlg.Put())), L"CoCreateInstance(FileOpenDialog)");
dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
...
ComPtr<IShellItem> item;  CoString path;                       // freed with CoTaskMemFree
item->GetDisplayName(SIGDN_FILESYSPATH, path.Put());
```

### WS-015 [Minor] - Mouse-only tray callback
- **Location:** `AddTrayIcon` (L1692), `WndProc` `WM_TRAY`: `if (lp == WM_LBUTTONUP) ... else if (lp == WM_RBUTTONUP)`.
- **Why It Matters:** Keyboard-only and assistive-technology users cannot switch wallpaper or open the menu; tooltips are unreliable on newer shells with the legacy protocol.
- **Suggested Fix:** `NOTIFYICON_VERSION_4` (`NIM_SETVERSION`), handle `NIN_SELECT` / `NIN_KEYSELECT` / `WM_CONTEXTMENU`, set `NIF_SHOWTIP`; keep the legacy path as a fallback. **Status:** fixed (`App::AddTrayIcon`, `App::OnTrayEvent`).
- **Code Remediation:**
```cpp
nid_.uVersion = NOTIFYICON_VERSION_4;
trayV4_ = ::Shell_NotifyIconW(NIM_SETVERSION, &nid_) != FALSE;
...
switch (LOWORD(lp)) {
case NIN_SELECT: case NIN_KEYSELECT: Step(+1); break;
case WM_CONTEXTMENU: ShowMenu({ GET_X_LPARAM(wp), GET_Y_LPARAM(wp) }); break;
}
```

### WS-016 [Minor] - No application manifest
- **Location:** process start-up: `SetProcessDPIAware()` (L1878); no `.manifest` / `.rc`.
- **Why It Matters:** DPI awareness set at run time is applied after the loader and CRT have run; crop sizes depend on real pixels. Without the manifest the app also lacks `longPathAware`, `supportedOS` GUIDs (compat shims may apply) and an explicit `asInvoker` request.
- **Suggested Fix:** Embed a manifest through the resource script (and disable linker manifest generation under MSVC). **Status:** fixed (`resources/WallpaperSwitcher.manifest`, `.rc`, `CMakeLists.txt`).
- **Code Remediation:**
```xml
<requestedExecutionLevel level="asInvoker" uiAccess="false"/>
<dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true</dpiAware>
<longPathAware xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">true</longPathAware>
```
```rc
CREATEPROCESS_MANIFEST_RESOURCE_ID RT_MANIFEST "WallpaperSwitcher.manifest"
```

### WS-017 [Minor] - Path handling in the listing and crop cache
- **Location:** `ScanFolder` (`g_cfg.folder + L"\\" + fd.cFileName`), `CropCacheDir` (`SHGetFolderPathW`, `CreateDirectoryW` unchecked), `PruneCropCache` (matches `crop_*.png` only).
- **Why It Matters:** Drive roots (`C:\`) produce `C:\\file`, which also changes the cache key for the same file; `SHGetFolderPathW` is `MAX_PATH`-limited; a failed directory creation silently disables cropping; a crash between encode and rename leaves `*.png.tmp` files that are never removed.
- **Suggested Fix:** `JoinPath`, `SHGetKnownFolderPath` + `CoString`, checked directory creation, prune `crop_*.tmp`. **Status:** fixed (`win_util.cpp`, `smart_crop.cpp`, `image_library.cpp`).
- **Code Remediation:**
```cpp
std::wstring JoinPath(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    const wchar_t last = dir.back();
    return (last == L'\\' || last == L'/') ? dir + name : dir + L"\\" + name;
}
```

### WS-018 [Minor] - One translation unit, ~40 mutable globals, no test seam
- **Location:** whole file (`g_cfg`, `g_files`, `g_perm`, `g_cursor`, `g_wic`, `g_nid`, ...).
- **Why It Matters:** Any function can mutate any state; nothing can be unit-tested without Windows, which is why the selection logic had to be tested by cutting regions out of the `.cpp` with `awk`. Re-entrancy (WS-013) is harder to reason about with global state.
- **Suggested Fix:** Modules with explicit ownership (`App`, `ImageLibrary`, `Playlist`, `DesktopWallpaper`, `FadeSession`), a portable `src/core/` tested through its real headers, CMake + CTest, CI. **Status:** done; 3 test executables, 68 checks, run on Linux and Windows in CI.
- **Code Remediation:**
```cpp
// Before: free functions over globals
static void Step(int dir, int only);   // reads/writes g_cfg, g_files, g_perm, g_cursor, g_haveShown, ...

// After: state has an owner, logic has a seam
class Playlist {                       // src/core: no Windows types
public:
    template <class ApplyFn> RunResult Run(const std::vector<Slot>& slots, int dir, int only, ApplyFn&& apply);
};
const RunResult r = library_.List().Run(slots, dir, only, apply);   // App owns the library, tests inject fakes
```

## 3. Strong Patterns

1. **A platform-independent core with injected effects.** `Playlist::Run` takes the "apply to the display" action as a functor and image classification as a callback, so the hardest logic (orientation matching, no two displays sharing an image, bounded lazy probing, resumable searches, `Next`/`Previous` symmetry) is plain C++ that runs under CTest on any OS, including the exact regression for a duplicate wallpaper on a portrait monitor.
2. **Crash-safe, bounded cache writes.** Smart-crop PNGs are written to a `.tmp` file and published with `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`, keyed by an FNV-1a hash of path + size + timestamp + target size + algorithm version (so stale crops can never be served), and pruned to the 64 most recently used.
3. **Defensive degradation and cheap probing.** Classification reads only image headers and EXIF through WIC, COM objects are scoped to a single operation (so monitor hot-plug is always reflected), and every optional capability has a fallback: no `IDesktopWallpaper` falls back to `SystemParametersInfo`, no WIC means no orientation filtering, and no wallpaper-layer window means an instant switch.

## Verification status

- Built: MinGW-w64 (CMake cross build, plus `-Wall -Wextra -Wshadow -Wold-style-cast` clean); cppcheck: no warnings on the portable core.
- Run: 3 portable test executables on Linux. Checked in the produced binary: embedded manifest and icon groups, ASLR/DEP/high-entropy flags, expected CLSIDs/IIDs, system-only imports.
- Not run: the app on Windows (tray, menu, wallpaper changes, cross-fade, multi-monitor) and the MSVC build (CI covers it).
