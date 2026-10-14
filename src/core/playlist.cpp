#include "playlist.h"

#include <algorithm>
#include <numeric>
#include <utility>

namespace ws {

Playlist::Playlist(ClassifyFn classify, std::uint32_t seed)
    : classify_(std::move(classify)), rng_(seed) {
    ResetCursors();
}

void Playlist::ResetCursors() {
    for (int& c : cursor_) c = kUnsetCursor;
}

void Playlist::Reorder(bool shuffle) {
    perm_.resize(files_.size());
    std::iota(perm_.begin(), perm_.end(), 0);
    if (shuffle) std::shuffle(perm_.begin(), perm_.end(), rng_);
    ResetCursors();
}

void Playlist::SetFiles(std::vector<FileEntry> files, bool shuffle) {
    files_ = std::move(files);
    known_ = 0;
    count_[0] = count_[1] = 0;
    pending_.clear();
    for (std::size_t i = 0; i < files_.size(); ++i) {
        const int o = files_[i].orient;
        if (o == kOrientUnknown) {
            pending_.push_back(static_cast<int>(i));
            continue;
        }
        ++known_;
        if (o == kOrientLandscape || o == kOrientBoth) ++count_[0];
        if (o == kOrientPortrait || o == kOrientBoth) ++count_[1];
    }
    std::shuffle(pending_.begin(), pending_.end(), rng_);    // random probing queue
    Reorder(shuffle);
}

void Playlist::Classify(int fileIndex) {
    FileEntry& f = files_[static_cast<std::size_t>(fileIndex)];
    if (f.orient != kOrientUnknown) return;
    if (classify_) classify_(f);
    if (f.orient == kOrientUnknown) f.orient = kOrientUnusable;      // a classifier must not leave it unknown
    ++known_;
    if (f.orient == kOrientLandscape || f.orient == kOrientBoth) ++count_[0];
    if (f.orient == kOrientPortrait || f.orient == kOrientBoth) ++count_[1];
}

void Playlist::PrefetchRandom(int count) {
    while (count > 0 && !pending_.empty()) {
        const int i = pending_.back();
        pending_.pop_back();
        if (files_[static_cast<std::size_t>(i)].orient == kOrientUnknown) {
            Classify(i);
            --count;
        }
    }
}

bool Playlist::Matches(int fileOrient, int wanted) const {
    if (fileOrient == kOrientBoth || fileOrient == wanted) return true;
    return wanted == kOrientPortrait && smartCrop_ && fileOrient == kOrientLandscape;
}

bool Playlist::Contains(const std::vector<int>& v, int x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

int Playlist::CursorFile(int key) const {
    if (key < 0 || key >= kNumKeys) return -1;
    const int cur = cursor_[key];
    if (cur == kUnsetCursor || cur < 0 || cur >= static_cast<int>(perm_.size())) return -1;
    return perm_[static_cast<std::size_t>(cur)];
}

// Walk perm_ from `pos` in direction `dir` (wrapping) to the next image that suits a display
// of orientation `wanted` (and is not in `avoid`, if given), reading unknown files on the
// way (at most `budget`).
//   Found  - pos is that image.
//   None   - a full lap found nothing suitable.
//   Budget - ran out of reads; pos is left where we stopped, so the next call resumes the
//            search instead of repeating it.
Playlist::Find Playlist::FindNext(int& pos, int dir, int wanted, int& budget, const std::vector<int>* avoid) {
    const int n = static_cast<int>(perm_.size());
    int p = pos;
    for (int i = 0; i < n; ++i) {
        p = Wrap(p + dir, n);
        const int f = perm_[static_cast<std::size_t>(p)];
        if (files_[static_cast<std::size_t>(f)].orient == kOrientUnknown) {
            if (budget <= 0) { pos = Wrap(p - dir, n); return Find::Budget; }
            --budget;
            Classify(f);
        }
        if (Matches(files_[static_cast<std::size_t>(f)].orient, wanted) && !(avoid && Contains(*avoid, f))) {
            pos = p;
            return Find::Found;
        }
    }
    return Find::None;
}

// Like FindNext, but `avoid` lists the images other displays are showing, and those are
// skipped so no two displays show the same picture. Only when there are not enough distinct
// images (e.g. a one-image folder) is a repeat allowed.
Playlist::Find Playlist::FindDistinct(int& pos, int dir, int wanted, int& budget, const std::vector<int>& avoid) {
    int p = pos;
    Find r = FindNext(p, dir, wanted, budget, &avoid);
    if (r == Find::None && !avoid.empty()) r = FindNext(p, dir, wanted, budget, nullptr);
    if (r != Find::None) pos = p;
    return r;
}

}  // namespace ws
