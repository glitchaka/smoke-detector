#pragma once

#include "rom/IRomModelSource.h"
#include "rom/RomLibrary.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace promptemon::rom {

class RomModelArchive {
public:
    bool open(const std::filesystem::path& romDirectory);
    void close();

    const RomLibrary& library() const noexcept { return library_; }
    std::size_t modelSourceCount() const noexcept { return sources_.size(); }
    std::vector<std::string> modelResources() const;

    bool has(const std::string& virtualPath) const;
    std::vector<std::uint8_t> extractBch(const std::string& virtualPath) const;
    ModelPayload load(const std::string& virtualPath) const;
    ModelExportPayload exportModel(const std::string& virtualPath) const;

private:
    struct SourceSlot {
        std::string label;
        std::unique_ptr<IRomModelSource> source;
    };

    struct DecodedPath {
        const IRomModelSource* source = nullptr;
        std::string resource;
        std::string label;
    };

    RomLibrary library_;
    std::vector<SourceSlot> sources_;

    DecodedPath decode(const std::string& virtualPath) const;
};

} // namespace promptemon::rom
