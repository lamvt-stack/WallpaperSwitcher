#include "wic.h"

#include <algorithm>

namespace ws {
namespace {

IWICImagingFactory* g_wic = nullptr;
bool g_wicTried = false;

// EXIF Orientation (tag 0x0112 = 274) as exposed by the WIC metadata readers.
int ReadExifOrientation(IWICMetadataQueryReader* reader) {
    static const wchar_t* const kQueries[] = {
        L"/app1/ifd/{ushort=274}",     // JPEG
        L"/ifd/{ushort=274}",          // TIFF and friends
        L"System.Photo.Orientation",   // Windows photo metadata policy
    };
    for (const wchar_t* q : kQueries) {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        int value = 0;
        if (SUCCEEDED(reader->GetMetadataByName(q, &pv))) {
            switch (pv.vt) {
            case VT_UI2: value = pv.uiVal; break;
            case VT_I2:  value = pv.iVal;  break;
            case VT_UI1: value = pv.bVal;  break;
            case VT_UI4: value = pv.ulVal <= 255 ? static_cast<int>(pv.ulVal) : 0; break;
            default: break;
            }
        }
        PropVariantClear(&pv);
        if (value >= 1 && value <= 8) return value;
    }
    return 0;
}

}  // namespace

IWICImagingFactory* GetWic() {
    if (!g_wic && !g_wicTried) {
        g_wicTried = true;
        const HRESULT hr = ::CoCreateInstance(kClsidWicFactory, nullptr, CLSCTX_INPROC_SERVER, kIidWicFactory,
                                              reinterpret_cast<void**>(&g_wic));
        if (!Succeeded(hr, L"CoCreateInstance(WICImagingFactory)")) g_wic = nullptr;
    }
    return g_wic;
}

void ReleaseWic() {
    if (g_wic) { g_wic->Release(); g_wic = nullptr; }
}

int FrameOrientation(IWICBitmapFrameDecode* frame) {
    ComPtr<IWICMetadataQueryReader> reader;
    if (FAILED(frame->GetMetadataQueryReader(reader.Put())) || !reader) return 0;
    return ReadExifOrientation(reader.Get());
}

WICBitmapTransformOptions ExifToTransform(int o) {
    switch (o) {
    case 2: return WICBitmapTransformFlipHorizontal;
    case 3: return WICBitmapTransformRotate180;
    case 4: return WICBitmapTransformFlipVertical;
    case 5: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
    case 6: return WICBitmapTransformRotate90;
    case 7: return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
    case 8: return WICBitmapTransformRotate270;
    default: return WICBitmapTransformRotate0;
    }
}

ImageProbe ProbeImage(const std::wstring& path) {
    ImageProbe out;
    IWICImagingFactory* wic = GetWic();
    if (!wic) { out.orient = kOrientBoth; return out; }

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                              decoder.Put())) || !decoder)
        return out;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.Put())) || !frame) return out;

    UINT w = 0, h = 0;
    if (FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0 || w > static_cast<UINT>(INT_MAX) || h > static_cast<UINT>(INT_MAX))
        return out;
    const int o = FrameOrientation(frame.Get());
    if (o >= 5 && o <= 8) std::swap(w, h);
    out.ow = static_cast<int>(w);
    out.oh = static_cast<int>(h);
    out.orient = (w >= h) ? kOrientLandscape : kOrientPortrait;
    return out;
}

bool OrientedImage::Open(const std::wstring& path) {
    IWICImagingFactory* wic = GetWic();
    if (!wic) return false;
    if (!Succeeded(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                  decoder_.Put()), L"CreateDecoderFromFilename") ||
        !Succeeded(decoder_->GetFrame(0, frame_.Put()), L"IWICBitmapDecoder::GetFrame"))
        return false;

    source_ = frame_.Get();
    const int exif = FrameOrientation(frame_.Get());
    if (exif >= 2 && exif <= 8) {
        if (!Succeeded(wic->CreateBitmapFlipRotator(rotator_.Put()), L"CreateBitmapFlipRotator") ||
            !Succeeded(rotator_->Initialize(frame_.Get(), ExifToTransform(exif)), L"IWICBitmapFlipRotator::Initialize"))
            return false;
        source_ = rotator_.Get();
    }
    if (!Succeeded(source_->GetSize(&width_, &height_), L"IWICBitmapSource::GetSize") || width_ == 0 || height_ == 0 ||
        width_ > static_cast<UINT>(INT_MAX) || height_ > static_cast<UINT>(INT_MAX))
        return false;
    return true;
}

}  // namespace ws
