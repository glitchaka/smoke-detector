#include "rom/IRomModelSource.h"

#include "rom/RumbleWorldArchive.h"

#include <memory>
#include <stdexcept>
#include <utility>

namespace promptemon::rom {
namespace {

class CiaBchModelSource final : public IRomModelSource {
public:
    explicit CiaBchModelSource(RomInfo info) : info_(std::move(info)) {
        archive_ = std::make_unique<RumbleWorldArchive>();
        if (!archive_->open(info_.path)) {
            throw std::runtime_error("Could not open 3DS RomFS: " + info_.path.string());
        }
        const auto paths = archive_->modelResources();
        resources_.reserve(paths.size());
        for (const auto& path : paths) resources_.push_back({path, path});
        if (resources_.empty()) {
            throw std::runtime_error("No BCH 3D models found in: " + info_.path.filename().string());
        }
    }

    const RomInfo& romInfo() const noexcept override { return info_; }
    std::string providerName() const override { return "Nintendo 3DS BCH"; }
    std::vector<ModelResource> models() const override { return resources_; }

    bool hasBch(const std::string& resourceId) const override {
        return archive_ && archive_->has(resourceId);
    }

    std::vector<std::uint8_t> extractBch(const std::string& resourceId) const override {
        if (!archive_) throw std::runtime_error("3DS model source is closed");
        return archive_->extractBch(resourceId);
    }

    ModelPayload load(const std::string& resourceId) const override {
        if (!hasBch(resourceId)) {
            throw std::runtime_error("Model resource not found: " + resourceId);
        }

        ModelPayload payload;
        payload.modelBch = extractBch(resourceId);

        std::vector<std::string> candidates;
        const auto slash = resourceId.find_last_of('/');
        const std::string directory = slash == std::string::npos ? std::string{} : resourceId.substr(0, slash);
        if (resourceId.rfind("pii/", 0) == 0) {
            candidates.push_back("pii/anim.bch.cx");
            candidates.push_back("pii/anim.bch");
        }
        if (resourceId.rfind("mii/", 0) == 0) {
            candidates.push_back("mii/mii_anim.bch.cx");
            candidates.push_back("mii/mii_anim.bch");
        }
        if (!directory.empty()) {
            candidates.push_back(directory + "/anim.bch.cx");
            candidates.push_back(directory + "/anim.bch");
            candidates.push_back(directory + "/animation.bch.cx");
            candidates.push_back(directory + "/animation.bch");
        }

        for (const auto& candidate : candidates) {
            if (!hasBch(candidate)) continue;
            try {
                payload.animationBch = extractBch(candidate);
                break;
            } catch (...) {
            }
        }
        return payload;
    }

private:
    RomInfo info_;
    std::unique_ptr<RumbleWorldArchive> archive_;
    std::vector<ModelResource> resources_;
};

} // namespace

std::unique_ptr<IRomModelSource> openModelSource(const RomInfo& rom) {
    if (rom.format == RomFormat::Nintendo3DsCia || rom.format == RomFormat::Nintendo3DsImage) {
        return std::make_unique<CiaBchModelSource>(rom);
    }
    return nullptr;
}

} // namespace promptemon::rom
