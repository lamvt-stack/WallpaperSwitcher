#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "check.h"
#include "core/crop_core.h"

using namespace ws;

struct Img {
    int w, h, stride;
    std::vector<unsigned char> d;
};
static Img MakeImg(int w, int h, unsigned char gray) {
    Img i;
    i.w = w;
    i.h = h;
    i.stride = ((w * 3 + 3) / 4) * 4;
    i.d.assign(static_cast<size_t>(i.stride) * h, gray);
    return i;
}
// High-detail texture patch.
static void Blob(Img& im, int x0, int x1, int y0, int y1) {
    unsigned seed = 12345;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            seed = seed * 1664525u + 1013904223u;
            const unsigned char v = static_cast<unsigned char>(seed >> 24);
            unsigned char* p = &im.d[static_cast<size_t>(y) * im.stride + x * 3];
            p[0] = v;
            p[1] = static_cast<unsigned char>(255 - v);
            p[2] = static_cast<unsigned char>(v / 2);
        }
}
static CropRect Crop(const Img& im, int W, int H, double aspect, const std::vector<FaceRect>& faces = {}) {
    return ChooseCropRect(im.d.data(), im.stride, im.w, im.h, W, H, aspect, faces);
}

int main() {
    const double A = 9.0 / 16.0;
    const int W = 1920, H = 1080, aw = 320, ah = 180;

    {
        Img im = MakeImg(aw, ah, 128);
        const CropRect r = Crop(im, W, H, A);
        CHECK(std::abs(r.x + r.w / 2 - W / 2) < 20, "flat image falls back to a centred crop");
        CHECK(r.h == H && r.w == 608, "crop keeps full height, width = H * aspect");
    }
    {
        Img im = MakeImg(aw, ah, 128);
        Blob(im, 235, 275, 50, 130);
        const CropRect r = Crop(im, W, H, A);
        CHECK(r.x <= 1410 && r.x + r.w >= 1650, "crop window contains the detailed region (right side)");
    }
    {
        Img im = MakeImg(aw, ah, 128);
        Blob(im, 30, 70, 40, 140);
        const CropRect r = Crop(im, W, H, A);
        CHECK(r.x <= 180 && r.x + r.w >= 420, "crop window contains the detailed region (left side)");
    }
    {
        Img im = MakeImg(aw, ah, 128);
        Blob(im, 235, 285, 30, 150);
        const std::vector<FaceRect> faces = { { 0.12, 0.25, 0.08, 0.18 } };
        const CropRect r = Crop(im, W, H, A, faces);
        CHECK(r.x <= 230 && r.x + r.w >= 384, "a face region wins over a detailed area elsewhere");
        const CropRect r2 = Crop(im, W, H, A);
        CHECK(r2.x > r.x, "without the face the same image crops to the right instead");
    }
    {
        Img im = MakeImg(200, 300, 128);
        Blob(im, 140, 190, 100, 200);
        const CropRect r = ChooseCropRect(im.d.data(), im.stride, 200, 300, 1200, 1800, A, {});
        CHECK(r.h == 1800 && r.w == 1013 && r.x + r.w >= 1140, "wider-than-target portrait is cropped horizontally toward the detail");
    }
    {
        Img im = MakeImg(90, 200, 128);
        Blob(im, 20, 70, 150, 190);
        const CropRect r = ChooseCropRect(im.d.data(), im.stride, 90, 200, 900, 2000, A, {});
        CHECK(r.w == 900 && r.y + r.h >= 1900, "narrower-than-target image is cropped vertically toward the detail");
    }
    {
        Img im = MakeImg(90, 160, 128);
        const CropRect r = ChooseCropRect(im.d.data(), im.stride, 90, 160, 1080, 1920, A, {});
        CHECK(r.x == 0 && r.y == 0 && r.w == 1080 && r.h == 1920, "matching aspect returns the whole image");
    }

    // ---- invalid input never crashes and returns the whole image ----
    {
        Img im = MakeImg(aw, ah, 128);
        CropRect r = ChooseCropRect(nullptr, 0, aw, ah, W, H, A, {});
        CHECK(r.x == 0 && r.w == W, "null bitmap is rejected");
        r = ChooseCropRect(im.d.data(), 4, aw, ah, W, H, A, {});
        CHECK(r.x == 0 && r.w == W, "stride smaller than a row is rejected");
        r = ChooseCropRect(im.d.data(), im.stride, 100000, ah, W, H, A, {});
        CHECK(r.x == 0 && r.w == W, "oversized analysis bitmap is rejected");
        r = ChooseCropRect(im.d.data(), im.stride, aw, ah, W, H, 0.0, {});
        CHECK(r.x == 0 && r.w == W, "non-positive aspect is rejected");
        r = ChooseCropRect(im.d.data(), im.stride, aw, ah, 2000000000, 1, 0.5625, {});
        CHECK(r.w >= 1 && r.h >= 1, "extreme source size does not overflow");
    }

    // ---- XMP face regions ----
    {
        const std::string mwg =
            "junk<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF><rdf:Description><mwg-rs:Regions><mwg-rs:RegionList><rdf:Bag>"
            "<rdf:li><rdf:Description mwg-rs:Name=\"A\" mwg-rs:Type=\"Face\"><mwg-rs:Area stArea:x=\"0.25\" stArea:y=\"0.4\" stArea:w=\"0.1\" stArea:h=\"0.2\" stArea:unit=\"normalized\"/></rdf:Description></rdf:li>"
            "<rdf:li><rdf:Description mwg-rs:Name=\"Rex\" mwg-rs:Type=\"Pet\"><mwg-rs:Area stArea:x=\"0.8\" stArea:y=\"0.5\" stArea:w=\"0.2\" stArea:h=\"0.2\"/></rdf:Description></rdf:li>"
            "</rdf:Bag></mwg-rs:RegionList></mwg-rs:Regions></rdf:Description></rdf:RDF></x:xmpmeta>tail";
        const auto f = ParseFaceRegions(ExtractXmp(mwg));
        CHECK(f.size() == 1, "MWG: one face, pet region ignored");
        if (f.size() == 1)
            CHECK(std::fabs(f[0].x - 0.20) < 1e-9 && std::fabs(f[0].y - 0.30) < 1e-9 && std::fabs(f[0].w - 0.1) < 1e-9,
                  "MWG: centre-based area converted to top-left");
    }
    {
        const std::string mp =
            "<x:xmpmeta><rdf:RDF><rdf:Description><MPRI:Regions><rdf:Bag><rdf:li><rdf:Description MPReg:PersonDisplayName=\"B\" MPReg:Rectangle=\"0.5, 0.2, 0.1, 0.15\"/></rdf:li></rdf:Bag></MPRI:Regions></rdf:Description></rdf:RDF></x:xmpmeta>";
        const auto f = ParseFaceRegions(ExtractXmp(mp));
        CHECK(f.size() == 1 && std::fabs(f[0].x - 0.5) < 1e-9 && std::fabs(f[0].h - 0.15) < 1e-9,
              "Microsoft people tag parsed (top-left based)");
    }
    {
        const std::string el =
            "<x:xmpmeta><mwg-rs:Area><stArea:x>0.5</stArea:x><stArea:y>0.5</stArea:y><stArea:w>0.2</stArea:w><stArea:h>0.2</stArea:h></mwg-rs:Area></x:xmpmeta>";
        const auto f = ParseFaceRegions(ExtractXmp(el));
        CHECK(f.size() == 1 && std::fabs(f[0].x - 0.4) < 1e-9, "MWG element form parsed once (closing tag not double counted)");
    }
    CHECK(ParseFaceRegions(ExtractXmp("no xmp here")).empty(), "no XMP -> no faces");
    {
        // hostile values: NaN / inf / huge / negative must be rejected, count capped at 16
        const std::string bad =
            "<x:xmpmeta><rdf:li><mwg-rs:Area stArea:x=\"nan\" stArea:y=\"0.5\" stArea:w=\"0.2\" stArea:h=\"0.2\"/></rdf:li>"
            "<rdf:li><mwg-rs:Area stArea:x=\"0.5\" stArea:y=\"0.5\" stArea:w=\"inf\" stArea:h=\"0.2\"/></rdf:li>"
            "<rdf:li><mwg-rs:Area stArea:x=\"0.5\" stArea:y=\"0.5\" stArea:w=\"-1\" stArea:h=\"0.2\"/></rdf:li>"
            "<rdf:li><mwg-rs:Area stArea:x=\"0.5\" stArea:y=\"0.5\" stArea:w=\"9e99\" stArea:h=\"0.2\"/></rdf:li></x:xmpmeta>";
        CHECK(ParseFaceRegions(ExtractXmp(bad)).empty(), "NaN / inf / negative / huge rectangles are rejected");
        std::string many = "<x:xmpmeta>";
        for (int i = 0; i < 100; ++i)
            many += "<rdf:li><mwg-rs:Area stArea:x=\"0.5\" stArea:y=\"0.5\" stArea:w=\"0.1\" stArea:h=\"0.1\"/></rdf:li>";
        many += "</x:xmpmeta>";
        CHECK(ParseFaceRegions(ExtractXmp(many)).size() == 16, "face count is capped at 16");
        CHECK(ParseFaceRegions(std::string(300000, '<')).empty(), "garbage input is handled");
    }
    return Report();
}
