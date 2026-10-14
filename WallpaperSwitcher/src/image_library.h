// The picture folder: a lazily classified listing wrapped in a Playlist, plus a session
// cache so "Rescan folder" does not re-read images whose size and timestamp are unchanged.
#pragma once

#include <string>
#include <unordered_map>

#include "core/playlist.h"

namespace ws {

class ImageLibrary {
public:
    ImageLibrary();

    // Lists `folder` (names, sizes and timestamps only - no image is opened) and starts a new
    // play order. Previously classified files that did not change keep their classification.
    void Scan(const std::wstring& folder, bool shuffle);

    Playlist& List() { return list_; }
    const Playlist& List() const { return list_; }

private:
    struct CacheValue {
        std::uint64_t size;
        std::uint64_t mtime;
        signed char   orient;
        int           ow, oh;
    };

    std::unordered_map<std::wstring, CacheValue> cache_;
    Playlist list_;
};

}  // namespace ws
