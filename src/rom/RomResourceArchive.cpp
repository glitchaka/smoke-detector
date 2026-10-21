#include "rom/RomResourceArchive.h"
#include "rom/CartridgeResourceSource.h"

#include <stdexcept>

namespace promptemon::rom {

bool RomResourceArchive::open(const std::filesystem::path& romDirectory) {
    close();
    library_ = RomLibrary::scan(romDirectory);

    std::size_t sourceIndex = 0;
    for (const auto& entry : library_.entries()) {
        try {
            auto source = openCartridgeResourceSource(entry.info);
            if (!source) source = openResourceSource(entry.info);
            if (!source) continue;
            SourceSlot slot;
            slot.label = "[" + std::to_string(sourceIndex++) + "] " + entry.info.path.filename().string();
            slot.source = std::move(source);
            sources_.push_back(std::move(slot));
        } catch (...) {
            // A resource provider must not prevent the remaining ROMs from being indexed.
        }
    }
    return !sources_.empty();
}

void RomResourceArchive::close() {
    sources_.clear();
    library_ = RomLibrary{};
}

std::vector<RomResourceSourceSummary> RomResourceArchive::sourceSummaries() const {
    std::vector<RomResourceSourceSummary> out;
    out.reserve(sources_.size());
    for (const auto& slot : sources_) {
        const auto resources = slot.source->resources();
        out.push_back({slot.label, slot.source->romInfo(), slot.source->providerName(), resources.size()});
    }
    return out;
}

std::vector<RomResource> RomResourceArchive::resourcesForSource(const std::string& sourceLabel) const {
    for (const auto& slot : sources_) {
        if (slot.label == sourceLabel) return slot.source->resources();
    }
    return {};
}

std::vector<std::string> RomResourceArchive::resourcePaths() const {
    std::vector<std::string> out;
    for (const auto& slot : sources_) {
        for (const auto& resource : slot.source->resources()) {
            out.push_back(slot.label + " :: " + resource.id);
        }
    }
    return out;
}

RomResourceArchive::DecodedPath RomResourceArchive::decode(const std::string& virtualPath) const {
    const std::string separator = " :: ";
    const auto split = virtualPath.find(separator);
    if (split == std::string::npos) return {};
    const std::string label = virtualPath.substr(0, split);
    const std::string resource = virtualPath.substr(split + separator.size());
    for (const auto& slot : sources_) {
        if (slot.label == label) return {slot.source.get(), resource, label};
    }
    return {};
}

bool RomResourceArchive::has(const std::string& virtualPath) const {
    const auto decoded = decode(virtualPath);
    return decoded.source && decoded.source->has(decoded.resource);
}

RomResourcePayload RomResourceArchive::extract(const std::string& virtualPath) const {
    const auto decoded = decode(virtualPath);
    if (!decoded.source) throw std::runtime_error("Unknown ROM resource: " + virtualPath);

    RomResource resource;
    bool found = false;
    for (const auto& candidate : decoded.source->resources()) {
        if (candidate.id == decoded.resource) {
            resource = candidate;
            found = true;
            break;
        }
    }
    if (!found) throw std::runtime_error("ROM resource not found: " + decoded.resource);

    RomResourcePayload payload;
    payload.rom = decoded.source->romInfo();
    payload.provider = decoded.label + " | " + decoded.source->providerName();
    payload.resource = std::move(resource);
    payload.bytes = decoded.source->extract(decoded.resource);
    return payload;
}

} // namespace promptemon::rom
