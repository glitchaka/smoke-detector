#pragma once

#include "rom/RomModelArchive.h"

#include <cstddef>
#include <filesystem>
#include <string>

namespace smoke_detector::exporter {

struct ExportResult {
    std::filesystem::path directory;
    std::filesystem::path interchangeModel;
    std::size_t animationContainers = 0;
    std::size_t animationClips = 0;
    std::size_t textures = 0;
    std::size_t audioResources = 0;
};

class AssetExporter {
public:
    static ExportResult exportModel(const promptemon::rom::RomModelArchive& archive,
                                    const std::string& virtualModelPath,
                                    const std::filesystem::path& exportRoot);
};

} // namespace smoke_detector::exporter
