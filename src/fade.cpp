#include "fade.h"

#include <algorithm>

#include "core/fade_math.h"
#include "wic.h"

namespace ws {
namespace {

constexpr wchar_t kFadeClass[] = L"WallpaperSwitcherFade";
constexpr ULONGLONG kHostRetryDelayMs = 60 * 1000;     // after a failed lookup, don't retry for a minute
ULONGLONG g_retryAfter = 0;

LRESULT CALLBACK FadeProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    try {
        switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = ::BeginPaint(hwnd, &ps);
            HBITMAP bmp = reinterpret_cast<HBITMAP>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (dc && bmp) {
                UniqueDc mem(::CreateCompatibleDC(dc));
                if (mem) {
                    HGDIOBJ old = ::SelectObject(mem.get(), bmp);
                    RECT rc;
                    ::GetClientRect(hwnd, &rc);
                    ::BitBlt(dc, 0, 0, rc.right, rc.bottom, mem.get(), 0, 0, SRCCOPY);
                    ::SelectObject(mem.get(), old);
                }
            }
            ::EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_NCHITTEST:  return HTTRANSPARENT;
        default: break;
        }
    } catch (...) {
        // Never let an exception unwind through the OS window-procedure frames.
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

BOOL CALLBACK EnumFindWallpaperHost(HWND top, LPARAM lp) {
    if (::FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr)) {      // the window holding the icons ...
        HWND w = ::FindWindowExW(nullptr, top, L"WorkerW", nullptr);        // ... the WorkerW right behind it
        if (w) { *reinterpret_cast<HWND*>(lp) = w; return FALSE; }
    }
    return TRUE;
}

// Explorer's wallpaper layer: the WorkerW behind the desktop icons (a child of Progman on
// newer Windows 11 builds, a top-level sibling on older ones).
HWND FindWallpaperHost() {
    HWND progman = ::FindWindowW(L"Progman", nullptr);
    if (!progman) return nullptr;
    for (int attempt = 0; attempt < 2; ++attempt) {
        HWND host = ::FindWindowExW(progman, nullptr, L"WorkerW", nullptr);
        if (!host) ::EnumWindows(EnumFindWallpaperHost, reinterpret_cast<LPARAM>(&host));
        if (host) return host;
        DWORD_PTR res = 0;                                                  // ask Explorer to create it
        if (!::SendMessageTimeoutW(progman, 0x052C, 0xD, 0x1, SMTO_NORMAL | SMTO_ABORTIFHUNG, 250, &res))
            LogError(L"SendMessageTimeoutW(Progman 0x052C)", ::GetLastError());
    }
    return nullptr;
}

// Make windows created on this thread use the same DPI awareness context as the host window
// (Windows refuses to parent windows with a different one).
class DpiScope {
public:
    explicit DpiScope(HWND host) {
        using GetCtxFn = HANDLE(WINAPI*)(HWND);
        using SetCtxFn = HANDLE(WINAPI*)(HANDLE);
        HMODULE u = ::GetModuleHandleW(L"user32.dll");
        if (!u) return;
        GetCtxFn get = reinterpret_cast<GetCtxFn>(reinterpret_cast<void*>(::GetProcAddress(u, "GetWindowDpiAwarenessContext")));
        SetCtxFn set = reinterpret_cast<SetCtxFn>(reinterpret_cast<void*>(::GetProcAddress(u, "SetThreadDpiAwarenessContext")));
        if (!get || !set) return;
        HANDLE ctx = get(host);
        if (!ctx) return;
        prev_ = set(ctx);
        set_ = set;
    }
    DpiScope(const DpiScope&) = delete;
    DpiScope& operator=(const DpiScope&) = delete;
    ~DpiScope() { if (set_ && prev_) set_(prev_); }

private:
    HANDLE (WINAPI* set_)(HANDLE) = nullptr;
    HANDLE prev_ = nullptr;
};

void WaitForVBlank() {
    using DwmFlushFn = HRESULT(WINAPI*)();
    static DwmFlushFn fn = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (HMODULE m = LoadSystemLibrary(L"dwmapi.dll"))
            fn = reinterpret_cast<DwmFlushFn>(reinterpret_cast<void*>(::GetProcAddress(m, "DwmFlush")));
    }
    LARGE_INTEGER f, a, b;
    ::QueryPerformanceFrequency(&f);
    ::QueryPerformanceCounter(&a);
    const bool ok = fn && SUCCEEDED(fn());
    ::QueryPerformanceCounter(&b);
    const double ms = static_cast<double>(b.QuadPart - a.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart);
    if (!ok || ms < 2.0) ::Sleep(8);               // no vblank sync available: fall back to ~120 Hz pacing
}

PlaceMode ToPlaceMode(DESKTOP_WALLPAPER_POSITION pos) {
    switch (pos) {
    case DWPOS_FILL: case DWPOS_SPAN: return PlaceMode::Fill;
    case DWPOS_FIT:                   return PlaceMode::Fit;
    case DWPOS_STRETCH:               return PlaceMode::Stretch;
    case DWPOS_TILE:                  return PlaceMode::Tile;
    default:                          return PlaceMode::Center;
    }
}

UniqueBitmap CreateDib(int w, int h, void** bits) {
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;                // top-down
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    UniqueBitmap bmp(::CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, bits, nullptr, 0));
    if (!bmp) LogError(L"CreateDIBSection", ::GetLastError());
    return bmp;
}

// Draw `path` onto a W x H bitmap the way the desktop would, so the cover matches what is on
// screen. Returns an empty bitmap on failure.
UniqueBitmap RenderWallpaper(const std::wstring& path, DESKTOP_WALLPAPER_POSITION pos, int W, int H, COLORREF bg) {
    IWICImagingFactory* wic = GetWic();
    if (!wic || W <= 0 || H <= 0 || W > kMaxRenderDim || H > kMaxRenderDim) return UniqueBitmap();

    OrientedImage img;
    if (!img.Open(path)) return UniqueBitmap();

    const PlaceMode mode = ToPlaceMode(pos);
    const Placement pl = ComputePlacement(mode, W, H, static_cast<int>(img.Width()), static_cast<int>(img.Height()));

    ComPtr<IWICBitmapScaler> scaler;
    IWICBitmapSource* scaled = img.Source();
    if (pl.dw != static_cast<int>(img.Width()) || pl.dh != static_cast<int>(img.Height())) {
        if (!Succeeded(wic->CreateBitmapScaler(scaler.Put()), L"CreateBitmapScaler") ||
            !Succeeded(scaler->Initialize(img.Source(), static_cast<UINT>(pl.dw), static_cast<UINT>(pl.dh),
                                          WICBitmapInterpolationModeFant), L"Scaler::Initialize"))
            return UniqueBitmap();
        scaled = scaler.Get();
    }
    ComPtr<IWICFormatConverter> conv;
    if (!Succeeded(wic->CreateFormatConverter(conv.Put()), L"CreateFormatConverter") ||
        !Succeeded(conv->Initialize(scaled, kPixelFormat32bppBGR, WICBitmapDitherTypeNone, nullptr, 0.0,
                                    WICBitmapPaletteTypeCustom), L"FormatConverter::Initialize"))
        return UniqueBitmap();
    IWICBitmapSource* px = conv.Get();
    ComPtr<IWICBitmap> cached;
    if (mode == PlaceMode::Tile) {                          // tiles reuse the decoded pixels
        if (!Succeeded(wic->CreateBitmapFromSource(conv.Get(), WICBitmapCacheOnLoad, cached.Put()), L"CreateBitmapFromSource"))
            return UniqueBitmap();
        px = cached.Get();
    }

    void* bits = nullptr;
    UniqueBitmap bmp = CreateDib(W, H, &bits);
    if (!bmp || !bits) return UniqueBitmap();
    const DWORD fill = (static_cast<DWORD>(GetRValue(bg)) << 16) | (static_cast<DWORD>(GetGValue(bg)) << 8) | GetBValue(bg);
    std::fill_n(static_cast<DWORD*>(bits), static_cast<size_t>(W) * static_cast<size_t>(H), fill);

    const UINT stride = static_cast<UINT>(W) * 4;
    auto blit = [&](int ox, int oy) {                       // copy the visible part of `px` placed at (ox, oy)
        const int x0 = std::max(0, ox), y0 = std::max(0, oy);
        const int x1 = std::min(W, ox + pl.dw), y1 = std::min(H, oy + pl.dh);
        if (x1 <= x0 || y1 <= y0) return;
        WICRect r = { x0 - ox, y0 - oy, x1 - x0, y1 - y0 };
        BYTE* dst = static_cast<BYTE*>(bits) + static_cast<size_t>(y0) * stride + static_cast<size_t>(x0) * 4;
        const UINT cb = static_cast<UINT>(static_cast<size_t>(H - y0) * stride - static_cast<size_t>(x0) * 4);
        px->CopyPixels(&r, stride, cb, dst);
    };
    if (mode == PlaceMode::Tile) {
        for (int ty = 0; ty < H; ty += pl.dh)
            for (int tx = 0; tx < W; tx += pl.dw) blit(tx, ty);
    } else {
        blit(pl.dx, pl.dy);
    }
    return bmp;
}

UniqueBitmap CropBitmap(HBITMAP src, const RECT& r) {
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return UniqueBitmap();
    void* bits = nullptr;
    UniqueBitmap out = CreateDib(w, h, &bits);
    if (!out) return UniqueBitmap();
    UniqueDc ds(::CreateCompatibleDC(nullptr)), dd(::CreateCompatibleDC(nullptr));
    if (!ds || !dd) return UniqueBitmap();
    HGDIOBJ o1 = ::SelectObject(ds.get(), src), o2 = ::SelectObject(dd.get(), out.get());
    const BOOL ok = ::BitBlt(dd.get(), 0, 0, w, h, ds.get(), r.left, r.top, SRCCOPY);
    ::SelectObject(ds.get(), o1);
    ::SelectObject(dd.get(), o2);
    if (!ok) return UniqueBitmap();
    return out;
}

}  // namespace

bool RegisterFadeWindowClass(HINSTANCE instance) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = FadeProc;
    wc.hInstance     = instance;
    wc.lpszClassName = kFadeClass;
    if (!::RegisterClassW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        LogError(L"RegisterClassW(fade)", ::GetLastError());
        return false;
    }
    return true;
}

void ResetFadeBackoff() { g_retryAfter = 0; }

FadeSession::FadeSession(const DesktopWallpaper& dw, std::vector<MonitorInfo> monitors, bool enabled)
    : dw_(dw), monitors_(std::move(monitors)), enabled_(enabled && dw.IsOpen()) {}

FadeSession::~FadeSession() { Clear(); }

void FadeSession::Clear() { overlays_.clear(); }

bool FadeSession::Covered(const std::wstring& id) const {
    return std::any_of(overlays_.begin(), overlays_.end(), [&](const Overlay& o) { return o.id == id; });
}

bool FadeSession::EnsureHost() {
    if (host_ && ::IsWindow(host_)) return true;
    if (hostTried_) return false;
    hostTried_ = true;
    if (::GetTickCount64() < g_retryAfter) return false;           // failed recently: skip, don't stall every switch
    host_ = FindWallpaperHost();
    if (!host_) g_retryAfter = ::GetTickCount64() + kHostRetryDelayMs;
    return host_ != nullptr;
}

void FadeSession::Begin(const Slot& slot, const std::wstring& newPath) {
    if (!enabled_ || monitors_.empty() || !EnsureHost()) return;

    DESKTOP_WALLPAPER_POSITION pos = DWPOS_FILL;
    dw_.GetPosition(pos);

    UniqueBitmap spanBitmap;
    RECT uni = monitors_[0].rc;
    for (const MonitorInfo& m : monitors_) ::UnionRect(&uni, &uni, &m.rc);

    bool any = false;
    for (const MonitorInfo& m : monitors_) {
        if (!slot.all && std::find(slot.ids.begin(), slot.ids.end(), m.id) == slot.ids.end()) continue;
        if (Covered(m.id)) continue;

        const std::wstring oldPath = dw_.GetWallpaper(m.id);
        if (oldPath.empty() || _wcsicmp(oldPath.c_str(), newPath.c_str()) == 0) continue;   // nothing to fade
        const COLORREF bg = dw_.BackgroundColor();

        const int w = m.rc.right - m.rc.left, h = m.rc.bottom - m.rc.top;
        UniqueBitmap bmp;
        if (pos == DWPOS_SPAN) {                             // one picture over the whole desktop
            if (!spanBitmap) spanBitmap = RenderWallpaper(oldPath, pos, uni.right - uni.left, uni.bottom - uni.top, bg);
            if (spanBitmap) {
                const RECT part = { m.rc.left - uni.left, m.rc.top - uni.top, m.rc.right - uni.left, m.rc.bottom - uni.top };
                bmp = CropBitmap(spanBitmap.get(), part);
            }
        } else {
            bmp = RenderWallpaper(oldPath, pos, w, h, bg);
        }
        if (!bmp) continue;

        UniqueHwnd win;
        {
            DpiScope dpi(host_);
            POINT pt = { m.rc.left, m.rc.top };
            ::ScreenToClient(host_, &pt);
            win.reset(::CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE, kFadeClass, L"", WS_CHILD,
                                        pt.x, pt.y, w, h, host_, nullptr, ::GetModuleHandleW(nullptr), nullptr));
        }
        if (!win) { LogError(L"CreateWindowExW(fade overlay)", ::GetLastError()); continue; }
        ::SetWindowLongPtrW(win.get(), GWLP_USERDATA, reinterpret_cast<LONG_PTR>(bmp.get()));
        if (!Check(::SetLayeredWindowAttributes(win.get(), 0, 255, LWA_ALPHA), L"SetLayeredWindowAttributes")) continue;
        ::SetWindowPos(win.get(), HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ::UpdateWindow(win.get());

        Overlay o;
        o.id = m.id;
        o.bitmap = std::move(bmp);
        o.hwnd = std::move(win);
        overlays_.push_back(std::move(o));
        any = true;
    }
    if (any) WaitForVBlank();                               // make sure the cover is on screen before the swap
}

void FadeSession::Run() {
    if (overlays_.empty()) return;
    LARGE_INTEGER freq, t0, now;
    ::QueryPerformanceFrequency(&freq);
    ::QueryPerformanceCounter(&t0);
    for (;;) {
        ::QueryPerformanceCounter(&now);
        const double ms = static_cast<double>(now.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
        const int a = FadeAlpha(ms, kFadeDurationMs);
        for (const Overlay& o : overlays_) ::SetLayeredWindowAttributes(o.hwnd.get(), 0, static_cast<BYTE>(a), LWA_ALPHA);
        if (a == 0) break;
        WaitForVBlank();
    }
    Clear();
}

}  // namespace ws
