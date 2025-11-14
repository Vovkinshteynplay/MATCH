#include "match/platform/SdlSaveService.hpp"

#include <SDL2/SDL.h>

#include <fstream>

namespace match::platform {

namespace {

std::filesystem::path DefaultSaveRoot() {
    std::filesystem::path base = SDL_GetPrefPath("MATCH", "Standalone");
    if (base.empty()) {
        base = std::filesystem::current_path() / "saves";
    }
    return base;
}

std::filesystem::path NormalizeFilename(const std::string& name) {
    std::filesystem::path path{name};
    return path.filename();
}

}  // namespace

SdlSaveService::SdlSaveService(std::filesystem::path root)
    : root_(root.empty() ? DefaultSaveRoot() : std::move(root)) {
    autosave_path_ = root_ / "autosave.bin";
}

bool SdlSaveService::Initialize() {
    return EnsureRootExists();
}

bool SdlSaveService::EnsureRootExists() const {
    std::error_code ec;
    if (std::filesystem::exists(root_)) {
        return true;
    }
    return std::filesystem::create_directories(root_, ec);
}

std::filesystem::path SdlSaveService::ResolvePath(const std::string& slot_name) const {
    auto safe = NormalizeFilename(slot_name);
    return root_ / safe.replace_extension(".bin");
}

std::vector<SaveSlotInfo> SdlSaveService::ListSlots() const {
    std::vector<SaveSlotInfo> slots;
    std::error_code ec;
    if (!std::filesystem::exists(root_, ec)) {
        return slots;
    }
    for (const auto& entry : std::filesystem::directory_iterator(root_, ec)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        auto path = entry.path();
        if (path.filename() == autosave_path_.filename()) {
            continue;
        }
        SaveSlotInfo info;
        info.path = path;
        info.name = path.stem().string();
        info.size_bytes = entry.file_size();
        std::error_code time_ec;
        auto ftime = entry.last_write_time(time_ec);
        if (!time_ec) {
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - std::filesystem::file_time_type::clock::now() +
                std::chrono::system_clock::now());
            info.modified_time = std::chrono::system_clock::to_time_t(sctp);
        }
        slots.push_back(info);
    }
    return slots;
}

bool SdlSaveService::Save(const std::string& slot_name, const std::vector<std::uint8_t>& payload) {
    if (!EnsureRootExists()) {
        return false;
    }
    std::filesystem::path path = ResolvePath(slot_name);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(payload.data()),
              static_cast<std::streamsize>(payload.size()));
    return out.good();
}

bool SdlSaveService::Load(const std::string& slot_name, std::vector<std::uint8_t>& out_payload) const {
    std::filesystem::path path = ResolvePath(slot_name);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    in.seekg(0, std::ios::end);
    std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    out_payload.resize(static_cast<std::size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(out_payload.data()), size);
    }
    return in.good();
}

bool SdlSaveService::Delete(const std::string& slot_name) {
    std::filesystem::path path = ResolvePath(slot_name);
    std::error_code ec;
    return std::filesystem::remove(path, ec);
}

bool SdlSaveService::AutoSave(const std::vector<std::uint8_t>& payload) {
    if (!EnsureRootExists()) {
        return false;
    }
    std::ofstream out(autosave_path_, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(payload.data()),
              static_cast<std::streamsize>(payload.size()));
    return out.good();
}

bool SdlSaveService::LoadAutoSave(std::vector<std::uint8_t>& out_payload) const {
    std::ifstream in(autosave_path_, std::ios::binary);
    if (!in) {
        return false;
    }
    in.seekg(0, std::ios::end);
    std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    out_payload.resize(static_cast<std::size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(out_payload.data()), size);
    }
    return in.good();
}

}  // namespace match::platform
