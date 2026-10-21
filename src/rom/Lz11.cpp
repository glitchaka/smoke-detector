#include "rom/Lz11.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace promptemon::rom {
namespace {

std::uint32_t read24(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 3 > data.size()) throw std::runtime_error("Truncated LZ11 header");
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16);
}

std::uint32_t read32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 4 > data.size()) throw std::runtime_error("Truncated LZ11 header");
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

} // namespace

bool Lz11::isCompressed(const std::vector<std::uint8_t>& data) noexcept {
    return data.size() >= 4 && data[0] == 0x11;
}

std::vector<std::uint8_t> Lz11::decompress(const std::vector<std::uint8_t>& data) {
    if (!isCompressed(data)) throw std::runtime_error("Not an LZ11 stream");

    std::size_t input = 1;
    std::uint32_t outputSize = read24(data, input);
    input += 3;
    if (outputSize == 0) {
        outputSize = read32(data, input);
        input += 4;
    }
    if (outputSize == 0) return {};

    std::vector<std::uint8_t> output;
    output.reserve(outputSize);

    while (output.size() < outputSize) {
        if (input >= data.size()) throw std::runtime_error("Truncated LZ11 flag byte");
        const std::uint8_t flags = data[input++];

        for (int bit = 7; bit >= 0 && output.size() < outputSize; --bit) {
            if ((flags & (1u << bit)) == 0) {
                if (input >= data.size()) throw std::runtime_error("Truncated LZ11 literal");
                output.push_back(data[input++]);
                continue;
            }

            if (input + 2 > data.size()) throw std::runtime_error("Truncated LZ11 backreference");
            const std::uint8_t b1 = data[input++];
            const int high = b1 >> 4;
            std::size_t length = 0;
            std::size_t displacement = 0;

            if (high == 0) {
                if (input + 2 > data.size()) throw std::runtime_error("Truncated LZ11 long backreference");
                const std::uint8_t b2 = data[input++];
                const std::uint8_t b3 = data[input++];
                length = static_cast<std::size_t>(((b1 & 0x0F) << 4) | (b2 >> 4)) + 0x11u;
                displacement = static_cast<std::size_t>(((b2 & 0x0F) << 8) | b3) + 1u;
            } else if (high == 1) {
                if (input + 3 > data.size()) throw std::runtime_error("Truncated LZ11 very long backreference");
                const std::uint8_t b2 = data[input++];
                const std::uint8_t b3 = data[input++];
                const std::uint8_t b4 = data[input++];
                length = static_cast<std::size_t>(((b1 & 0x0F) << 12) | (b2 << 4) | (b3 >> 4)) + 0x111u;
                displacement = static_cast<std::size_t>(((b3 & 0x0F) << 8) | b4) + 1u;
            } else {
                const std::uint8_t b2 = data[input++];
                length = static_cast<std::size_t>(high) + 1u;
                displacement = static_cast<std::size_t>(((b1 & 0x0F) << 8) | b2) + 1u;
            }

            if (displacement == 0 || displacement > output.size()) {
                throw std::runtime_error("Invalid LZ11 backreference distance");
            }

            for (std::size_t i = 0; i < length && output.size() < outputSize; ++i) {
                output.push_back(output[output.size() - displacement]);
            }
        }
    }

    return output;
}

} // namespace promptemon::rom
