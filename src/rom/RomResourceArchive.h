#pragma once

#include "rom/IRomResourceSource.h"
#include "rom/RomLibrary.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace promptemon::rom {

struct RomResourceSourceSummary {
    std::string label;
    RomInfo rom;
    std::string provider;
    std::size_t resourceCount = 0;
};

class RomResourceArchive {
public:
    bool open(const std::filesystem::path& romDirectory);
    void close();

    const RomLibrary& library() const noexcept { return library_; }
    std::size_t sourceCount() const noexcept { return sources_.size(); }

    std::vector<RomResourceSourceSummary> sourceSummaries() const;
    std::vector<RomResource> resourcesForSource(const std::string& sourceLabel) const;
    std::vector<std::string> resourcePaths() const;

    bool has(const std::string& virtualPath) const;
    RomResourcePayload extract(const std::string& virtualPath) const;

private:
    struct SourceSlot {
        std::string label;
        std::unique_ptr<IRomResourceSource> source;
    };

    struct DecodedPath {
        const IRomResourceSource* source = nullptr;
        std::string resource;
        std::string label;
    };

    RomLibrary library_;
    std::vector<SourceSlot> sources_;

    DecodedPath decode(const std::string& virtualPath) const;
};

} // namespace promptemon::rom
