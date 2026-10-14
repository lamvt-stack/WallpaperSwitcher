// Playlist: which image each display shows, and how that moves on Next / Previous.
// Pure logic with no Windows types, so it is unit-tested natively. The Windows side
// supplies image classification through a callback and applies the chosen images through
// a callback passed to Run().
#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace ws {

// Image / display orientation classes.
enum Orient : int {
    kOrientUnknown   = -2,   // not read yet
    kOrientUnusable  = -1,   // could not be decoded
    kOrientLandscape = 0,
    kOrientPortrait  = 1,
    kOrientBoth      = 2,    // image decoder unavailable: can't tell, so don't filter
};

struct FileEntry {
    std::wstring  path;
    std::uint64_t size   = 0;
    std::uint64_t mtime  = 0;
    signed char   orient = kOrientUnknown;
    int           ow = 0, oh = 0;   // orientation-corrected pixel size (0 = unknown)
};

constexpr int kMaxMonitors = 16;
constexpr int kKeySpan     = 2;                         // cursor keys: 0 landscape group, 1 portrait group,
constexpr int kKeyMonitor  = 3;                         // 2 span, 3 + k monitor k
constexpr int kNumKeys     = kKeyMonitor + kMaxMonitors;
constexpr int kUnsetCursor = INT_MIN;
constexpr int kProbeBudget = 16;                        // max image reads per display per switch

// One independently-advancing wallpaper: a single monitor (different-image mode), a group of
// same-orientation monitors (same-image mode) or the whole desktop (span).
struct Slot {
    int  orient = kOrientLandscape;
    int  key    = 0;                // index into the playlist's cursors
    bool all    = false;            // true: apply to every monitor
    int  tw = 0, th = 0;            // pixel size of the display (or desktop) this slot drives
    std::vector<std::wstring> ids;  // monitor ids it drives (when !all)
};

struct RunResult {
    int  missing   = -1;            // orientation that has no suitable images in the folder
    int  searching = -1;            // orientation still being searched for (probe budget used up)
    bool applied   = false;         // at least one image was applied
};

class Playlist {
public:
    // `classify` reads an image's orientation / size into the entry (called lazily, at most
    // once per file, never for files that already have a known orientation).
    using ClassifyFn = std::function<void(FileEntry&)>;

    explicit Playlist(ClassifyFn classify, std::uint32_t seed = std::random_device{}());

    // Replace the listing (entries may already be classified, e.g. from a cache).
    void SetFiles(std::vector<FileEntry> files, bool shuffle);
    // New play order; what we know about the images is kept.
    void Reorder(bool shuffle);
    void ResetCursors();
    // Portrait displays accept landscape images when smart crop is on (they get cropped).
    void SetSmartCrop(bool on) { smartCrop_ = on; }

    bool Empty() const { return files_.empty(); }
    const std::vector<FileEntry>& Files() const { return files_; }
    std::size_t Known() const { return known_; }                         // files classified so far
    std::size_t Count(int orient) const { return count_[orient == kOrientPortrait ? 1 : 0]; }

    // Classify up to `count` randomly chosen, still-unread images.
    void PrefetchRandom(int count);

    // Choose and apply an image for every slot.
    //   dir   +1 next, -1 previous, 0 re-apply what is shown (after a mode / display change)
    //   only  >= 0: advance just that slot, the others keep their image
    // No two slots ever show the same source image, whatever their orientation (repeats only
    // when the folder has too few images): images that stay put are reserved first, then the
    // slots that change pick from what is left, in order. At most kProbeBudget unread images
    // are opened per slot. apply(slot, fileEntry) -> bool puts the image on the display(s).
    template <class ApplyFn>
    RunResult Run(const std::vector<Slot>& slots, int dir, int only, ApplyFn&& apply);

    // Introspection (tests, tooltips).
    int CursorFile(int key) const;   // file index shown by `key`, or -1
    int CursorPos(int key) const { return cursor_[key]; }
    void SetCursorPos(int key, int pos) { cursor_[key] = pos; }

private:
    enum class Find { Found, None, Budget };

    bool Matches(int fileOrient, int wanted) const;
    void Classify(int fileIndex);
    Find FindNext(int& pos, int dir, int wanted, int& budget, const std::vector<int>* avoid) ;
    Find FindDistinct(int& pos, int dir, int wanted, int& budget, const std::vector<int>& avoid);
    static int Wrap(int v, int n) { return ((v % n) + n) % n; }
    static bool Contains(const std::vector<int>& v, int x);

    ClassifyFn             classify_;
    std::mt19937           rng_;
    std::vector<FileEntry> files_;
    std::vector<int>       perm_;       // play order: indices into files_
    std::vector<int>       pending_;    // shuffled indices still to be probed at random
    std::size_t            known_ = 0;
    std::size_t            count_[2] = { 0, 0 };
    int                    cursor_[kNumKeys];
    bool                   smartCrop_ = true;
};

// ---- template implementation ------------------------------------------------------------

template <class ApplyFn>
RunResult Playlist::Run(const std::vector<Slot>& slots, int dir, int only, ApplyFn&& apply) {
    RunResult res;
    const int n = static_cast<int>(perm_.size());
    if (n == 0) return res;
    const int d = dir != 0 ? dir : 1;                  // direction used for searching

    enum class Act { Keep, Advance, Fix };
    std::vector<Act> act(slots.size(), Act::Advance);
    std::vector<int> taken;                            // source files already in use by a display

    // Pass 1: displays that keep their image reserve it. A kept image that no longer suits
    // its display, or that another display already holds, is replaced.
    for (std::size_t s = 0; s < slots.size(); ++s) {
        const int key = slots[s].key;
        if (key < 0 || key >= kNumKeys) continue;
        const int cur = cursor_[key];
        if (cur == kUnsetCursor || cur < 0 || cur >= n) continue;                // new: Advance
        if (dir != 0 && (only < 0 || static_cast<int>(s) == only)) continue;     // moving: Advance
        const int f = perm_[static_cast<std::size_t>(cur)];
        if (Matches(files_[static_cast<std::size_t>(f)].orient, slots[s].orient) && !Contains(taken, f)) {
            act[s] = Act::Keep;
            taken.push_back(f);
        } else {
            act[s] = Act::Fix;
        }
    }

    // Pass 2: pick and apply.
    for (std::size_t s = 0; s < slots.size(); ++s) {
        const Slot& slot = slots[s];
        if (slot.key < 0 || slot.key >= kNumKeys) continue;
        int& cur = cursor_[slot.key];
        int budget = kProbeBudget;
        Find r = Find::Found;

        if (cur == kUnsetCursor || cur < 0 || cur >= n) {
            cur = Wrap(-d, n);                         // so the first image examined is position 0
            act[s] = Act::Advance;
        }
        if (act[s] == Act::Advance)  r = FindDistinct(cur, d, slot.orient, budget, taken);
        else if (act[s] == Act::Fix) r = FindDistinct(cur, +1, slot.orient, budget, taken);
        else if (dir != 0)           continue;         // this display is not being changed

        for (int attempt = 0; r == Find::Found && attempt < 4; ++attempt) {
            const int f = perm_[static_cast<std::size_t>(cur)];
            if (apply(slot, files_[static_cast<std::size_t>(f)])) {
                res.applied = true;
                if (!Contains(taken, f)) taken.push_back(f);
                break;
            }
            r = FindDistinct(cur, d, slot.orient, budget, taken);   // unusable file: try the next one
        }
        if (r == Find::None)        res.missing   = slot.orient;
        else if (r == Find::Budget) res.searching = slot.orient;
    }
    return res;
}

}  // namespace ws
