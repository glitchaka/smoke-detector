#pragma once

#include "rom/RomInspector.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace promptemon::rom {

enum class ResourceKind {
    Unknown,
    RawRom,
    System,
    File,
    Model,
    Animation,
    Texture,
    Audio,
    Archive,
    Data,
    Text,
    Graphics,
    Palette,
};

struct RomResource {
    std::string id;
    std::string displayName;
    ResourceKind kind = ResourceKind::Unknown;
    std::uint64_t size = 0;
};

struct RomResourcePayload {
    RomInfo rom;
    std::string provider;
    RomResource resource;
    std::vector<std::uint8_t> bytes;
};

class IRomResourceSource {
public:
    virtual ~IRomResourceSource() = default;

    virtual const RomInfo& romInfo() const noexcept = 0;
    virtual std::string providerName() const = 0;
    virtual std::vector<RomResource> resources() const = 0;
    virtual bool has(const std::string& resourceId) const = 0;
    virtual std::vector<std::uint8_t> extract(const std::string& resourceId) const = 0;
};

const char* resourceKindName(ResourceKind kind) noexcept;
std::unique_ptr<IRomResourceSource> openResourceSource(const RomInfo& rom);

} // namespace promptemon::rom
