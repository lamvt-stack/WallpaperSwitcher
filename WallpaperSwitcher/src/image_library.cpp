#include "image_library.h"

#include <algorithm>

#include "wic.h"
#include "win_util.h"

namespace ws {
namespace {

constexpr size_t kMaxFiles = 2000000;     // hard cap on a listing (memory bound for pathological folders)

bool IsImageFile(const wchar_t* name) {
    static const wchar_t* const kExts[] = { L".jpg", L".jpeg", L".png", L".bmp" };
    const wchar_t* dot = wcsrchr(name, L'.');
    if (!dot) return false;
    for (const wchar_t* ext : kExts)
        if (_wcsicmp(dot, ext) == 0) return true;
    return false;
}

}  // namespace

ImageLibrary::ImageLibrary()
    : list_([](FileEntry& f) {
          const ImageProbe p = ProbeImage(f.path);
          f.orient = static_cast<signed char>(p.orient);
          f.ow = p.ow;
          f.oh = p.oh;
      }) {}

void ImageLibrary::Scan(const std::wstring& folder, bool shuffle) {
    // Remember what we learned about the previous listing.
    for (const FileEntry& f : list_.Files())
        if (f.orient != kOrientUnknown) cache_[f.path] = { f.size, f.mtime, f.orient, f.ow, f.oh };

    std::vector<FileEntry> files;
    if (!folder.empty()) {
        WIN32_FIND_DATAW fd;
        UniqueFind h(::FindFirstFileExW(JoinPath(folder, L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch,
                                        nullptr, FIND_FIRST_EX_LARGE_FETCH));
        if (!h) {
            LogError(L"FindFirstFileExW", ::GetLastError());
        } else {
            bool capped = false;
            do {
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !IsImageFile(fd.cFileName)) continue;
                if (files.size() >= kMaxFiles) { capped = true; break; }
                FileEntry e;
                e.path  = JoinPath(folder, fd.cFileName);
                e.size  = (static_cast<std::uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
                e.mtime = (static_cast<std::uint64_t>(fd.ftLastWriteTime.dwHighDateTime) << 32) | fd.ftLastWriteTime.dwLowDateTime;
                const auto it = cache_.find(e.path);
                if (it != cache_.end() && it->second.size == e.size && it->second.mtime == e.mtime) {
                    e.orient = it->second.orient;
                    e.ow = it->second.ow;
                    e.oh = it->second.oh;
                }
                files.push_back(std::move(e));
            } while (::FindNextFileW(h.get(), &fd));
            if (!capped) {
                const DWORD err = ::GetLastError();            // read immediately after the failing call
                if (err != ERROR_NO_MORE_FILES) LogError(L"FindNextFileW", err);
            }
        }
        std::sort(files.begin(), files.end(), [](const FileEntry& a, const FileEntry& b) {
            return _wcsicmp(a.path.c_str(), b.path.c_str()) < 0;
        });
    }
    list_.SetFiles(std::move(files), shuffle);
}

}  // namespace ws
