#include "bch/BchUvMetadata.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace promptemon::bch {
namespace {

using Bytes = std::vector<std::uint8_t>;

std::uint32_t u32(const Bytes& data, std::size_t offset) {
    if (offset + 4 > data.size()) throw std::runtime_error("BCH truncated while reading UV metadata");
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

void put32(Bytes& data, std::size_t offset, std::uint32_t value) {
    if (offset + 4 > data.size()) throw std::runtime_error("BCH relocation out of bounds while reading UV metadata");
    for (int i = 0; i < 4; ++i) data[offset + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value >> (i * 8));
}

struct Header {
    std::uint8_t backwardCompatibility = 0;
    std::uint32_t main = 0;
    std::uint32_t strings = 0;
    std::uint32_t gpu = 0;
    std::uint32_t data = 0;
    std::uint32_t dataExt = 0;
    std::uint32_t relocation = 0;
    std::uint32_t relocationLength = 0;
};

Header relocate(Bytes& bytes) {
    if (bytes.size() < 0x40 || bytes[0] != 'B' || bytes[1] != 'C' || bytes[2] != 'H') {
        throw std::runtime_error("Not BCH while reading UV metadata");
    }

    Header header;
    header.backwardCompatibility = bytes[4];
    header.main = u32(bytes, 8);
    header.strings = u32(bytes, 12);
    header.gpu = u32(bytes, 16);
    header.data = u32(bytes, 20);
    if (header.backwardCompatibility > 0x20) {
        header.dataExt = u32(bytes, 24);
        header.relocation = u32(bytes, 28);
        header.relocationLength = u32(bytes, 52);
    } else {
        header.relocation = u32(bytes, 24);
        header.relocationLength = u32(bytes, 44);
    }

    const std::uint64_t relocationEnd = std::min<std::uint64_t>(
        static_cast<std::uint64_t>(header.relocation) + header.relocationLength,
        bytes.size());

    for (std::uint32_t offset = header.relocation;
         static_cast<std::uint64_t>(offset) + 4 <= relocationEnd;
         offset += 4) {
        const std::uint32_t value = u32(bytes, offset);
        const std::uint32_t relative = value & 0x1ffffffu;
        const std::uint32_t flags = value >> 25;

        const auto patch = [&](std::uint64_t patchOffset, std::uint32_t base, std::uint32_t high = 0) {
            if (patchOffset + 4 > bytes.size()) return;
            std::uint32_t pointer = u32(bytes, static_cast<std::size_t>(patchOffset)) + base;
            pointer = (pointer & 0x7fffffffu) | high;
            put32(bytes, static_cast<std::size_t>(patchOffset), pointer);
        };

        if (flags == 0) patch(static_cast<std::uint64_t>(relative) * 4 + header.main, header.main);
        else if (flags == 1) patch(static_cast<std::uint64_t>(relative) + header.main, header.strings);
        else if (flags == 2) patch(static_cast<std::uint64_t>(relative) * 4 + header.main, header.gpu);
        else if (flags == 7 || flags == 0x0c) patch(static_cast<std::uint64_t>(relative) * 4 + header.main, header.data);

        const std::uint64_t gpuPointer = static_cast<std::uint64_t>(relative) * 4 + header.gpu;
        if (header.backwardCompatibility >= 0x21) {
            if (flags == 0x25 || flags == 0x26 || flags == 0x2e) patch(gpuPointer, header.data);
            else if (flags == 0x27) patch(gpuPointer, header.data, 0x80000000u);
            else if (flags == 0x28) patch(gpuPointer, header.data);
            else if (flags == 0x2b) patch(gpuPointer, header.dataExt);
            else if (flags == 0x2c) patch(gpuPointer, header.dataExt, 0x80000000u);
            else if (flags == 0x2d) patch(gpuPointer, header.dataExt);
        }
    }

    return header;
}

} // namespace

std::vector<MaterialUvMetadata> readMaterialUvMetadata(const std::vector<std::uint8_t>& modelBch) {
    Bytes bytes = modelBch;
    const Header header = relocate(bytes);
    if (static_cast<std::uint64_t>(header.main) + 8 > bytes.size()) return {};

    const std::uint32_t modelTable = u32(bytes, header.main);
    const std::uint32_t modelCount = u32(bytes, header.main + 4);
    if (!modelCount || !modelTable || static_cast<std::uint64_t>(modelTable) + 4 > bytes.size()) return {};

    const std::uint32_t modelHeader = u32(bytes, modelTable);
    if (!modelHeader || static_cast<std::uint64_t>(modelHeader) + 56 > bytes.size()) return {};

    const std::size_t materialDescriptor = static_cast<std::size_t>(modelHeader) + 4 + 48;
    if (materialDescriptor + 8 > bytes.size()) return {};
    const std::uint32_t materialsOffset = u32(bytes, materialDescriptor);
    const std::uint32_t materialCount = std::min<std::uint32_t>(u32(bytes, materialDescriptor + 4), 4096);

    const std::size_t materialStride = header.backwardCompatibility < 0x21 ? 0x58 : 0x2c;
    std::vector<MaterialUvMetadata> result;
    result.reserve(materialCount);

    for (std::uint32_t i = 0; i < materialCount; ++i) {
        const std::size_t materialOffset = static_cast<std::size_t>(materialsOffset) +
                                           static_cast<std::size_t>(i) * materialStride;
        if (materialOffset + materialStride > bytes.size()) break;

        MaterialUvMetadata metadata;
        const std::uint32_t parametersOffset = u32(bytes, materialOffset);
        if (parametersOffset && static_cast<std::uint64_t>(parametersOffset) + 36 <= bytes.size()) {
            // H3DMaterialParams starts with 12 bytes before TextureCoords[0].
            // H3DTextureCoord begins with four one-byte fields followed by
            // Scale.X, Scale.Y, Rotation, Translation.X and Translation.Y.
            const std::size_t coord = static_cast<std::size_t>(parametersOffset) + 12;
            metadata.flags = bytes[coord];
            metadata.transformType = bytes[coord + 1] <= 2 ? bytes[coord + 1] : 0;
            metadata.mappingType = bytes[coord + 2];
            metadata.referenceCamera = static_cast<std::int8_t>(bytes[coord + 3]);
        }
        result.push_back(metadata);
    }
    return result;
}

void applyMaterialUvMetadata(Scene& scene,
                             const rom::RomModelArchive& archive,
                             const std::string& virtualModelPath) {
    const auto metadata = readMaterialUvMetadata(archive.extractBch(virtualModelPath));
    auto& materials = scene.materialsMutable();
    const std::size_t count = std::min(materials.size(), metadata.size());
    for (std::size_t i = 0; i < count; ++i) {
        materials[i].uvFlags = metadata[i].flags;
        materials[i].uvTransformType = metadata[i].transformType;
        materials[i].uvMappingType = metadata[i].mappingType;
        materials[i].uvReferenceCamera = metadata[i].referenceCamera;
    }
}

} // namespace promptemon::bch
