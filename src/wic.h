// Windows Imaging Component helpers: shared factory, EXIF orientation, header-only probing
// and an orientation-corrected image source.
#pragma once

#include <wincodec.h>

#include <string>

#include "core/playlist.h"
#include "win_util.h"

namespace ws {

// WIC identifiers declared here (no dependency on uuid.lib / windowscodecs.lib).
inline const GUID kClsidWicFactory   = { 0xCACAF262, 0x9370, 0x4615, { 0xA1, 0x3B, 0x9F, 0x55, 0x39, 0xDA, 0x4C, 0x0A } };
inline const GUID kIidWicFactory     = { 0xEC5EC8A9, 0xC395, 0x4314, { 0x9C, 0x77, 0x54, 0xD7, 0xA9, 0x35, 0xFF, 0x70 } };
inline const GUID kPixelFormat24bppBGR = { 0x6FDDC324, 0x4E03, 0x4BFE, { 0xB1, 0x85, 0x3D, 0x77, 0x76, 0x8D, 0xC9, 0x0C } };
inline const GUID kPixelFormat32bppBGR = { 0x6FDDC324, 0x4E03, 0x4BFE, { 0xB1, 0x85, 0x3D, 0x77, 0x76, 0x8D, 0xC9, 0x0E } };
inline const GUID kContainerFormatPng  = { 0x1B7CFAF4, 0x713F, 0x473C, { 0xBB, 0xCD, 0x61, 0x37, 0x42, 0x5F, 0xAE, 0xAF } };

// The shared WIC factory (created on first use; UI thread only). nullptr if unavailable.
IWICImagingFactory* GetWic();
void ReleaseWic();

// EXIF Orientation (1..8) of a decoded frame, or 0 when absent / unreadable.
int FrameOrientation(IWICBitmapFrameDecode* frame);
WICBitmapTransformOptions ExifToTransform(int exifOrientation);

struct ImageProbe {
    int orient = kOrientUnusable;   // Orient value
    int ow = 0, oh = 0;             // orientation-corrected pixel size
};

// Orientation of the image as it is meant to be *viewed*: EXIF tag when present (values 5-8
// swap width and height), otherwise the stored pixel size. Square counts as landscape. Only
// the header / metadata is read, never the pixels. Returns kOrientBoth if WIC is unavailable.
ImageProbe ProbeImage(const std::wstring& path);

// A decoded first frame with the EXIF rotation applied.
class OrientedImage {
public:
    bool Open(const std::wstring& path);
    IWICBitmapSource* Source() const { return source_; }
    UINT Width() const { return width_; }
    UINT Height() const { return height_; }

private:
    ComPtr<IWICBitmapDecoder>     decoder_;
    ComPtr<IWICBitmapFrameDecode> frame_;
    ComPtr<IWICBitmapFlipRotator> rotator_;
    IWICBitmapSource*             source_ = nullptr;   // points at frame_ or rotator_
    UINT                          width_ = 0, height_ = 0;
};

}  // namespace ws
