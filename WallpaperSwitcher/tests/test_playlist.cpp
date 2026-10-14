#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "check.h"
#include "core/playlist.h"

using namespace ws;

// A folder whose true orientations are known to the test; classification is "reading the file".
struct Folder {
    std::vector<int> truth;
    int reads = 0;
    Playlist list;

    explicit Folder(std::vector<int> orients, bool shuffle = false, bool preclassify = false, bool smartCrop = true)
        : truth(std::move(orients)),
          list([this](FileEntry& f) { ++reads; f.orient = static_cast<signed char>(truth[static_cast<size_t>(f.size)]); }, 12345u) {
        std::vector<FileEntry> files(truth.size());
        for (size_t i = 0; i < files.size(); ++i) {
            files[i].path = L"f" + std::to_wstring(i);
            files[i].size = i;                                   // index back into `truth`
            if (preclassify) files[i].orient = static_cast<signed char>(truth[i]);
        }
        list.SetSmartCrop(smartCrop);
        list.SetFiles(std::move(files), shuffle);
    }
};

static Slot Mk(int orient, int key) {
    Slot s;
    s.orient = orient;
    s.key = key;
    return s;
}

struct Outcome {
    std::map<int, int> shown;      // slot key -> file index
    RunResult res;
};
static Outcome Go(Folder& f, const std::vector<Slot>& slots, int dir, int only = -1, const std::set<int>& failFiles = {}) {
    Outcome o;
    auto apply = [&](const Slot& s, const FileEntry& fe) {
        const int idx = static_cast<int>(fe.size);
        if (failFiles.count(idx)) return false;
        o.shown[s.key] = idx;
        return true;
    };
    o.res = f.list.Run(slots, dir, only, apply);
    return o;
}
static bool Distinct(const Outcome& o) {
    std::set<int> u;
    for (const auto& kv : o.shown) u.insert(kv.second);
    return u.size() == o.shown.size();
}

int main() {
    // The reported bug: landscape + portrait monitor, "same image" mode, landscape-only folder.
    {
        Folder f(std::vector<int>(12, kOrientLandscape));
        const std::vector<Slot> slots = { Mk(kOrientLandscape, 0), Mk(kOrientPortrait, 1) };
        const Outcome r = Go(f, slots, +1);
        CHECK(r.shown.size() == 2 && Distinct(r), "startup: portrait monitor gets a different image than the landscape monitor");
        bool ok = true;
        for (int i = 0; i < 40; ++i) { const Outcome q = Go(f, slots, +1); ok = ok && q.shown.size() == 2 && Distinct(q); }
        CHECK(ok, "40 consecutive Next steps: monitors never show the same image");
        ok = true;
        for (int i = 0; i < 40; ++i) { const Outcome q = Go(f, slots, -1); ok = ok && q.shown.size() == 2 && Distinct(q); }
        CHECK(ok, "40 consecutive Previous steps: monitors never show the same image");
    }
    // Mixed folder, "different image per monitor" with 3 monitors (L, P, L).
    {
        std::vector<int> o;
        for (int i = 0; i < 30; ++i) o.push_back(i % 4 == 3 ? kOrientPortrait : kOrientLandscape);
        Folder f(o, true);
        const std::vector<Slot> slots = { Mk(kOrientLandscape, 3), Mk(kOrientPortrait, 4), Mk(kOrientLandscape, 5) };
        const Outcome r = Go(f, slots, +1);
        CHECK(r.shown.size() == 3 && Distinct(r), "3 monitors (L,P,L): all different at startup");
        bool ok = true, landOk = true;
        for (int i = 0; i < 60; ++i) {
            const Outcome q = Go(f, slots, i % 7 == 6 ? -1 : +1);
            ok = ok && q.shown.size() == 3 && Distinct(q);
            landOk = landOk && f.truth[static_cast<size_t>(q.shown.at(3))] == kOrientLandscape &&
                     f.truth[static_cast<size_t>(q.shown.at(5))] == kOrientLandscape;
        }
        CHECK(ok, "60 mixed Next/Previous steps stay all-different");
        CHECK(landOk, "landscape monitors only ever get landscape images");
    }
    // "Next on monitor N" only changes that monitor and never duplicates another.
    {
        Folder f(std::vector<int>(20, kOrientLandscape));
        const std::vector<Slot> slots = { Mk(kOrientLandscape, 3), Mk(kOrientPortrait, 4), Mk(kOrientLandscape, 5) };
        Go(f, slots, +1);
        bool ok = true, others = true;
        for (int i = 0; i < 30; ++i) {
            const int b0 = f.list.CursorFile(3), b1 = f.list.CursorFile(4), b2 = f.list.CursorFile(5);
            const Outcome q = Go(f, slots, +1, 1);
            const int a0 = f.list.CursorFile(3), a1 = f.list.CursorFile(4), a2 = f.list.CursorFile(5);
            others = others && b0 == a0 && b2 == a2 && b1 != a1;
            ok = ok && a1 != a0 && a1 != a2 && q.shown.size() == 1;
        }
        CHECK(others, "per-monitor Next changes only the chosen monitor");
        CHECK(ok, "per-monitor Next never lands on an image another monitor shows");
    }
    // One image only: repeats are the only option.
    {
        Folder f({ kOrientLandscape });
        const std::vector<Slot> slots = { Mk(kOrientLandscape, 0), Mk(kOrientPortrait, 1) };
        const Outcome r = Go(f, slots, +1);
        CHECK(r.shown.size() == 2 && r.res.missing == -1, "one-image folder: both monitors still get an image (repeat allowed)");
    }
    // Smart crop off: portrait monitors take portrait images only.
    {
        const std::vector<int> o = { kOrientLandscape, kOrientLandscape, kOrientPortrait, kOrientLandscape, kOrientPortrait, kOrientLandscape };
        Folder f(o, false, false, false);
        const std::vector<Slot> slots = { Mk(kOrientLandscape, 0), Mk(kOrientPortrait, 1) };
        bool ok = true;
        for (int i = 0; i < 12; ++i) {
            const Outcome q = Go(f, slots, +1);
            ok = ok && f.truth[static_cast<size_t>(q.shown.at(1))] == kOrientPortrait &&
                 f.truth[static_cast<size_t>(q.shown.at(0))] == kOrientLandscape && Distinct(q);
        }
        CHECK(ok, "smart crop OFF: orientations strictly matched, still distinct");
        Folder g(std::vector<int>(6, kOrientLandscape), false, false, false);
        const Outcome r = Go(g, slots, +1);
        CHECK(r.res.missing == kOrientPortrait && r.shown.count(0) == 1 && r.shown.count(1) == 0,
              "smart crop OFF, no portrait images: portrait reported missing, landscape still set");
    }
    // Colliding kept images (e.g. after switching modes) are repaired on re-apply.
    {
        Folder f(std::vector<int>(10, kOrientLandscape), false, true);
        const std::vector<Slot> slots = { Mk(kOrientLandscape, 0), Mk(kOrientPortrait, 1) };
        f.list.SetCursorPos(0, 4);
        f.list.SetCursorPos(1, 4);
        const Outcome r = Go(f, slots, 0);
        CHECK(r.shown.size() == 2 && Distinct(r), "re-apply with a colliding pair repairs the duplicate");
        CHECK(f.list.CursorPos(0) == 4, "...and the first display keeps its image");
    }
    // Re-apply (display change) leaves cursors alone.
    {
        Folder f(std::vector<int>(10, kOrientLandscape), false, true);
        const std::vector<Slot> slots = { Mk(kOrientLandscape, 0), Mk(kOrientPortrait, 1) };
        Go(f, slots, +1);
        Go(f, slots, +1);
        const int a = f.list.CursorPos(0), b = f.list.CursorPos(1);
        const Outcome r = Go(f, slots, 0);
        CHECK(f.list.CursorPos(0) == a && f.list.CursorPos(1) == b && r.shown.size() == 2, "dir=0 re-applies the same images without moving");
    }
    // A monitor is plugged in: it gets its own image, existing ones are untouched.
    {
        Folder f(std::vector<int>(10, kOrientLandscape), false, true);
        const std::vector<Slot> two = { Mk(kOrientLandscape, 3), Mk(kOrientLandscape, 4) };
        Go(f, two, +1);
        const int a = f.list.CursorPos(3), b = f.list.CursorPos(4);
        const std::vector<Slot> three = { Mk(kOrientLandscape, 3), Mk(kOrientLandscape, 4), Mk(kOrientPortrait, 5) };
        const Outcome r = Go(f, three, 0);
        CHECK(f.list.CursorPos(3) == a && f.list.CursorPos(4) == b && r.shown.size() == 3 && Distinct(r),
              "new monitor gets its own image, others keep theirs");
    }
    // Unusable image is skipped.
    {
        Folder f(std::vector<int>(6, kOrientLandscape));
        const std::vector<Slot> slots = { Mk(kOrientLandscape, 0), Mk(kOrientPortrait, 1) };
        const Outcome r = Go(f, slots, +1, -1, { 0 });
        CHECK(r.shown.size() == 2 && r.shown.at(0) != 0 && r.shown.at(1) != 0 && Distinct(r), "images that fail to apply are skipped");
    }
    // Rare orientation in a big unread folder: bounded reads per step, the search resumes.
    {
        std::vector<int> o(1000, kOrientLandscape);
        o[700] = kOrientPortrait;
        Folder f(o, false, false, false);
        const std::vector<Slot> slots = { Mk(kOrientPortrait, 1) };
        int steps = 0;
        Outcome r;
        bool bounded = true;
        do {
            f.reads = 0;
            r = Go(f, slots, +1);
            ++steps;
            if (f.reads > kProbeBudget) bounded = false;
        } while (!r.res.applied && steps < 200);
        CHECK(bounded, "each step reads at most the probe budget (16) unread images");
        CHECK(r.res.applied && r.shown.at(1) == 700 && steps > 1, "rare orientation is found after several cheap steps");
    }
    // Next / Previous are symmetrical for a single display.
    {
        Folder f(std::vector<int>(9, kOrientLandscape), true, true);
        const std::vector<Slot> slots = { Mk(kOrientLandscape, 0) };
        std::vector<int> seq, back;
        for (int i = 0; i < 5; ++i) seq.push_back(Go(f, slots, +1).shown.at(0));
        for (int i = 0; i < 4; ++i) back.push_back(Go(f, slots, -1).shown.at(0));
        CHECK(back[0] == seq[3] && back[1] == seq[2] && back[2] == seq[1] && back[3] == seq[0],
              "Next x5 then Previous x4 walks back through the same images");
    }
    // Robustness: empty folder, bad slot keys, lazy classification accounting.
    {
        Folder f(std::vector<int>{});
        const Outcome r = Go(f, { Mk(kOrientLandscape, 0) }, +1);
        CHECK(r.shown.empty() && !r.res.applied, "empty folder: nothing happens");
        Folder g(std::vector<int>(5, kOrientLandscape));
        Slot bad = Mk(kOrientLandscape, 999);
        const Outcome q = Go(g, { bad, Mk(kOrientLandscape, -3) }, +1);
        CHECK(q.shown.empty(), "out-of-range cursor keys are ignored");
        Folder h(std::vector<int>(50, kOrientLandscape));
        CHECK(h.reads == 0 && h.list.Known() == 0, "scanning reads no image");
        h.list.PrefetchRandom(3);
        CHECK(h.reads == 3 && h.list.Known() == 3 && h.list.Count(kOrientLandscape) == 3, "prefetch classifies exactly the requested number of random images");
    }
    return Report();
}
