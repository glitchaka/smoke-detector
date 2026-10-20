#include "rom/Lzh8.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace promptemon::rom {
namespace {

class BitReader {
public:
    BitReader(const std::vector<std::uint8_t>& data, std::size_t offset)
        : data_(data), offset_(offset) {}

    std::uint32_t read(unsigned count) {
        std::uint32_t out = 0;
        unsigned produced = 0;
        while (produced < count) {
            if (bitsLeft_ == 0) {
                if (offset_ >= data_.size()) {
                    throw std::runtime_error("Unexpected end of LZH8 stream");
                }
                bitPool_ = data_[offset_++];
                bitsLeft_ = 8;
            }

            const unsigned take = std::min<unsigned>(bitsLeft_, count - produced);
            out <<= take;
            out |= (bitPool_ >> (bitsLeft_ - take)) & ((1u << take) - 1u);
            bitsLeft_ -= take;
            produced += take;
        }
        return out;
    }

    std::size_t offset() const noexcept { return offset_; }
    void seek(std::size_t offset) noexcept {
        offset_ = offset;
        bitPool_ = 0;
        bitsLeft_ = 0;
    }

private:
    const std::vector<std::uint8_t>& data_;
    std::size_t offset_ = 0;
    std::uint8_t bitPool_ = 0;
    unsigned bitsLeft_ = 0;
};

std::uint16_t read16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 2 > data.size()) throw std::runtime_error("Truncated LZH8 header");
    return static_cast<std::uint16_t>(data[offset]) |
           static_cast<std::uint16_t>(data[offset + 1] << 8);
}

std::uint32_t read32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 4 > data.size()) throw std::runtime_error("Truncated LZH8 header");
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

} // namespace

bool Lzh8::isCompressed(const std::vector<std::uint8_t>& data) noexcept {
    return !data.empty() && data[0] == 0x40;
}

std::vector<std::uint8_t> Lzh8::decompress(const std::vector<std::uint8_t>& data) {
    if (!isCompressed(data) || data.size() < 4) {
        throw std::runtime_error("Not an LZH8 stream");
    }

    std::size_t offset = 0;
    const std::uint32_t header = read32(data, offset);
    offset += 4;

    std::uint32_t outputSize = header >> 8;
    if (outputSize == 0) {
        outputSize = read32(data, offset);
        offset += 4;
    }

    constexpr unsigned kLengthBits = 9;
    constexpr unsigned kDisplacementBits = 5;
    std::vector<std::uint16_t> lengthTable(1024, 0);
    std::vector<std::uint8_t> displacementTable(64, 0);

    const std::size_t lengthStart = offset;
    const std::size_t lengthBytes = (static_cast<std::size_t>(read16(data, offset)) + 1u) * 4u;
    offset += 2;
    BitReader bits(data, offset);
    for (std::size_t i = 1; bits.offset() - lengthStart < lengthBytes && i < lengthTable.size(); ++i) {
        lengthTable[i] = static_cast<std::uint16_t>(bits.read(kLengthBits));
    }
    offset = lengthStart + lengthBytes;

    if (offset >= data.size()) throw std::runtime_error("Truncated LZH8 displacement table");
    const std::size_t displacementStart = offset;
    const std::size_t displacementBytes = (static_cast<std::size_t>(data[offset]) + 1u) * 4u;
    ++offset;
    bits.seek(offset);
    for (std::size_t i = 1; bits.offset() - displacementStart < displacementBytes && i < displacementTable.size(); ++i) {
        displacementTable[i] = static_cast<std::uint8_t>(bits.read(kDisplacementBits));
    }
    offset = displacementStart + displacementBytes;
    bits.seek(offset);

    std::vector<std::uint8_t> output;
    output.reserve(outputSize);

    while (output.size() < outputSize) {
        std::size_t lengthIndex = 1;
        for (;;) {
            const unsigned child = bits.read(1);
            const unsigned payload = lengthTable[lengthIndex] & 0x7Fu;
            const std::size_t nextIndex = (lengthIndex / 2u * 2u) + (payload + 1u) * 2u + child;
            if (nextIndex >= lengthTable.size()) throw std::runtime_error("Invalid LZH8 length tree");

            const bool leaf = (lengthTable[lengthIndex] & (0x100u >> child)) != 0;
            if (!leaf) {
                if (nextIndex == lengthIndex) throw std::runtime_error("Invalid LZH8 length tree loop");
                lengthIndex = nextIndex;
                continue;
            }

            std::uint16_t length = lengthTable[nextIndex];
            if (length < 0x100u) {
                output.push_back(static_cast<std::uint8_t>(length));
                break;
            }

            length = static_cast<std::uint16_t>((length & 0xFFu) + 3u);
            std::size_t displacementIndex = 1;
            for (;;) {
                const unsigned displacementChild = bits.read(1);
                const unsigned displacementPayload = displacementTable[displacementIndex] & 0x7u;
                const std::size_t nextDisplacementIndex =
                    (displacementIndex / 2u * 2u) + (displacementPayload + 1u) * 2u + displacementChild;
                if (nextDisplacementIndex >= displacementTable.size()) {
                    throw std::runtime_error("Invalid LZH8 displacement tree");
                }

                const bool displacementLeaf =
                    (displacementTable[displacementIndex] & (0x10u >> displacementChild)) != 0;
                if (!displacementLeaf) {
                    if (nextDisplacementIndex == displacementIndex) {
                        throw std::runtime_error("Invalid LZH8 displacement tree loop");
                    }
                    displacementIndex = nextDisplacementIndex;
                    continue;
                }

                const std::uint8_t displacementLength = displacementTable[nextDisplacementIndex];
                std::size_t displacement = 0;
                if (displacementLength != 0) {
                    displacement = 1;
                    for (unsigned i = 1; i < displacementLength; ++i) {
                        displacement = (displacement << 1u) | bits.read(1);
                    }
                }

                const std::size_t distance = displacement + 1u;
                if (distance > output.size()) throw std::runtime_error("Invalid LZH8 backreference");

                for (std::uint16_t i = 0; i < length && output.size() < outputSize; ++i) {
                    output.push_back(output[output.size() - distance]);
                }
                break;
            }
            break;
        }
    }

    return output;
}

} // namespace promptemon::rom
