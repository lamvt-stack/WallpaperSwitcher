#include "app.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <strsafe.h>
#include <windowsx.h>

#include <algorithm>
#include <exception>

#include "core/numeric.h"
#include "desktop.h"
#include "fade.h"
#include "smart_crop.h"
#include "../resources/resource.h"

namespace ws {
namespace {

constexpr wchar_t kAppName[]   = L"WallpaperSwitcher";
constexpr wchar_t kClassName[] = L"WallpaperSwitcherHiddenWnd";
constexpr UINT    kTrayMessage = WM_APP + 1;
constexpr UINT_PTR kTimerId    = 1;
constexpr int     kHotkeyNext  = 1;
constexpr int     kHotkeyPrev  = 2;
constexpr int     kSampleSize  = 3;      // random images probed at startup and each time the menu opens

enum : UINT {
    kIdNext = 1001, kIdPrev, kIdFolder, kIdShuffle, kIdStartup, kIdRescan, kIdExit, kIdSmartCrop, kIdFade,
    kIdIntervalBase = 1100,
    kIdStyleBase    = 1200,
    kIdMonModeBase  = 1300,   // +0 same image, +1 different image per monitor
    kIdMonNextBase  = 1400,   // +monitor index
    kIdIconBase     = 1500,   // +Dark / Light / Auto
};

struct Interval { const wchar_t* label; UINT minutes; };
const Interval kIntervals[] = {
    { L"Manual only", 0 }, { L"1 minute", 1 },  { L"5 minutes", 5 },
    { L"15 minutes", 15 }, { L"30 minutes", 30 }, { L"1 hour", 60 }, { L"3 hours", 180 },
};
static_assert(sizeof(kIntervals) / sizeof(kIntervals[0]) == static_cast<size_t>(kIntervalCount), "interval table / count mismatch");

const wchar_t* const kIconLabels[] = { L"Dark", L"Light", L"Match Windows theme" };

// Sets a flag for the lifetime of a scope.
class ScopedFlag {
public:
    explicit ScopedFlag(bool& f) : f_(f), prev_(f) { f_ = true; }
    ScopedFlag(const ScopedFlag&) = delete;
    ScopedFlag& operator=(const ScopedFlag&) = delete;
    ~ScopedFlag() { f_ = prev_; }
private:
    bool& f_;
    bool prev_;
};

// Owning popup menu: every path (including exceptions) destroys it; sub-menus are handed over
// to their parent only once AppendMenu has succeeded.
class PopupMenu {
public:
    PopupMenu() : menu_(::CreatePopupMenu()) {
        if (!menu_) LogError(L"CreatePopupMenu", ::GetLastError());
    }
    explicit operator bool() const { return static_cast<bool>(menu_); }
    HMENU get() const { return menu_.get(); }

    void Item(UINT id, const std::wstring& label, bool checked = false, bool enabled = true) {
        const UINT flags = MF_STRING | (checked ? MF_CHECKED : 0) | (enabled ? 0 : MF_GRAYED);
        Check(::AppendMenuW(menu_.get(), flags, id, label.c_str()), L"AppendMenuW(item)");
    }
    void Separator() { Check(::AppendMenuW(menu_.get(), MF_SEPARATOR, 0, nullptr), L"AppendMenuW(separator)"); }
    void Submenu(const std::wstring& label, PopupMenu&& sub) {
        if (Check(::AppendMenuW(menu_.get(), MF_POPUP, reinterpret_cast<UINT_PTR>(sub.menu_.get()), label.c_str()),
                  L"AppendMenuW(submenu)"))
            sub.menu_.release();          // now owned (and destroyed) by the parent menu
    }

private:
    UniqueMenu menu_;
};

}  // namespace

App::App() : library_() {}

App::~App() {
    if (hwnd_) {
        ::SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        ::DestroyWindow(hwnd_);
    }
}

bool App::Create(HINSTANCE instance) {
    instance_ = instance;
    cfg_ = LoadConfig();
    library_.List().SetSmartCrop(cfg_.smartCrop);

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WndProcThunk;
    wc.hInstance     = instance;
    wc.lpszClassName = kClassName;
    if (!::RegisterClassW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        LogError(L"RegisterClassW", ::GetLastError());
        return false;
    }
    if (!RegisterFadeWindowClass(instance)) LogError(L"fade overlay class unavailable; fading disabled", ERROR_INVALID_FUNCTION);

    // Hidden top-level window (not message-only, so it still receives "TaskbarCreated",
    // WM_DISPLAYCHANGE and WM_SETTINGCHANGE broadcasts).
    HWND h = ::CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, kAppName, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (!h) {
        LogError(L"CreateWindowExW(main)", ::GetLastError());
        return false;
    }

    taskbarCreated_ = ::RegisterWindowMessageW(L"TaskbarCreated");
    if (taskbarCreated_ == 0) LogError(L"RegisterWindowMessageW(TaskbarCreated)", ::GetLastError());

    AddTrayIcon();
    ApplyIcon(true);
    hotkeysOk_ = Check(::RegisterHotKey(hwnd_, kHotkeyNext, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_RIGHT), L"RegisterHotKey(next)");
    hotkeysOk_ = Check(::RegisterHotKey(hwnd_, kHotkeyPrev, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_LEFT), L"RegisterHotKey(prev)") && hotkeysOk_;
    if (!hotkeysOk_) SetTooltip(L"Wallpaper Switcher - hotkeys unavailable (already in use)");
    return true;
}

void App::Start() {
    if (cfg_.folder.empty()) {
        ChooseFolder();                      // first run: pick a folder, then switch
    } else {
        Rescan();                            // names only - fast even for huge folders
        Step(+1);                            // switch right away, reading only what it needs
    }
    library_.List().PrefetchRandom(kSampleSize);   // plus a few random images to start filling the cache
    ApplyTimer();
}

int App::Run() {
    MSG msg = {};
    BOOL r;
    while ((r = ::GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (r == -1) {
            LogError(L"GetMessageW", ::GetLastError());
            return 1;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

// ---- window procedure ----------------------------------------------------------------------

LRESULT CALLBACK App::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<App*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return ::DefWindowProcW(hwnd, msg, wp, lp);
    try {
        return self->HandleMessage(hwnd, msg, wp, lp);
    } catch (...) {
        // Never let a C++ exception unwind through the OS window-procedure frames.
        LogError(L"unhandled C++ exception in WndProc", ERROR_INTERNAL_ERROR);
        return ::DefWindowProcW(hwnd, msg, wp, lp);
    }
}

LRESULT App::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (taskbarCreated_ != 0 && msg == taskbarCreated_) {          // Explorer (re)started
        ResetFadeBackoff();
        AddTrayIcon();
        return 0;
    }
    switch (msg) {
    case kTrayMessage:
        OnTrayEvent(wp, lp);
        return 0;
    case WM_COMMAND:
        OnCommand(LOWORD(wp));
        return 0;
    case WM_TIMER:
        if (wp == kTimerId && !modal_) Step(+1);
        return 0;
    case WM_HOTKEY:
        if (wp == static_cast<WPARAM>(kHotkeyNext)) Step(+1);
        else if (wp == static_cast<WPARAM>(kHotkeyPrev)) Step(-1);
        return 0;
    case WM_SETTINGCHANGE:                 // may be the Windows light/dark switch; lParam is not dereferenced
        if (cfg_.icon == IconChoice::Auto) ApplyIcon(false);
        return 0;
    case WM_DISPLAYCHANGE:                 // monitor plugged / unplugged / rotated / rearranged
        if (haveShown_) Step(0);
        return 0;
    case WM_DESTROY:
        ::KillTimer(hwnd, kTimerId);
        ::UnregisterHotKey(hwnd, kHotkeyNext);
        ::UnregisterHotKey(hwnd, kHotkeyPrev);
        if (nid_.cbSize) ::Shell_NotifyIconW(NIM_DELETE, &nid_);
        ::PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY:
        hwnd_ = nullptr;
        break;
    default:
        break;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- tray icon -----------------------------------------------------------------------------

int App::IconResourceId() const {
    switch (cfg_.icon) {
    case IconChoice::Light: return IDI_APPICON_LIGHT;
    case IconChoice::Auto: {
        // "Match Windows theme": the dark icon while the Windows taskbar / tray is dark.
        DWORD usesLight = 1, size = sizeof(usesLight);
        if (::RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                           L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &usesLight, &size) != ERROR_SUCCESS)
            usesLight = 1;
        return usesLight ? IDI_APPICON_LIGHT : IDI_APPICON_DARK;
    }
    default: return IDI_APPICON_DARK;
    }
}

namespace {
// The embedded icon at the given size; each .ico holds 16..256 px images, so Windows picks
// the matching hand-tuned one. LR_SHARED icons are owned by the system and must not be destroyed.
HICON LoadAppIcon(int resId, int cx, int cy) {
    HICON h = static_cast<HICON>(::LoadImageW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(resId), IMAGE_ICON, cx, cy,
                                              LR_DEFAULTCOLOR | LR_SHARED));
    if (!h) {
        LogError(L"LoadImageW(app icon)", ::GetLastError());
        h = ::LoadIconW(nullptr, IDI_APPLICATION);
    }
    return h;
}
}  // namespace

void App::AddTrayIcon() {
    iconResId_ = IconResourceId();
    nid_ = {};
    nid_.cbSize           = sizeof(nid_);
    nid_.hWnd             = hwnd_;
    nid_.uID              = 1;
    nid_.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid_.uCallbackMessage = kTrayMessage;
    nid_.hIcon            = LoadAppIcon(iconResId_, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
    ::StringCchCopyW(nid_.szTip, _countof(nid_.szTip), L"Wallpaper Switcher");
    if (!Check(::Shell_NotifyIconW(NIM_ADD, &nid_), L"Shell_NotifyIconW(NIM_ADD)")) return;   // retried on TaskbarCreated
    nid_.uVersion = NOTIFYICON_VERSION_4;       // keyboard activation + proper context-menu events
    trayV4_ = ::Shell_NotifyIconW(NIM_SETVERSION, &nid_) != FALSE;
}

void App::ApplyIcon(bool force) {
    const int id = IconResourceId();
    if (!force && id == iconResId_) return;
    iconResId_ = id;
    HICON small_ = LoadAppIcon(id, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
    HICON big    = LoadAppIcon(id, ::GetSystemMetrics(SM_CXICON),   ::GetSystemMetrics(SM_CYICON));
    if (hwnd_) {
        ::SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_));
        ::SendMessageW(hwnd_, WM_SETICON, ICON_BIG,   reinterpret_cast<LPARAM>(big));
    }
    nid_.hIcon  = small_;
    nid_.uFlags = NIF_ICON;
    Check(::Shell_NotifyIconW(NIM_MODIFY, &nid_), L"Shell_NotifyIconW(NIM_MODIFY icon)");
}

void App::SetTooltip(const std::wstring& text) {
    ::StringCchCopyW(nid_.szTip, _countof(nid_.szTip), text.c_str());     // truncates if too long
    nid_.uFlags = NIF_TIP | NIF_SHOWTIP;
    ::Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void App::OnTrayEvent(WPARAM wp, LPARAM lp) {
    if (trayV4_) {
        switch (LOWORD(lp)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
            Step(+1);
            break;
        case WM_CONTEXTMENU: {
            POINT pt = { GET_X_LPARAM(wp), GET_Y_LPARAM(wp) };
            ShowMenu(pt);
            break;
        }
        default: break;
        }
        return;
    }
    if (lp == WM_LBUTTONUP) {
        Step(+1);
    } else if (lp == WM_RBUTTONUP) {
        POINT pt = {};
        if (!Check(::GetCursorPos(&pt), L"GetCursorPos")) return;
        ShowMenu(pt);
    }
}

// ---- menu ----------------------------------------------------------------------------------

bool App::PerMonitorMode() const {
    return cfg_.monitorMode == MonitorMode::Different && GetStyle(cfg_.styleIdx).pos != DWPOS_SPAN;
}

void App::ShowMenu(POINT anchor) {
    ScopedFlag modal(modal_);
    Playlist& list = library_.List();
    list.PrefetchRandom(kSampleSize);        // learn a few more images each time the menu opens

    std::vector<MonitorInfo> mons;
    {
        DesktopWallpaper dw;
        if (dw.Open()) mons = dw.Monitors();
    }

    PopupMenu menu, intervals, styles, monitors, icons;
    if (!menu || !intervals || !styles || !monitors || !icons) return;

    for (int i = 0; i < kIntervalCount; ++i)
        intervals.Item(kIdIntervalBase + static_cast<UINT>(i), kIntervals[i].label, i == cfg_.intervalIdx);
    for (int i = 0; i < kStyleCount; ++i)
        styles.Item(kIdStyleBase + static_cast<UINT>(i), GetStyle(i).label, i == cfg_.styleIdx);

    monitors.Item(kIdMonModeBase + 0, L"Same image on all monitors", cfg_.monitorMode == MonitorMode::Same);
    monitors.Item(kIdMonModeBase + 1, L"Different image on each monitor", cfg_.monitorMode == MonitorMode::Different);
    monitors.Separator();
    monitors.Item(kIdSmartCrop, L"Smart crop for portrait monitors", cfg_.smartCrop);
    if (PerMonitorMode() && mons.size() > 1) {
        monitors.Separator();
        for (size_t k = 0; k < mons.size(); ++k) {
            const RECT& rc = mons[k].rc;
            monitors.Item(kIdMonNextBase + static_cast<UINT>(k),
                          Format(L"Next on monitor %u (%d x %d, %ls)", static_cast<unsigned>(k + 1),
                                 static_cast<int>(rc.right - rc.left), static_cast<int>(rc.bottom - rc.top),
                                 OrientationOfRect(rc) == kOrientLandscape ? L"landscape" : L"portrait"));
        }
    } else if (mons.size() <= 1) {
        monitors.Separator();
        monitors.Item(0, L"Only one monitor detected", false, false);
    } else if (GetStyle(cfg_.styleIdx).pos == DWPOS_SPAN) {
        monitors.Separator();
        monitors.Item(0, L"Span uses one image for all monitors", false, false);
    }
    monitors.Separator();
    monitors.Item(0, Format(L"Checked %u of %u images: %u landscape, %u portrait", static_cast<unsigned>(list.Known()),
                            static_cast<unsigned>(list.Files().size()), static_cast<unsigned>(list.Count(kOrientLandscape)),
                            static_cast<unsigned>(list.Count(kOrientPortrait))), false, false);

    for (int i = 0; i < 3; ++i)
        icons.Item(kIdIconBase + static_cast<UINT>(i), kIconLabels[i], i == static_cast<int>(cfg_.icon));

    menu.Item(kIdNext, L"Next wallpaper\tCtrl+Alt+Right");
    menu.Item(kIdPrev, L"Previous wallpaper\tCtrl+Alt+Left");
    menu.Separator();
    menu.Item(kIdFolder, L"Choose folder...");
    menu.Item(kIdRescan, L"Rescan folder");
    menu.Submenu(L"Change every", std::move(intervals));
    menu.Submenu(L"Picture position", std::move(styles));
    menu.Submenu(L"Monitors", std::move(monitors));
    menu.Submenu(L"Icon", std::move(icons));
    menu.Item(kIdShuffle, L"Shuffle", cfg_.shuffle);
    menu.Item(kIdFade, L"Cross-fade transition", cfg_.fade);
    menu.Item(kIdStartup, L"Run at startup", IsRunAtStartup());
    menu.Separator();
    menu.Item(kIdExit, L"Exit");

    ::SetForegroundWindow(hwnd_);            // required so the menu closes on an outside click
    if (!::TrackPopupMenu(menu.get(), TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, anchor.x, anchor.y, 0, hwnd_, nullptr))
        LogError(L"TrackPopupMenu", ::GetLastError());
    ::PostMessageW(hwnd_, WM_NULL, 0, 0);
}

void App::OnCommand(UINT id) {
    if (id >= kIdIntervalBase && id < kIdIntervalBase + static_cast<UINT>(kIntervalCount)) {
        cfg_.intervalIdx = static_cast<int>(id - kIdIntervalBase);
        SaveConfig(cfg_);
        ApplyTimer();
        return;
    }
    if (id >= kIdStyleBase && id < kIdStyleBase + static_cast<UINT>(kStyleCount)) {
        cfg_.styleIdx = static_cast<int>(id - kIdStyleBase);
        SaveConfig(cfg_);
        RefreshCurrent();
        return;
    }
    if (id == kIdMonModeBase || id == kIdMonModeBase + 1) {
        cfg_.monitorMode = id == kIdMonModeBase ? MonitorMode::Same : MonitorMode::Different;
        SaveConfig(cfg_);
        RefreshCurrent();
        return;
    }
    if (id >= kIdIconBase && id < kIdIconBase + 3) {
        cfg_.icon = static_cast<IconChoice>(id - kIdIconBase);
        SaveConfig(cfg_);
        ApplyIcon(false);
        return;
    }
    if (id >= kIdMonNextBase && id < kIdMonNextBase + static_cast<UINT>(kMaxMonitors)) {
        Step(+1, static_cast<int>(id - kIdMonNextBase));
        return;
    }
    switch (id) {
    case kIdNext:   Step(+1); break;
    case kIdPrev:   Step(-1); break;
    case kIdFolder: ChooseFolder(); break;
    case kIdRescan: Rescan(); break;
    case kIdShuffle:
        cfg_.shuffle = !cfg_.shuffle;
        SaveConfig(cfg_);
        library_.List().Reorder(cfg_.shuffle);          // new play order; what we know about images is kept
        haveShown_ = false;
        break;
    case kIdSmartCrop:
        cfg_.smartCrop = !cfg_.smartCrop;
        SaveConfig(cfg_);
        library_.List().SetSmartCrop(cfg_.smartCrop);
        RefreshCurrent();                               // re-apply: cropped now, or portrait-only again
        break;
    case kIdFade:
        cfg_.fade = !cfg_.fade;
        SaveConfig(cfg_);
        break;
    case kIdStartup:
        if (!SetRunAtStartup(!IsRunAtStartup())) SetTooltip(L"Wallpaper Switcher - could not change the startup setting");
        break;
    case kIdExit:
        ::DestroyWindow(hwnd_);
        break;
    default:
        break;
    }
}

// ---- folder --------------------------------------------------------------------------------

bool App::PickFolder(std::wstring& out) {
    ComPtr<IFileOpenDialog> dlg;
    if (!Succeeded(::CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dlg.Put())),
                   L"CoCreateInstance(FileOpenDialog)"))
        return false;
    DWORD opts = 0;
    if (!Succeeded(dlg->GetOptions(&opts), L"IFileDialog::GetOptions") ||
        !Succeeded(dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST), L"IFileDialog::SetOptions"))
        return false;
    dlg->SetTitle(L"Choose your wallpaper folder");
    if (!cfg_.folder.empty()) {                         // start where the user already is
        ComPtr<IShellItem> start;
        if (SUCCEEDED(::SHCreateItemFromParsingName(cfg_.folder.c_str(), nullptr, IID_PPV_ARGS(start.Put()))) && start)
            dlg->SetFolder(start.Get());
    }
    const HRESULT hr = dlg->Show(hwnd_);
    if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return false;
    if (!Succeeded(hr, L"IFileDialog::Show")) return false;
    ComPtr<IShellItem> item;
    CoString path;
    if (!Succeeded(dlg->GetResult(item.Put()), L"IFileDialog::GetResult") ||
        !Succeeded(item->GetDisplayName(SIGDN_FILESYSPATH, path.Put()), L"IShellItem::GetDisplayName") || path.Empty())
        return false;
    out = path.Str();
    return true;
}

void App::ChooseFolder() {
    std::wstring f;
    bool picked;
    {
        ScopedFlag modal(modal_);
        ::SetForegroundWindow(hwnd_);
        picked = PickFolder(f);
    }
    if (!picked) return;
    cfg_.folder = f;
    SaveConfig(cfg_);
    Rescan();
    Step(+1);
}

void App::Rescan() {
    library_.Scan(cfg_.folder, cfg_.shuffle);
    haveShown_ = false;
}

// ---- wallpaper -----------------------------------------------------------------------------

void App::ApplyTimer() {
    ::KillTimer(hwnd_, kTimerId);
    const UINT mins = kIntervals[ClampInt(cfg_.intervalIdx, 0, kIntervalCount - 1)].minutes;
    if (mins != 0) Check(::SetTimer(hwnd_, kTimerId, mins * 60u * 1000u, nullptr) != 0, L"SetTimer");
}

void App::Step(int dir, int only) {
    if (stepping_) return;
    ScopedFlag guard(stepping_);
    try {
        StepImpl(dir, only);
    } catch (const std::exception&) {
        LogError(L"exception while changing the wallpaper", ERROR_INTERNAL_ERROR);
    }
}

// Landscape displays only ever receive landscape images. Portrait displays receive portrait
// images as they are and (smart crop on) landscape images cropped to the display's shape.
// Different displays never show the same image. At most kProbeBudget unread images are opened
// per display.
void App::StepImpl(int dir, int only) {
    Playlist& list = library_.List();
    if (list.Empty()) Rescan();
    if (list.Empty()) {
        SetTooltip(L"Wallpaper Switcher - no images in folder");
        return;
    }

    DesktopWallpaper dw;
    const bool haveDesktop = dw.Open();
    std::vector<MonitorInfo> mons;
    if (haveDesktop) mons = dw.Monitors();
    const std::vector<Slot> slots = BuildSlots(haveDesktop, mons, cfg_.monitorMode, cfg_.styleIdx);
    if (haveDesktop) dw.SetPosition(GetStyle(cfg_.styleIdx).pos);

    FadeSession fade(dw, mons, cfg_.fade);
    auto apply = [&](const Slot& slot, const FileEntry& fe) {
        std::wstring toSet;
        if (slot.orient == kOrientPortrait && cfg_.smartCrop) toSet = MakeSmartCrop(fe, slot.tw, slot.th);   // empty = use the original
        if (toSet.empty()) toSet = fe.path;
        fade.Begin(slot, toSet);                      // cover the old picture first
        return ApplyToSlot(dw, slot, toSet, cfg_.styleIdx);
    };
    const RunResult r = list.Run(slots, dir, only, apply);
    fade.Run();                                       // then dissolve it away
    if (r.applied) haveShown_ = true;

    if (r.missing >= 0) {
        SetTooltip(r.missing == kOrientLandscape ? L"Wallpaper Switcher - no landscape images in folder"
                                                 : L"Wallpaper Switcher - no portrait images in folder");
    } else if (r.searching >= 0) {
        SetTooltip(r.searching == kOrientLandscape ? L"Wallpaper Switcher - still looking for landscape images"
                                                   : L"Wallpaper Switcher - still looking for portrait images");
    } else if (slots.size() == 1) {
        const int f = list.CursorFile(slots[0].key);
        if (f >= 0) {
            const std::wstring& path = list.Files()[static_cast<size_t>(f)].path;
            const size_t slash = path.find_last_of(L'\\');
            SetTooltip(L"Wallpaper: " + (slash == std::wstring::npos ? path : path.substr(slash + 1)));
        }
    } else {
        SetTooltip(L"Wallpaper Switcher - multiple monitors");
    }
}

// Apply a changed setting (position, monitor mode, smart crop) without picking a new image
// when nothing has been shown yet by this session.
void App::RefreshCurrent() {
    if (haveShown_) { Step(0); return; }
    DesktopWallpaper dw;
    if (dw.Open()) {
        dw.SetPosition(GetStyle(cfg_.styleIdx).pos);
    } else {
        wchar_t cur[MAX_PATH] = {};
        if (::SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, cur, 0) && cur[0]) SetWallpaperLegacy(cur, cfg_.styleIdx);
    }
}

}  // namespace ws
