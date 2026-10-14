// Subject-aware crop selection. Pure computation on a small BGR bitmap plus face
// rectangles read from XMP metadata - no Windows types, fully unit-tested.
#pragma once

#include <string>
#include <vector>

namespace ws {

struct FaceRect { double x, y, w, h; };   // normalized 0..1, top-left origin
struct CropRect { int x, y, w, h; };      // pixels in the (orientation-corrected) source

// A face counts for this many "whole images" of detail when positioning the window.
constexpr double kFaceWeight = 2.0;

// The XMP packet inside a chunk of file bytes (empty if there is none).
std::string ExtractXmp(const std::string& buf);

// Face regions written by photo tools into XMP:
//   * MWG regions (Lightroom, digiKam, ...):  stArea:x/y/w/h, centre based, Type="Face"
//   * Microsoft people tags:                  MPReg:Rectangle="x, y, w, h", top-left based
// Coordinates are taken as relative to the orientation-corrected image. At most 16 faces;
// malformed or out-of-range rectangles are ignored.
std::vector<FaceRect> ParseFaceRegions(const std::string& xmp);

// Choose the crop window of the given aspect ratio (width / height) inside a W x H image,
// using the small analysis bitmap `bgr` (aw x ah, 24 bits per pixel, `stride` bytes per row)
// of that same image. The window spans the full length of the image's short side, so no
// resolution is thrown away; only its position along the other axis is chosen. Pixels are
// scored by detail (gradient), local contrast and a little colour saturation, faces pull
// the window towards them, and a mild centre bias makes flat images fall back to a centred
// crop. Invalid input yields the whole image.
CropRect ChooseCropRect(const unsigned char* bgr, int stride, int aw, int ah,
                        int W, int H, double aspect, const std::vector<FaceRect>& faces);

}  // namespace ws
