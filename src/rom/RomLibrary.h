#pragma once

#include "rom/RomInspector.h"

#include <filesystem>
#include <string>
#include <vector>

namespace promptemon::rom {

struct RomLibraryEntry {
    RomInfo info;
    bool hasModelProvider = false;
    std::string modelProvider;
    std::string modelError;
};

class RomLibrary {
public:
    static RomLibrary scan(const std::filesystem::path& directory);

    const std::vector<RomLibraryEntry>& entries() const noexcept { return entries_; }
    bool empty() const noexcept { return entries_.empty(); }

private:
    std::vector<RomLibraryEntry> entries_;
};

} // namespace promptemon::rom
