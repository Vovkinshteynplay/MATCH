#include "match/app/AssetFS.hpp"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <mutex>

namespace match::app {

bool FileExists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

void PushIfExists(std::vector<std::filesystem::path>& roots, const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return;
    }
    if (std::find(roots.begin(), roots.end(), path) == roots.end()) {
        roots.push_back(path);
    }
}

void HarvestAssetDirs(std::vector<std::filesystem::path>& roots,
                      const std::filesystem::path& start,
                      int max_depth = 8) {
    if (start.empty()) {
        return;
    }
    std::filesystem::path cursor = start;
    for (int depth = 0; depth < max_depth && !cursor.empty(); ++depth) {
        PushIfExists(roots, cursor / "assets");
        PushIfExists(roots, cursor / "assets_common");
#if defined(_WIN32)
        PushIfExists(roots, cursor / "assets_win");
#elif defined(__APPLE__)
        PushIfExists(roots, cursor / "assets_mac");
#else
        PushIfExists(roots, cursor / "assets_linux");
#endif
        cursor = cursor.parent_path();
    }
}

const std::vector<std::filesystem::path>& AssetRoots() {
    static std::vector<std::filesystem::path> roots;
    static std::once_flag init_flag;
    std::call_once(init_flag, [] {
        try {
            HarvestAssetDirs(roots, std::filesystem::current_path(), 8);
        } catch (const std::exception&) {
        }

        if (const char* env = std::getenv("MATCH_ASSETS")) {
            try {
                HarvestAssetDirs(roots, std::filesystem::path(env), 2);
            } catch (const std::exception&) {
            }
        }

        if (char* raw_base = SDL_GetBasePath()) {
            std::filesystem::path base_path(raw_base);
            SDL_free(raw_base);
            HarvestAssetDirs(roots, base_path, 8);
        }

        if (roots.empty()) {
            PushIfExists(roots, std::filesystem::current_path());
        }
    });
    return roots;
}

std::filesystem::path AssetPath(const std::string& filename) {
    for (const auto& root : AssetRoots()) {
        std::filesystem::path candidate = root / filename;
        if (FileExists(candidate)) {
            return candidate;
        }
    }
    return std::filesystem::path(filename);
}

}  // namespace match::app
