// Cross-fade timing curve and desktop picture placement. Pure functions, no Windows types.
#pragma once

#include <algorithm>
#include <cmath>

#include "numeric.h"

namespace ws {

// 500 ms is long enough to read as a dissolve rather than a flicker and short enough not to
// feel sluggish (inside the 400-600 ms comfort window).
constexpr double kFadeDurationMs = 500.0;

// Opacity (0..255) of the OLD picture `elapsedMs` into a fade of `durMs`.
// Smoothstep easing: starts and ends gently, so the dissolve has no visible "kick".
inline int FadeAlpha(double elapsedMs, double durMs) {
    if (durMs <= 0.0 || elapsedMs >= durMs) return 0;
    if (elapsedMs <= 0.0) return 255;
    const double x = elapsedMs / durMs;
    const double e = x * x * (3.0 - 2.0 * x);
    return ClampInt(RoundToInt(255.0 * (1.0 - e)), 0, 255);
}

// How the desktop lays a picture out on a W x H display.
enum class PlaceMode { Fill, Fit, Stretch, Center, Tile };

// Scaled picture size and top-left corner (negative = cropped by the display edge).
struct Placement { int dw, dh, dx, dy; };

// Scaled sizes are capped so a hostile image shape (say 1 x 2,000,000,000) can never turn
// into an enormous bitmap or an int overflow further down the pipeline.
constexpr int kMaxRenderDim = 32768;

inline Placement ComputePlacement(PlaceMode m, int W, int H, int iw, int ih) {
    W = std::max(W, 1);
    H = std::max(H, 1);
    iw = std::max(iw, 1);
    ih = std::max(ih, 1);
    Placement p = { iw, ih, 0, 0 };
    if (m == PlaceMode::Fill || m == PlaceMode::Fit) {
        const double sx = static_cast<double>(W) / iw, sy = static_cast<double>(H) / ih;
        const double s = (m == PlaceMode::Fit) ? std::min(sx, sy) : std::max(sx, sy);
        p.dw = ClampInt(RoundToInt(iw * s), 1, kMaxRenderDim);
        p.dh = ClampInt(RoundToInt(ih * s), 1, kMaxRenderDim);
    } else if (m == PlaceMode::Stretch) {
        p.dw = W;
        p.dh = H;
    }
    p.dw = ClampInt(p.dw, 1, kMaxRenderDim);
    p.dh = ClampInt(p.dh, 1, kMaxRenderDim);
    if (m != PlaceMode::Tile) {
        p.dx = static_cast<int>(std::floor((static_cast<double>(W) - p.dw) / 2.0));
        p.dy = static_cast<int>(std::floor((static_cast<double>(H) - p.dh) / 2.0));
    }
    return p;
}

}  // namespace ws
