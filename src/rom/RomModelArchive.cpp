#include "rom/RomModelArchive.h"

#include <stdexcept>

namespace promptemon::rom {

bool RomModelArchive::open(const std::filesystem::path& romDirectory) {
    close();
    library_ = RomLibrary::scan(romDirectory);

    std::size_t sourceIndex = 0;
    for (const auto& entry : library_.entries()) {
        if (!entry.hasModelProvider) continue;
        try {
            auto source = openModelSource(entry.info);
            if (!source) continue;
            SourceSlot slot;
            slot.label = "[" + std::to_string(sourceIndex++) + "] " + entry.info.path.filename().string();
            slot.source = std::move(source);
            sources_.push_back(std::move(slot));
        } catch (...) {
        }
    }
    return !library_.empty();
}

void RomModelArchive::close() {
    sources_.clear();
    library_ = RomLibrary{};
}

std::vector<std::string> RomModelArchive::modelResources() const {
    std::vector<std::string> out;
    for (const auto& slot : sources_) {
        for (const auto& model : slot.source->models()) {
            out.push_back(slot.label + " :: " + model.id);
        }
    }
    return out;
}

RomModelArchive::DecodedPath RomModelArchive::decode(const std::string& virtualPath) const {
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

bool RomModelArchive::has(const std::string& virtualPath) const {
    const auto decoded = decode(virtualPath);
    return decoded.source && decoded.source->hasBch(decoded.resource);
}

std::vector<std::uint8_t> RomModelArchive::extractBch(const std::string& virtualPath) const {
    const auto decoded = decode(virtualPath);
    if (!decoded.source) throw std::runtime_error("Unknown ROM model resource: " + virtualPath);
    return decoded.source->extractBch(decoded.resource);
}

ModelPayload RomModelArchive::load(const std::string& virtualPath) const {
    const auto decoded = decode(virtualPath);
    if (!decoded.source) throw std::runtime_error("Unknown ROM model resource: " + virtualPath);
    return decoded.source->load(decoded.resource);
}

ModelExportPayload RomModelArchive::exportModel(const std::string& virtualPath) const {
    const auto decoded = decode(virtualPath);
    if (!decoded.source) throw std::runtime_error("Unknown ROM model resource: " + virtualPath);
    auto payload = decoded.source->exportModel(decoded.resource);
    payload.provider = decoded.label + " | " + payload.provider;
    return payload;
}

} // namespace promptemon::rom
