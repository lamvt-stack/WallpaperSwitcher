#include "crop_core.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "numeric.h"

namespace ws {
namespace {

constexpr size_t kMaxFaces = 16;
constexpr int kMaxAnalysisDim = 4096;

// Per-pixel "interest": local gradient (detail / focus) + contrast against the surroundings
// + a little colour saturation. Each term is capped so a few very bright or very sharp
// pixels (a sun, a specular highlight) cannot dominate.
void ComputeScore(const unsigned char* bgr, int stride, int w, int h, std::vector<float>& score) {
    const size_t N = static_cast<size_t>(w) * static_cast<size_t>(h);
    std::vector<float> lum(N);
    std::vector<unsigned char> sat(N);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const unsigned char* p = bgr + static_cast<size_t>(y) * static_cast<size_t>(stride) + static_cast<size_t>(x) * 3;
            const float b = p[0], g = p[1], r = p[2];
            const int ib = p[0], ig = p[1], ir = p[2];
            const size_t i = static_cast<size_t>(y) * w + x;
            lum[i] = 0.114f * b + 0.587f * g + 0.299f * r;
            sat[i] = static_cast<unsigned char>(std::max(ir, std::max(ig, ib)) - std::min(ir, std::min(ig, ib)));
        }
    }

    // Integral image for fast box means.
    const size_t iw = static_cast<size_t>(w) + 1;
    std::vector<double> integ(iw * (static_cast<size_t>(h) + 1), 0.0);
    for (int y = 0; y < h; ++y) {
        double row = 0.0;
        for (int x = 0; x < w; ++x) {
            row += lum[static_cast<size_t>(y) * w + x];
            integ[static_cast<size_t>(y + 1) * iw + (x + 1)] = integ[static_cast<size_t>(y) * iw + (x + 1)] + row;
        }
    }
    const int radius = std::max(3, std::min(w, h) / 10);

    score.assign(N, 0.0f);
    for (int y = 0; y < h; ++y) {
        const int y0 = std::max(0, y - radius), y1 = std::min(h, y + radius + 1);
        for (int x = 0; x < w; ++x) {
            const int x0 = std::max(0, x - radius), x1 = std::min(w, x + radius + 1);
            const double sum = integ[static_cast<size_t>(y1) * iw + x1] - integ[static_cast<size_t>(y0) * iw + x1]
                             - integ[static_cast<size_t>(y1) * iw + x0] + integ[static_cast<size_t>(y0) * iw + x0];
            const double mean = sum / (static_cast<double>(x1 - x0) * (y1 - y0));
            const float  l    = lum[static_cast<size_t>(y) * w + x];
            const float contrast = static_cast<float>(std::fabs(l - mean));

            const float lx0 = lum[static_cast<size_t>(y) * w + std::max(0, x - 1)];
            const float lx1 = lum[static_cast<size_t>(y) * w + std::min(w - 1, x + 1)];
            const float ly0 = lum[static_cast<size_t>(std::max(0, y - 1)) * w + x];
            const float ly1 = lum[static_cast<size_t>(std::min(h - 1, y + 1)) * w + x];
            const float grad = std::fabs(lx1 - lx0) + std::fabs(ly1 - ly0);

            score[static_cast<size_t>(y) * w + x] =
                std::min(grad, 80.0f) + std::min(contrast, 80.0f) + 0.25f * sat[static_cast<size_t>(y) * w + x];
        }
    }
}

// Start index of the window of length `win` with the greatest total interest. A mild centre
// bias (at most -25% at the extremes) decides between near-equal candidates, e.g. on flat
// images, so the result falls back to a centred crop.
int BestWindow(const std::vector<double>& prof, int win) {
    const int len = static_cast<int>(prof.size());
    if (win >= len) return 0;
    std::vector<double> pre(static_cast<size_t>(len) + 1, 0.0);
    for (int i = 0; i < len; ++i) pre[static_cast<size_t>(i) + 1] = pre[static_cast<size_t>(i)] + prof[static_cast<size_t>(i)];
    int best = 0;
    double bestVal = -1.0;
    for (int x = 0; x + win <= len; ++x) {
        const double sum = pre[static_cast<size_t>(x + win)] - pre[static_cast<size_t>(x)];
        const double d = ((x + win * 0.5) - len * 0.5) / (len * 0.5);
        const double v = sum * (1.0 - 0.25 * d * d);
        if (v > bestVal) { bestVal = v; best = x; }
    }
    return best;
}

// Find `name` between [from, to) and return its value, written either as an attribute
// (name="v") or as an element (<name>v</name>).
bool XmpValue(const std::string& s, size_t from, size_t to, const char* name, std::string& out) {
    const size_t nlen = std::strlen(name);
    to = std::min(to, s.size());
    size_t p = s.find(name, from);
    while (p != std::string::npos && p < to) {
        const size_t q = p + nlen;
        if (q + 1 < to && s[q] == '=' && (s[q + 1] == '"' || s[q + 1] == '\'')) {
            const size_t e = s.find(s[q + 1], q + 2);
            if (e != std::string::npos && e <= to) { out = s.substr(q + 2, e - q - 2); return true; }
        } else if (q < to && s[q] == '>') {
            const size_t e = s.find('<', q + 1);
            if (e != std::string::npos && e <= to) { out = s.substr(q + 1, e - q - 1); return true; }
        }
        p = s.find(name, p + 1);
    }
    return false;
}

bool ParseNumber(const char*& p, double& out) {
    while (*p == ' ' || *p == ',' || *p == '\t') ++p;
    char* e = nullptr;
    out = std::strtod(p, &e);
    if (e == p) return false;
    p = e;
    return true;
}

bool AddFace(std::vector<FaceRect>& out, double x, double y, double w, double h) {
    // Written so that NaN fails every comparison and is rejected.
    if (!(w > 0.0 && h > 0.0 && w <= 1.001 && h <= 1.001)) return false;
    if (!(x == x) || !(y == y)) return false;
    FaceRect f;
    f.x = std::min(1.0, std::max(0.0, x));
    f.y = std::min(1.0, std::max(0.0, y));
    f.w = std::min(w, 1.0 - f.x);
    f.h = std::min(h, 1.0 - f.y);
    if (f.w <= 0.0 || f.h <= 0.0) return false;
    out.push_back(f);
    return true;
}

}  // namespace

std::string ExtractXmp(const std::string& buf) {
    const size_t b = buf.find("<x:xmpmeta");
    if (b == std::string::npos) return std::string();
    const size_t e = buf.find("</x:xmpmeta>", b);
    return e == std::string::npos ? buf.substr(b) : buf.substr(b, e + 12 - b);
}

std::vector<FaceRect> ParseFaceRegions(const std::string& xmp) {
    std::vector<FaceRect> faces;

    // MWG regions
    {
        const std::string key = "stArea:w";
        size_t pos = 0, p;
        while (faces.size() < kMaxFaces && (p = xmp.find(key, pos)) != std::string::npos) {
            pos = p + key.size();
            if (p > 0 && xmp[p - 1] == '/') continue;                    // closing tag
            size_t cs = xmp.rfind("<rdf:li", p);
            if (cs == std::string::npos || p - cs > 1500) cs = p > 600 ? p - 600 : 0;
            size_t ce = xmp.find("<rdf:li", p);
            if (ce == std::string::npos || ce - p > 1500) ce = std::min(xmp.size(), p + 1500);

            std::string sx, sy, sw, sh, type;
            if (!XmpValue(xmp, cs, ce, "stArea:x", sx) || !XmpValue(xmp, cs, ce, "stArea:y", sy) ||
                !XmpValue(xmp, cs, ce, "stArea:w", sw) || !XmpValue(xmp, cs, ce, "stArea:h", sh))
                continue;
            if (XmpValue(xmp, cs, ce, "mwg-rs:Type", type) && type != "Face") continue;   // pets, barcodes, focus points
            const double cx = std::strtod(sx.c_str(), nullptr), cy = std::strtod(sy.c_str(), nullptr);
            const double w  = std::strtod(sw.c_str(), nullptr), h  = std::strtod(sh.c_str(), nullptr);
            AddFace(faces, cx - w / 2.0, cy - h / 2.0, w, h);
        }
    }
    // Microsoft people tags
    {
        const std::string key = "MPReg:Rectangle";
        size_t pos = 0, p;
        while (faces.size() < kMaxFaces && (p = xmp.find(key, pos)) != std::string::npos) {
            pos = p + key.size();
            if (p > 0 && xmp[p - 1] == '/') continue;
            std::string val;
            if (!XmpValue(xmp, p, std::min(xmp.size(), p + 300), key.c_str(), val)) continue;
            const char* c = val.c_str();
            double x, y, w, h;
            if (ParseNumber(c, x) && ParseNumber(c, y) && ParseNumber(c, w) && ParseNumber(c, h))
                AddFace(faces, x, y, w, h);
        }
    }
    return faces;
}

CropRect ChooseCropRect(const unsigned char* bgr, int stride, int aw, int ah,
                        int W, int H, double aspect, const std::vector<FaceRect>& faces) {
    CropRect r = { 0, 0, W, H };
    if (!bgr || W <= 0 || H <= 0 || aw <= 0 || ah <= 0 || aw > kMaxAnalysisDim || ah > kMaxAnalysisDim ||
        stride < aw * 3 || !(aspect > 0.0))
        return r;

    int cw = W, ch = H;
    if (static_cast<double>(W) / H > aspect) cw = ClampInt(RoundToInt(H * aspect), 1, W);
    else                                     ch = ClampInt(RoundToInt(W / aspect), 1, H);
    r.w = cw;
    r.h = ch;
    if (cw == W && ch == H) return r;

    std::vector<float> score;
    ComputeScore(bgr, stride, aw, ah, score);

    const bool horiz = cw < W;                 // slide the window left-right, else up-down
    const int  len   = horiz ? aw : ah;
    std::vector<double> prof(static_cast<size_t>(len), 0.0);
    double total = 0.0;
    for (int y = 0; y < ah; ++y) {
        for (int x = 0; x < aw; ++x) {
            const double s = score[static_cast<size_t>(y) * aw + x];
            prof[static_cast<size_t>(horiz ? x : y)] += s;
            total += s;
        }
    }
    const double eps = 1e-3 + total * 1e-6 / len;     // lets the centre bias decide on flat images
    for (double& v : prof) v += eps;

    for (const FaceRect& f : faces) {
        const double lo   = horiz ? f.x : f.y;
        const double span = horiz ? f.w : f.h;
        const int i0 = ClampInt(RoundToInt(std::floor(lo * len)), 0, len - 1);
        const int i1 = ClampInt(RoundToInt(std::ceil((lo + span) * len)), i0 + 1, len);
        const double mass = total * kFaceWeight / (i1 - i0);
        for (int i = i0; i < i1; ++i) prof[static_cast<size_t>(i)] += mass;
    }

    const int full = horiz ? W : H;
    const int win  = horiz ? cw : ch;
    const int winA = ClampInt(RoundToInt(win * static_cast<double>(len) / full), 1, len);
    const int best = BestWindow(prof, winA);
    const double centre = (best + winA * 0.5) * full / len;
    const int off = ClampInt(RoundToInt(centre - win * 0.5), 0, full - win);
    if (horiz) r.x = off; else r.y = off;
    return r;
}

}  // namespace ws
