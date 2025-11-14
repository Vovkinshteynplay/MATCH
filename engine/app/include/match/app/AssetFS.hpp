#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace match::app {

bool FileExists(const std::filesystem::path& path);

const std::vector<std::filesystem::path>& AssetRoots();

std::filesystem::path AssetPath(const std::string& filename);

}  // namespace match::app

