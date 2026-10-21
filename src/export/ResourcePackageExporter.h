#pragma once

#include "rom/RomResourceArchive.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace smoke_detector::exporter {

struct ResourcePackageResult {
    std::string sourceLabel;
    std::filesystem::path directory;
    std::filesystem::path packageFile;
    std::filesystem::path selfExtractingFile;
    std::size_t resources = 0;
    std::uint64_t bytes = 0;
};

class ResourcePackageExporter {
public:
    using ProgressCallback = std::function<void(std::size_t current,
                                                std::size_t total,
                                                const std::string& sourceLabel,
                                                const std::string& resourceId)>;

    static std::vector<ResourcePackageResult> exportAll(
        const promptemon::rom::RomResourceArchive& archive,
        const std::filesystem::path& exportRoot,
        const std::filesystem::path& selfExtractorStub = {},
        ProgressCallback progress = {},
        bool buildPackages = false);
};

} // namespace smoke_detector::exporter
