#include <algorithm>
#include <climits>
#include <cmath>

#include "check.h"
#include "core/fade_math.h"
#include "core/numeric.h"

using namespace ws;

int main() {
    // ---- fade curve ----
    CHECK(kFadeDurationMs >= 400.0 && kFadeDurationMs <= 600.0, "duration is inside the 400-600 ms window");
    CHECK(FadeAlpha(0, 500) == 255 && FadeAlpha(-5, 500) == 255, "starts fully opaque (old picture)");
    CHECK(FadeAlpha(500, 500) == 0 && FadeAlpha(900, 500) == 0, "ends fully transparent and stays there");
    const int mid = FadeAlpha(250, 500);
    CHECK(mid >= 127 && mid <= 128, "halfway point is 50% opacity");
    bool mono = true;
    int prev = 256;
    for (int ms = 0; ms <= 500; ++ms) {
        const int a = FadeAlpha(ms, 500);
        if (a > prev) mono = false;
        prev = a;
    }
    CHECK(mono, "opacity never increases (no flicker)");
    const int s0 = FadeAlpha(0, 500) - FadeAlpha(16.7, 500);
    const int sM = FadeAlpha(241.6, 500) - FadeAlpha(258.3, 500);
    const int sE = FadeAlpha(483.3, 500) - FadeAlpha(500, 500);
    CHECK(s0 <= 3 && sE <= 3 && sM >= 10 && sM <= 14, "gentle start/end, fastest in the middle at 60 Hz");
    int frames = 0;
    for (double t = 0; FadeAlpha(t, 500) > 0; t += 1000.0 / 60.0) ++frames;
    CHECK(frames >= 29 && frames <= 31, "about 30 distinct frames at 60 Hz");
    CHECK(FadeAlpha(10, 0) == 0 && FadeAlpha(10, -1) == 0, "degenerate duration means no fade");

    // ---- placement ----
    {
        const Placement p = ComputePlacement(PlaceMode::Fill, 1080, 1920, 1920, 1080);
        CHECK(p.dh == 1920 && p.dw == 3413 && p.dx <= 0 && p.dx + p.dw >= 1080 && p.dy == 0,
              "Fill covers the screen, centred, overflow cropped");
    }
    {
        const Placement p = ComputePlacement(PlaceMode::Fit, 1920, 1080, 1000, 1000);
        CHECK(p.dw == 1080 && p.dh == 1080 && p.dx == 420 && p.dy == 0, "Fit keeps the whole picture, centred");
    }
    {
        const Placement p = ComputePlacement(PlaceMode::Stretch, 1920, 1080, 100, 50);
        CHECK(p.dw == 1920 && p.dh == 1080 && p.dx == 0 && p.dy == 0, "Stretch fills exactly");
    }
    {
        const Placement p = ComputePlacement(PlaceMode::Center, 1920, 1080, 800, 600);
        CHECK(p.dw == 800 && p.dh == 600 && p.dx == 560 && p.dy == 240, "Center keeps native size, centred");
    }
    {
        const Placement p = ComputePlacement(PlaceMode::Center, 1000, 1000, 1999, 1001);
        CHECK(p.dx == -500 && p.dy == -1, "Center of a larger picture crops symmetrically (floor division)");
    }
    {
        const Placement p = ComputePlacement(PlaceMode::Tile, 1920, 1080, 256, 256);
        CHECK(p.dw == 256 && p.dh == 256 && p.dx == 0 && p.dy == 0, "Tile starts at the top-left at native size");
    }

    // ---- hostile sizes must not overflow or explode ----
    {
        const Placement p = ComputePlacement(PlaceMode::Fill, 3840, 2160, 1, 2000000000);
        CHECK(p.dw >= 1 && p.dw <= kMaxRenderDim && p.dh >= 1 && p.dh <= kMaxRenderDim, "1 x 2,000,000,000 image stays within the render cap");
        const Placement q = ComputePlacement(PlaceMode::Fill, 3840, 2160, 0, -5);
        CHECK(q.dw >= 1 && q.dh >= 1, "zero / negative image size is sanitised");
        const Placement z = ComputePlacement(PlaceMode::Fit, 0, 0, 100, 100);
        CHECK(z.dw >= 1 && z.dh >= 1, "zero display size is sanitised");
    }

    // ---- RoundToInt ----
    CHECK(RoundToInt(2.5) == 3 && RoundToInt(-2.5) == -3 && RoundToInt(0.49) == 0, "RoundToInt rounds half away from zero");
    CHECK(RoundToInt(1e30) == INT_MAX && RoundToInt(-1e30) == INT_MIN, "RoundToInt saturates instead of overflowing");
    const double nan = std::nan("");
    CHECK(RoundToInt(nan) == 0, "RoundToInt maps NaN to 0");
    return Report();
}
