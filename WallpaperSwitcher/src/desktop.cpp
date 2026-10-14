#include "desktop.h"

#include <algorithm>

#include "core/numeric.h"

namespace ws {
namespace {

const StyleDef kStyles[kStyleCount] = {
    { L"Fill",                 DWPOS_FILL,    L"10", L"0" },
    { L"Fit",                  DWPOS_FIT,     L"6",  L"0" },
    { L"Stretch",              DWPOS_STRETCH, L"2",  L"0" },
    { L"Center",               DWPOS_CENTER,  L"0",  L"0" },
    { L"Tile",                 DWPOS_TILE,    L"0",  L"1" },
    { L"Span across monitors", DWPOS_SPAN,    L"22", L"0" },
};

bool WriteDesktopRegString(const wchar_t* name, const wchar_t* value) {
    UniqueHkey key;
    LSTATUS rc = ::RegCreateKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                                   key.put(), nullptr);
    if (rc != ERROR_SUCCESS) { LogError(L"RegCreateKeyExW(Control Panel\\Desktop)", static_cast<DWORD>(rc)); return false; }
    rc = ::RegSetValueExW(key.get(), name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value),
                          static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
    if (rc != ERROR_SUCCESS) LogError(L"RegSetValueExW(wallpaper style)", static_cast<DWORD>(rc));
    return rc == ERROR_SUCCESS;
}

}  // namespace

const StyleDef& GetStyle(int index) { return kStyles[ClampIndex(index, kStyleCount)]; }

int OrientationOfRect(const RECT& rc) {
    return (rc.right - rc.left) >= (rc.bottom - rc.top) ? kOrientLandscape : kOrientPortrait;
}

// ---- DesktopWallpaper ----------------------------------------------------------------------

bool DesktopWallpaper::Open() {
    return Succeeded(::CoCreateInstance(__uuidof(::DesktopWallpaper), nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(dw_.Put())),
                     L"CoCreateInstance(DesktopWallpaper)") && dw_;
}

std::vector<MonitorInfo> DesktopWallpaper::Monitors() const {
    std::vector<MonitorInfo> out;
    if (!dw_) return out;
    UINT count = 0;
    if (!Succeeded(dw_->GetMonitorDevicePathCount(&count), L"GetMonitorDevicePathCount")) return out;
    for (UINT i = 0; i < count; ++i) {
        CoString id;
        if (FAILED(dw_->GetMonitorDevicePathAt(i, id.Put())) || id.Empty()) continue;
        RECT rc = {};
        // Paths for disconnected monitors fail here or give an empty rect.
        if (SUCCEEDED(dw_->GetMonitorRECT(id.Str().c_str(), &rc)) && !::IsRectEmpty(&rc)) out.push_back({ id.Str(), rc });
    }
    std::sort(out.begin(), out.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
        return a.rc.left != b.rc.left ? a.rc.left < b.rc.left : a.rc.top < b.rc.top;
    });
    if (out.size() > static_cast<size_t>(kMaxMonitors)) out.resize(kMaxMonitors);
    return out;
}

bool DesktopWallpaper::SetPosition(DESKTOP_WALLPAPER_POSITION pos) {
    return dw_ && Succeeded(dw_->SetPosition(pos), L"IDesktopWallpaper::SetPosition");
}

bool DesktopWallpaper::GetPosition(DESKTOP_WALLPAPER_POSITION& pos) const {
    return dw_ && Succeeded(dw_->GetPosition(&pos), L"IDesktopWallpaper::GetPosition");
}

bool DesktopWallpaper::SetWallpaper(const wchar_t* monitorIdOrNull, const std::wstring& path) {
    return dw_ && Succeeded(dw_->SetWallpaper(monitorIdOrNull, path.c_str()), L"IDesktopWallpaper::SetWallpaper");
}

std::wstring DesktopWallpaper::GetWallpaper(const std::wstring& monitorId) const {
    CoString p;
    if (!dw_ || FAILED(dw_->GetWallpaper(monitorId.c_str(), p.Put()))) return std::wstring();
    return p.Str();
}

COLORREF DesktopWallpaper::BackgroundColor() const {
    COLORREF c = 0;
    if (dw_) dw_->GetBackgroundColor(&c);
    return c;
}

// ---- slots ---------------------------------------------------------------------------------

std::vector<Slot> BuildSlots(bool haveDesktop, const std::vector<MonitorInfo>& mons, MonitorMode mode, int styleIdx) {
    std::vector<Slot> slots;

    if (!haveDesktop || mons.empty()) {
        Slot s;
        s.tw = ::GetSystemMetrics(SM_CXSCREEN);
        s.th = ::GetSystemMetrics(SM_CYSCREEN);
        s.orient = s.tw >= s.th ? kOrientLandscape : kOrientPortrait;
        s.key = s.orient;
        s.all = true;
        slots.push_back(s);
        return slots;
    }

    if (GetStyle(styleIdx).pos == DWPOS_SPAN) {
        RECT u = mons[0].rc;
        for (const MonitorInfo& m : mons) ::UnionRect(&u, &u, &m.rc);
        Slot s;
        s.orient = OrientationOfRect(u);
        s.tw = u.right - u.left;
        s.th = u.bottom - u.top;
        s.key = kKeySpan;
        s.all = true;
        slots.push_back(s);
        return slots;
    }

    if (mode == MonitorMode::Different && mons.size() > 1) {
        for (size_t k = 0; k < mons.size(); ++k) {
            Slot s;
            s.orient = OrientationOfRect(mons[k].rc);
            s.tw = mons[k].rc.right - mons[k].rc.left;
            s.th = mons[k].rc.bottom - mons[k].rc.top;
            s.key = kKeyMonitor + static_cast<int>(k);
            s.ids.push_back(mons[k].id);
            slots.push_back(s);
        }
        return slots;
    }

    // Same image: one slot per orientation group; the group's first monitor sets the crop shape.
    Slot group[2];
    for (int o = 0; o < 2; ++o) { group[o].orient = o; group[o].key = o; }
    for (const MonitorInfo& m : mons) {
        Slot& g = group[OrientationOfRect(m.rc)];
        if (g.ids.empty()) { g.tw = m.rc.right - m.rc.left; g.th = m.rc.bottom - m.rc.top; }
        g.ids.push_back(m.id);
    }
    for (int o = 0; o < 2; ++o)
        if (!group[o].ids.empty()) slots.push_back(group[o]);
    if (slots.size() == 1) { slots[0].all = true; slots[0].ids.clear(); }
    return slots;
}

// ---- applying ------------------------------------------------------------------------------

bool SetWallpaperLegacy(const std::wstring& path, int styleIdx) {
    const StyleDef& s = GetStyle(styleIdx);
    if (!WriteDesktopRegString(L"WallpaperStyle", s.regStyle) || !WriteDesktopRegString(L"TileWallpaper", s.regTile))
        return false;
    // SPI_SETDESKWALLPAPER takes a non-const PVOID but does not modify the string.
    return Check(::SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, const_cast<wchar_t*>(path.c_str()),
                                         SPIF_UPDATEINIFILE | SPIF_SENDCHANGE), L"SystemParametersInfoW(SETDESKWALLPAPER)");
}

bool ApplyToSlot(DesktopWallpaper& dw, const Slot& slot, const std::wstring& path, int styleIdx) {
    if (!dw.IsOpen()) return SetWallpaperLegacy(path, styleIdx);
    if (slot.all) return dw.SetWallpaper(nullptr, path);
    bool ok = false;
    for (const std::wstring& id : slot.ids)
        if (dw.SetWallpaper(id.c_str(), path)) ok = true;
    return ok;
}

}  // namespace ws
