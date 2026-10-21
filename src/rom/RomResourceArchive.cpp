#include "rom/RomResourceArchive.h"
#include "rom/CartridgeResourceSource.h"
#include "rom/ThreeDsCrypto.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace promptemon::rom {
namespace {

std::vector<std::uint8_t> readWholeFile(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("ROM is too large to read: " + path.string());
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Could not open ROM: " + path.string());
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
            throw std::runtime_error("Could not read complete ROM: " + path.string());
        }
    }
    return bytes;
}

class OriginalRomResourceSource final : public IRomResourceSource {
public:
    OriginalRomResourceSource(RomInfo original,
                              std::unique_ptr<IRomResourceSource> inner,
                              std::string diagnostic)
        : original_(std::move(original)), inner_(std::move(inner)), diagnostic_(std::move(diagnostic)) {}

    const RomInfo& romInfo() const noexcept override { return original_; }

    std::string providerName() const override {
        std::string name = inner_ ? inner_->providerName() : "ROM image";
        if (!diagnostic_.empty()) name += " | " + diagnostic_;
        return name;
    }

    std::vector<RomResource> resources() const override {
        std::vector<RomResource> out = inner_ ? inner_->resources() : std::vector<RomResource>{};
        for (auto& resource : out) {
            if (resource.kind == ResourceKind::RawRom || resource.id.rfind("raw/rom", 0) == 0) {
                resource.size = static_cast<std::uint64_t>(original_.size);
                resource.displayName = "Complete original ROM image";
            }
        }
        if (!diagnostic_.empty()) {
            const bool exists = std::any_of(out.begin(), out.end(), [](const RomResource& resource) {
                return resource.id == "system/3ds-status.txt";
            });
            if (!exists && (original_.format == RomFormat::Nintendo3DsImage || original_.format == RomFormat::Nintendo3DsCia)) {
                out.push_back({"system/3ds-status.txt", "Nintendo 3DS container status", ResourceKind::Text,
                               static_cast<std::uint64_t>(diagnostic_.size())});
            }
        }
        return out;
    }

    bool has(const std::string& resourceId) const override {
        if (resourceId == "system/3ds-status.txt" && !diagnostic_.empty()) return true;
        if (resourceId.rfind("raw/rom", 0) == 0) return true;
        return inner_ && inner_->has(resourceId);
    }

    std::vector<std::uint8_t> extract(const std::string& resourceId) const override {
        if (resourceId == "system/3ds-status.txt" && !diagnostic_.empty()) {
            return std::vector<std::uint8_t>(diagnostic_.begin(), diagnostic_.end());
        }
        if (resourceId.rfind("raw/rom", 0) == 0) return readWholeFile(original_.path);
        if (!inner_) throw std::runtime_error("ROM resource provider is not available");
        return inner_->extract(resourceId);
    }

private:
    RomInfo original_;
    std::unique_ptr<IRomResourceSource> inner_;
    std::string diagnostic_;
};

} // namespace

bool RomResourceArchive::open(const std::filesystem::path& romDirectory) {
    close();
    library_ = RomLibrary::scan(romDirectory);

    std::size_t sourceIndex = 0;
    for (const auto& entry : library_.entries()) {
        try {
            auto source = openCartridgeResourceSource(entry.info);
            std::string diagnostic;

            if (!source) {
                RomInfo readableInfo = entry.info;
                if (entry.info.format == RomFormat::Nintendo3DsImage) {
                    const auto readable = prepareReadableThreeDsImage(entry.info);
                    readableInfo.path = readable.path;
                    if (readable.temporary) {
                        std::error_code ec;
                        readableInfo.size = std::filesystem::file_size(readable.path, ec);
                    }
                    diagnostic = readable.diagnostic;
                }

                source = openResourceSource(readableInfo);
                if (source && readableInfo.path != entry.info.path) {
                    source = std::make_unique<OriginalRomResourceSource>(entry.info, std::move(source), diagnostic);
                } else if (source && !diagnostic.empty()) {
                    source = std::make_unique<OriginalRomResourceSource>(entry.info, std::move(source), diagnostic);
                }
            }
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
