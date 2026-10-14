#include "config.h"

#include "win_util.h"

namespace ws {
namespace {

constexpr wchar_t kSettingsKey[] = L"Software\\WallpaperSwitcher";
constexpr wchar_t kRunKey[]      = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[]    = L"WallpaperSwitcher";
constexpr DWORD   kMaxStringBytes = 64 * 1024;       // sanity limit for values we read

// Reads a REG_SZ of any length (two-call pattern, so long folder paths are not truncated).
bool ReadString(HKEY root, const wchar_t* subkey, const wchar_t* name, std::wstring& out) {
    DWORD bytes = 0;
    LSTATUS rc = ::RegGetValueW(root, subkey, name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes);
    if (rc != ERROR_SUCCESS || bytes < sizeof(wchar_t) || bytes > kMaxStringBytes) return false;
    std::wstring buf(bytes / sizeof(wchar_t), L'\0');
    rc = ::RegGetValueW(root, subkey, name, RRF_RT_REG_SZ, nullptr, &buf[0], &bytes);
    if (rc != ERROR_SUCCESS) return false;
    buf.resize(wcslen(buf.c_str()));                 // `bytes` includes the terminator
    out = std::move(buf);
    return true;
}

DWORD ReadDword(HKEY root, const wchar_t* subkey, const wchar_t* name, DWORD def) {
    DWORD v = 0, size = sizeof(v);
    if (::RegGetValueW(root, subkey, name, RRF_RT_REG_DWORD, nullptr, &v, &size) == ERROR_SUCCESS) return v;
    return def;
}

// One open key for a batch of writes; every write is checked.
class WriteKey {
public:
    WriteKey(HKEY root, const wchar_t* subkey) {
        const LSTATUS rc = ::RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, key_.put(), nullptr);
        if (rc != ERROR_SUCCESS) { LogError(L"RegCreateKeyExW", static_cast<DWORD>(rc)); key_.reset(); }
    }
    bool Ok() const { return static_cast<bool>(key_); }

    bool String(const wchar_t* name, const std::wstring& v) {
        if (!key_) return false;
        const LSTATUS rc = ::RegSetValueExW(key_.get(), name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()),
                                            static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
        if (rc != ERROR_SUCCESS) LogError(L"RegSetValueExW(string)", static_cast<DWORD>(rc));
        return rc == ERROR_SUCCESS;
    }
    bool Dword(const wchar_t* name, DWORD v) {
        if (!key_) return false;
        const LSTATUS rc = ::RegSetValueExW(key_.get(), name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&v), sizeof(v));
        if (rc != ERROR_SUCCESS) LogError(L"RegSetValueExW(dword)", static_cast<DWORD>(rc));
        return rc == ERROR_SUCCESS;
    }

private:
    UniqueHkey key_;
};

std::wstring QuotedExePath() {
    const std::wstring exe = ModulePath();
    return exe.empty() ? std::wstring() : L"\"" + exe + L"\"";
}

}  // namespace

Config LoadConfig() {
    Config c;
    ReadString(HKEY_CURRENT_USER, kSettingsKey, L"Folder", c.folder);
    const DWORD i = ReadDword(HKEY_CURRENT_USER, kSettingsKey, L"Interval", kDefaultIntervalIdx);
    const DWORD s = ReadDword(HKEY_CURRENT_USER, kSettingsKey, L"Style", 0);
    const DWORD m = ReadDword(HKEY_CURRENT_USER, kSettingsKey, L"MonitorMode", 0);
    const DWORD ic = ReadDword(HKEY_CURRENT_USER, kSettingsKey, L"Icon", 0);
    c.intervalIdx = i < static_cast<DWORD>(kIntervalCount) ? static_cast<int>(i) : kDefaultIntervalIdx;
    c.styleIdx    = s < static_cast<DWORD>(kStyleCount) ? static_cast<int>(s) : 0;
    c.monitorMode = m == 1 ? MonitorMode::Different : MonitorMode::Same;
    c.shuffle     = ReadDword(HKEY_CURRENT_USER, kSettingsKey, L"Shuffle", 1) != 0;
    c.smartCrop   = ReadDword(HKEY_CURRENT_USER, kSettingsKey, L"SmartCrop", 1) != 0;
    c.fade        = ReadDword(HKEY_CURRENT_USER, kSettingsKey, L"Fade", 1) != 0;
    c.icon        = ic == 1 ? IconChoice::Light : (ic == 2 ? IconChoice::Auto : IconChoice::Dark);
    return c;
}

bool SaveConfig(const Config& c) {
    WriteKey k(HKEY_CURRENT_USER, kSettingsKey);
    if (!k.Ok()) return false;
    bool ok = true;
    ok &= k.String(L"Folder", c.folder);
    ok &= k.Dword(L"Interval", static_cast<DWORD>(c.intervalIdx));
    ok &= k.Dword(L"Style", static_cast<DWORD>(c.styleIdx));
    ok &= k.Dword(L"MonitorMode", static_cast<DWORD>(c.monitorMode));
    ok &= k.Dword(L"Shuffle", c.shuffle ? 1 : 0);
    ok &= k.Dword(L"SmartCrop", c.smartCrop ? 1 : 0);
    ok &= k.Dword(L"Fade", c.fade ? 1 : 0);
    ok &= k.Dword(L"Icon", static_cast<DWORD>(c.icon));
    return ok;
}

bool IsRunAtStartup() {
    std::wstring v;
    const std::wstring want = QuotedExePath();
    return !want.empty() && ReadString(HKEY_CURRENT_USER, kRunKey, kRunValue, v) && _wcsicmp(v.c_str(), want.c_str()) == 0;
}

bool SetRunAtStartup(bool enable) {
    if (enable) {
        const std::wstring cmd = QuotedExePath();
        if (cmd.empty()) return false;
        WriteKey k(HKEY_CURRENT_USER, kRunKey);
        return k.Ok() && k.String(kRunValue, cmd);
    }
    UniqueHkey key;
    LSTATUS rc = ::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, key.put());
    if (rc == ERROR_FILE_NOT_FOUND) return true;            // nothing to remove
    if (rc != ERROR_SUCCESS) { LogError(L"RegOpenKeyExW(Run)", static_cast<DWORD>(rc)); return false; }
    rc = ::RegDeleteValueW(key.get(), kRunValue);
    if (rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND) { LogError(L"RegDeleteValueW", static_cast<DWORD>(rc)); return false; }
    return true;
}

}  // namespace ws
