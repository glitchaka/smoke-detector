#pragma once

#include "rom/RomInspector.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace promptemon::rom {

struct ModelResource {
    std::string id;
    std::string displayName;
};

struct ModelPayload {
    std::vector<std::uint8_t> modelBch;
    std::vector<std::uint8_t> animationBch;
};

class IRomModelSource {
public:
    virtual ~IRomModelSource() = default;

    virtual const RomInfo& romInfo() const noexcept = 0;
    virtual std::string providerName() const = 0;
    virtual std::vector<ModelResource> models() const = 0;
    virtual bool hasBch(const std::string& resourceId) const = 0;
    virtual std::vector<std::uint8_t> extractBch(const std::string& resourceId) const = 0;
    virtual ModelPayload load(const std::string& resourceId) const = 0;
};

std::unique_ptr<IRomModelSource> openModelSource(const RomInfo& rom);

} // namespace promptemon::rom
