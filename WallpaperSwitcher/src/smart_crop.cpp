#include "smart_crop.h"

#include "win_util.h"   // first: defines the Windows environment the SDK headers below rely on

#include <knownfolders.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/crop_core.h"
#include "core/numeric.h"
#include "wic.h"

namespace ws {
namespace {

constexpr int kCropVersion = 1;        // bump when the crop algorithm changes (invalidates cached crops)
constexpr int kMaxCrops = 64;          // cached crop files kept on disk
constexpr DWORD kXmpReadBytes = 262144;

std::wstring CropCacheDir() {
    CoString base;
    if (!Succeeded(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, base.Put()),
                   L"SHGetKnownFolderPath(LocalAppData)") || base.Empty())
        return std::wstring();
    std::wstring dir = JoinPath(base.Str(), L"WallpaperSwitcher");
    for (int pass = 0; pass < 2; ++pass) {
        if (!::CreateDirectoryW(dir.c_str(), nullptr) && ::GetLastError() != ERROR_ALREADY_EXISTS) {
            LogError(L"CreateDirectoryW(crop cache)", ::GetLastError());
            return std::wstring();
        }
        if (pass == 0) dir = JoinPath(dir, L"crops");
    }
    return dir;
}

// Cache key: source file identity + target size + algorithm version (FNV-1a).
std::wstring CropFileName(const FileEntry& f, int tw, int th) {
    std::uint64_t h = 1469598103934665603ULL;
    auto mix = [&h](const void* data, size_t n) {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    };
    const int ver = kCropVersion;
    mix(f.path.c_str(), f.path.size() * sizeof(wchar_t));
    mix(&f.size, sizeof(f.size));
    mix(&f.mtime, sizeof(f.mtime));
    mix(&tw, sizeof(tw));
    mix(&th, sizeof(th));
    mix(&ver, sizeof(ver));
    return Format(L"crop_%08x%08x.png", static_cast<unsigned>(h >> 32), static_cast<unsigned>(h & 0xFFFFFFFFu));
}

void TouchFile(const std::wstring& path) {              // mark as recently used
    UniqueFile h(::CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, 0, nullptr));
    if (!h) return;
    FILETIME ft;
    ::GetSystemTimeAsFileTime(&ft);
    ::SetFileTime(h.get(), nullptr, nullptr, &ft);
}

// Keep only the kMaxCrops most recently used crops, and sweep up temp files left by a crash.
void PruneCropCache(const std::wstring& dir) {
    struct Item { FILETIME t; std::wstring name; };
    std::vector<Item> items;
    WIN32_FIND_DATAW fd;
    {
        UniqueFind h(::FindFirstFileW(JoinPath(dir, L"crop_*.png").c_str(), &fd));
        if (h) {
            do { items.push_back({ fd.ftLastWriteTime, fd.cFileName }); } while (::FindNextFileW(h.get(), &fd));
        }
    }
    if (items.size() > static_cast<size_t>(kMaxCrops)) {
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return ::CompareFileTime(&a.t, &b.t) < 0; });
        for (size_t i = 0; i + kMaxCrops < items.size(); ++i) ::DeleteFileW(JoinPath(dir, items[i].name).c_str());
    }
    UniqueFind t(::FindFirstFileW(JoinPath(dir, L"crop_*.tmp").c_str(), &fd));
    if (t) {
        do { ::DeleteFileW(JoinPath(dir, fd.cFileName).c_str()); } while (::FindNextFileW(t.get(), &fd));
    }
}

// Face regions stored in the file's XMP (read from the first 256 KB).
std::vector<FaceRect> ReadFaceRegions(const std::wstring& path) {
    UniqueFile h(::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h) return {};
    std::string buf(kXmpReadBytes, '\0');
    DWORD got = 0;
    if (!Check(::ReadFile(h.get(), &buf[0], kXmpReadBytes, &got, nullptr), L"ReadFile(XMP)")) got = 0;
    buf.resize(got);
    return ParseFaceRegions(ExtractXmp(buf));
}

bool EncodePng(IWICImagingFactory* wic, IWICBitmapSource* source, const std::wstring& file) {
    ComPtr<IWICStream> stream;
    if (!Succeeded(wic->CreateStream(stream.Put()), L"CreateStream") ||
        !Succeeded(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE), L"IWICStream::InitializeFromFilename"))
        return false;
    ComPtr<IWICBitmapEncoder> enc;
    if (!Succeeded(wic->CreateEncoder(kContainerFormatPng, nullptr, enc.Put()), L"CreateEncoder(PNG)") ||
        !Succeeded(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache), L"IWICBitmapEncoder::Initialize"))
        return false;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    if (!Succeeded(enc->CreateNewFrame(frame.Put(), props.Put()), L"CreateNewFrame") ||
        !Succeeded(frame->Initialize(props.Get()), L"IWICBitmapFrameEncode::Initialize"))
        return false;
    UINT w = 0, h = 0;
    if (!Succeeded(source->GetSize(&w, &h), L"GetSize") || !Succeeded(frame->SetSize(w, h), L"SetSize")) return false;
    WICPixelFormatGUID fmt = kPixelFormat24bppBGR;
    if (!Succeeded(frame->SetPixelFormat(&fmt), L"SetPixelFormat")) return false;
    ComPtr<IWICFormatConverter> conv;
    if (!Succeeded(wic->CreateFormatConverter(conv.Put()), L"CreateFormatConverter") ||
        !Succeeded(conv->Initialize(source, fmt, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom),
                   L"IWICFormatConverter::Initialize"))
        return false;
    return Succeeded(frame->WriteSource(conv.Get(), nullptr), L"WriteSource") && Succeeded(frame->Commit(), L"Frame Commit") &&
           Succeeded(enc->Commit(), L"Encoder Commit") && Succeeded(stream->Commit(STGC_DEFAULT), L"Stream Commit");
}

}  // namespace

std::wstring MakeSmartCrop(const FileEntry& file, int tw, int th) {
    if (tw <= 0 || th <= 0) return std::wstring();
    const double aspect = static_cast<double>(tw) / th;

    // Already (nearly) the right shape? Nothing to do - and no decoding needed.
    if (file.ow > 0 && file.oh > 0 && std::fabs((static_cast<double>(file.ow) / file.oh) / aspect - 1.0) < 0.02)
        return std::wstring();

    const std::wstring dir = CropCacheDir();
    if (dir.empty()) return std::wstring();
    const std::wstring out = JoinPath(dir, CropFileName(file, tw, th));
    if (::GetFileAttributesW(out.c_str()) != INVALID_FILE_ATTRIBUTES) { TouchFile(out); return out; }

    IWICImagingFactory* wic = GetWic();
    if (!wic) return std::wstring();

    OrientedImage img;
    if (!img.Open(file.path)) return std::wstring();
    const UINT W = img.Width(), H = img.Height();
    if (std::fabs((static_cast<double>(W) / H) / aspect - 1.0) < 0.02) return std::wstring();

    // Small analysis bitmap (longest side about 320 px).
    const double k  = std::min(1.0, 320.0 / std::max(W, H));
    const UINT   aw = static_cast<UINT>(ClampInt(RoundToInt(W * k), 1, 4096));
    const UINT   ah = static_cast<UINT>(ClampInt(RoundToInt(H * k), 1, 4096));
    ComPtr<IWICBitmapScaler> small;
    ComPtr<IWICFormatConverter> bgr;
    if (!Succeeded(wic->CreateBitmapScaler(small.Put()), L"CreateBitmapScaler") ||
        !Succeeded(small->Initialize(img.Source(), aw, ah, WICBitmapInterpolationModeFant), L"Scaler::Initialize") ||
        !Succeeded(wic->CreateFormatConverter(bgr.Put()), L"CreateFormatConverter") ||
        !Succeeded(bgr->Initialize(small.Get(), kPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom), L"FormatConverter::Initialize"))
        return std::wstring();
    const UINT stride = ((aw * 3 + 3) / 4) * 4;
    std::vector<BYTE> pixels(static_cast<size_t>(stride) * ah);
    if (!Succeeded(bgr->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data()), L"CopyPixels(analysis)"))
        return std::wstring();

    const CropRect cr = ChooseCropRect(pixels.data(), static_cast<int>(stride), static_cast<int>(aw), static_cast<int>(ah),
                                       static_cast<int>(W), static_cast<int>(H), aspect, ReadFaceRegions(file.path));
    if (cr.w <= 0 || cr.h <= 0) return std::wstring();

    // Crop, and shrink if the crop is far larger than the display needs (keeps up to 2x
    // supersampling so Windows' final downscale stays crisp).
    WICRect rc = { cr.x, cr.y, cr.w, cr.h };
    ComPtr<IWICBitmapClipper> clip;
    if (!Succeeded(wic->CreateBitmapClipper(clip.Put()), L"CreateBitmapClipper") ||
        !Succeeded(clip->Initialize(img.Source(), &rc), L"Clipper::Initialize"))
        return std::wstring();
    IWICBitmapSource* result = clip.Get();
    ComPtr<IWICBitmapScaler> scaler;
    const double s = std::min(1.0, 2.0 * th / cr.h);
    if (s < 1.0) {
        const UINT ow = static_cast<UINT>(ClampInt(RoundToInt(cr.w * s), 1, 16384));
        const UINT oh = static_cast<UINT>(ClampInt(RoundToInt(cr.h * s), 1, 16384));
        if (!Succeeded(wic->CreateBitmapScaler(scaler.Put()), L"CreateBitmapScaler") ||
            !Succeeded(scaler->Initialize(clip.Get(), ow, oh, WICBitmapInterpolationModeFant), L"Scaler::Initialize"))
            return std::wstring();
        result = scaler.Get();
    }

    // Write to a temp file, then move into place, so a crash never leaves a half-written PNG
    // under the real name.
    const std::wstring tmp = out + L".tmp";
    bool ok = EncodePng(wic, result, tmp);                    // the stream is closed when EncodePng returns
    if (ok) ok = Check(::MoveFileExW(tmp.c_str(), out.c_str(), MOVEFILE_REPLACE_EXISTING), L"MoveFileExW(crop)");
    if (!ok) { ::DeleteFileW(tmp.c_str()); return std::wstring(); }
    PruneCropCache(dir);
    return out;
}

}  // namespace ws
