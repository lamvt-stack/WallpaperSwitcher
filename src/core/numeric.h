// Small numeric helpers shared by the portable core.
#pragma once

#include <climits>
#include <cmath>

namespace ws {

inline int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Index clamped to [0, count - 1] (count > 0).
inline int ClampIndex(int i, int count) { return ClampInt(i, 0, count - 1); }

// double -> int with saturation. std::lround() returns `long`, which is only 32 bits on
// Windows, so converting an out-of-range value (e.g. a scale factor derived from a hostile
// image size) would be undefined behaviour. NaN maps to 0.
inline int RoundToInt(double v) {
    if (!(v == v)) return 0;
    if (v >= static_cast<double>(INT_MAX)) return INT_MAX;
    if (v <= static_cast<double>(INT_MIN)) return INT_MIN;
    return static_cast<int>(std::llround(v));
}

}  // namespace ws
