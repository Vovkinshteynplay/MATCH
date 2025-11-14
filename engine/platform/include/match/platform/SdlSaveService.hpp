#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace match::platform {

struct SaveSlotInfo {
    std::string name;
    std::filesystem::path path;
    std::uintmax_t size_bytes = 0;
    std::time_t modified_time = 0;
};

class SdlSaveService {
public:
    explicit SdlSaveService(std::filesystem::path root);

    bool Initialize();
    std::vector<SaveSlotInfo> ListSlots() const;
    bool Save(const std::string& slot_name, const std::vector<std::uint8_t>& payload);
    bool Load(const std::string& slot_name, std::vector<std::uint8_t>& out_payload) const;
    bool Delete(const std::string& slot_name);

    bool AutoSave(const std::vector<std::uint8_t>& payload);
    bool LoadAutoSave(std::vector<std::uint8_t>& out_payload) const;

    const std::filesystem::path& root() const { return root_; }

private:
    std::filesystem::path ResolvePath(const std::string& slot_name) const;
    bool EnsureRootExists() const;

    std::filesystem::path root_;
    std::filesystem::path autosave_path_;
};

}  // namespace match::platform

