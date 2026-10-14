// Subject-aware crop for portrait displays: renders a crop of a picture, positioned by
// ChooseCropRect(), to a cached PNG shaped like the display.
#pragma once

#include <string>

#include "core/playlist.h"

namespace ws {

// Returns the path of a PNG cropped to the aspect ratio of a targetW x targetH display, or
// an empty string when no crop is needed or possible (the caller then uses the original).
// Results are cached under %LOCALAPPDATA%\WallpaperSwitcher\crops (64 most recent kept).
std::wstring MakeSmartCrop(const FileEntry& file, int targetW, int targetH);

}  // namespace ws
