#include "rom/CartridgeResourceSource.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace promptemon::rom {
namespace {

constexpr std::uint32_t kMaxDecoded = 8u * 1024u * 1024u;

std::vector<std::uint8_t> readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Could not open ROM: " + path.string());
    in.seekg(0, std::ios::end);
    const auto end = in.tellg();
    if (end < 0) throw std::runtime_error("Could not determine ROM size: " + path.string());
    const auto size = static_cast<std::uint64_t>(end);
    if (size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("ROM is too large to load: " + path.string());
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    in.seekg(0, std::ios::beg);
    if (!data.empty()) {
        in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (in.gcount() != static_cast<std::streamsize>(data.size())) {
            throw std::runtime_error("Unexpected end of ROM: " + path.string());
        }
    }
    return data;
}

std::string extensionOrBin(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    if (ext.empty()) return ".bin";
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

std::string hexOffset(std::size_t offset) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << offset;
    return out.str();
}

std::uint32_t u24(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 3 > data.size()) return 0;
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16);
}

std::uint16_t u16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 2 > data.size()) return 0;
    return static_cast<std::uint16_t>(data[offset]) |
           static_cast<std::uint16_t>(data[offset + 1] << 8);
}

struct CompressionRecord {
    enum class Type { GbaLz77, At4px };
    Type type = Type::GbaLz77;
    std::size_t offset = 0;
    std::uint32_t compressedSize = 0;
    std::uint32_t decodedSize = 0;
};

bool validateGbaLz77(const std::vector<std::uint8_t>& data, std::size_t offset, CompressionRecord& out) {
    if (offset + 4 > data.size() || data[offset] != 0x10) return false;
    const std::uint32_t target = u24(data, offset + 1);
    if (target < 32 || target > kMaxDecoded) return false;

    std::size_t pos = offset + 4;
    std::uint32_t produced = 0;
    while (produced < target) {
        if (pos >= data.size()) return false;
        const std::uint8_t flags = data[pos++];
        for (int bit = 7; bit >= 0 && produced < target; --bit) {
            if ((flags & (1u << bit)) != 0) {
                if (pos + 2 > data.size()) return false;
                const std::uint8_t a = data[pos++];
                const std::uint8_t b = data[pos++];
                const std::uint32_t length = (a >> 4) + 3u;
                const std::uint32_t back = (((a & 0x0Fu) << 8) | b) + 1u;
                if (back > produced) return false;
                produced = std::min(target, produced + length);
            } else {
                if (pos >= data.size()) return false;
                ++pos;
                ++produced;
            }
        }
    }

    const auto consumed = pos - offset;
    if (consumed < 8 || consumed >= static_cast<std::size_t>(target) + 4u || consumed > 0xFFFFFFFFu) return false;
    out = {CompressionRecord::Type::GbaLz77, offset, static_cast<std::uint32_t>(consumed), target};
    return true;
}

std::vector<std::uint8_t> decompressGbaLz77(const std::vector<std::uint8_t>& data, const CompressionRecord& rec) {
    if (rec.offset + rec.compressedSize > data.size()) throw std::runtime_error("Truncated GBA LZ77 resource");
    std::vector<std::uint8_t> out;
    out.reserve(rec.decodedSize);
    std::size_t pos = rec.offset + 4;
    while (out.size() < rec.decodedSize) {
        const std::uint8_t flags = data.at(pos++);
        for (int bit = 7; bit >= 0 && out.size() < rec.decodedSize; --bit) {
            if ((flags & (1u << bit)) != 0) {
                const std::uint8_t a = data.at(pos++);
                const std::uint8_t b = data.at(pos++);
                const std::size_t length = (a >> 4) + 3u;
                const std::size_t back = (((a & 0x0Fu) << 8) | b) + 1u;
                if (back > out.size()) throw std::runtime_error("Invalid GBA LZ77 lookback");
                for (std::size_t i = 0; i < length && out.size() < rec.decodedSize; ++i) {
                    out.push_back(out[out.size() - back]);
                }
            } else {
                out.push_back(data.at(pos++));
            }
        }
    }
    return out;
}

bool validateAt4px(const std::vector<std::uint8_t>& data, std::size_t offset, CompressionRecord& out) {
    static const char magic[] = "AT4PX";
    if (offset + 0x12 > data.size()) return false;
    for (std::size_t i = 0; i < 5; ++i) {
        if (data[offset + i] != static_cast<std::uint8_t>(magic[i])) return false;
    }
    const std::uint32_t compressed = u16(data, offset + 5);
    const std::uint32_t decoded = u16(data, offset + 0x10);
    if (compressed < 0x12 || decoded == 0 || decoded > kMaxDecoded || offset + compressed > data.size()) return false;
    out = {CompressionRecord::Type::At4px, offset, compressed, decoded};
    return true;
}

std::vector<std::uint8_t> decompressAt4px(const std::vector<std::uint8_t>& data, const CompressionRecord& rec) {
    if (rec.offset + rec.compressedSize > data.size() || rec.compressedSize < 0x12) {
        throw std::runtime_error("Truncated AT4PX resource");
    }

    std::uint8_t flags[9]{};
    for (std::size_t i = 0; i < 9; ++i) flags[i] = data[rec.offset + 7 + i];
    std::size_t pos = rec.offset + 0x12;
    const std::size_t end = rec.offset + rec.compressedSize;
    std::vector<std::uint8_t> out;
    out.reserve(rec.decodedSize);

    while (pos < end && out.size() < rec.decodedSize) {
        const std::uint8_t control = data[pos++];
        for (int bit = 7; bit >= 0 && pos < end && out.size() < rec.decodedSize; --bit) {
            if ((control & (1u << bit)) != 0) {
                out.push_back(data[pos++]);
                continue;
            }

            const std::uint8_t command = data[pos++];
            const std::uint8_t low = command & 0x0Fu;
            const std::uint8_t high = command >> 4;
            int matching = -1;
            for (int i = 0; i < 9; ++i) {
                if ((flags[i] & 0x0Fu) == high) {
                    matching = i;
                    break;
                }
            }

            if (matching >= 0) {
                std::uint8_t byte1 = 0;
                std::uint8_t byte2 = 0;
                if (matching == 0) {
                    byte1 = byte2 = static_cast<std::uint8_t>(low | (low << 4));
                } else {
                    int base = low;
                    if (matching == 1) ++base;
                    if (matching == 5) --base;
                    int nybble[4] = {base, base, base, base};
                    if (matching >= 1 && matching <= 4) --nybble[matching - 1];
                    else ++nybble[matching - 5];
                    byte1 = static_cast<std::uint8_t>(((nybble[0] & 0x0F) << 4) | (nybble[1] & 0x0F));
                    byte2 = static_cast<std::uint8_t>(((nybble[2] & 0x0F) << 4) | (nybble[3] & 0x0F));
                }
                out.push_back(byte1);
                if (out.size() < rec.decodedSize) out.push_back(byte2);
                continue;
            }

            if (pos >= end) throw std::runtime_error("Truncated AT4PX lookback");
            const int lookback = -0x1000 + (static_cast<int>(low) << 8) + data[pos++];
            const std::size_t count = static_cast<std::size_t>(high) + 3u;
            const auto start = static_cast<std::ptrdiff_t>(out.size()) + lookback;
            if (start < 0) throw std::runtime_error("Invalid AT4PX lookback");
            for (std::size_t i = 0; i < count && out.size() < rec.decodedSize; ++i) {
                const auto source = start + static_cast<std::ptrdiff_t>(i);
                if (source < 0 || static_cast<std::size_t>(source) >= out.size()) {
                    throw std::runtime_error("Invalid AT4PX copy");
                }
                out.push_back(out[static_cast<std::size_t>(source)]);
            }
        }
    }

    if (out.size() != rec.decodedSize) throw std::runtime_error("AT4PX did not reach declared output size");
    return out;
}

std::vector<std::uint8_t> grayscaleTga(std::uint16_t width, std::uint16_t height, std::vector<std::uint8_t> pixels) {
    if (pixels.size() != static_cast<std::size_t>(width) * height) {
        throw std::runtime_error("Invalid grayscale TGA dimensions");
    }
    std::vector<std::uint8_t> out(18 + pixels.size());
    out[2] = 3;
    out[12] = static_cast<std::uint8_t>(width & 0xFFu);
    out[13] = static_cast<std::uint8_t>(width >> 8);
    out[14] = static_cast<std::uint8_t>(height & 0xFFu);
    out[15] = static_cast<std::uint8_t>(height >> 8);
    out[16] = 8;
    out[17] = 0x20;
    std::copy(pixels.begin(), pixels.end(), out.begin() + 18);
    return out;
}

std::vector<std::uint8_t> gb2bppView(const std::vector<std::uint8_t>& bytes) {
    const std::size_t tiles = bytes.size() / 16u;
    if (tiles == 0) return {};
    const std::size_t columns = std::min<std::size_t>(32, tiles);
    const std::size_t rows = (tiles + columns - 1u) / columns;
    const std::size_t width = columns * 8u;
    const std::size_t height = rows * 8u;
    if (width > 0xFFFFu || height > 0xFFFFu) throw std::runtime_error("GB tile view is too large");

    std::vector<std::uint8_t> pixels(width * height, 255);
    static const std::uint8_t shades[4] = {255, 170, 85, 0};
    for (std::size_t tile = 0; tile < tiles; ++tile) {
        const std::size_t tx = (tile % columns) * 8u;
        const std::size_t ty = (tile / columns) * 8u;
        const std::size_t base = tile * 16u;
        for (std::size_t y = 0; y < 8; ++y) {
            const std::uint8_t lo = bytes[base + y * 2u];
            const std::uint8_t hi = bytes[base + y * 2u + 1u];
            for (std::size_t x = 0; x < 8; ++x) {
                const int bit = 7 - static_cast<int>(x);
                const std::uint8_t value = static_cast<std::uint8_t>(((hi >> bit) & 1u) * 2u + ((lo >> bit) & 1u));
                pixels[(ty + y) * width + tx + x] = shades[value];
            }
        }
    }
    return grayscaleTga(static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height), std::move(pixels));
}

std::vector<std::uint8_t> gba4bppView(const std::vector<std::uint8_t>& bytes) {
    const std::size_t tiles = bytes.size() / 32u;
    if (tiles == 0) return {};
    const std::size_t columns = std::min<std::size_t>(32, tiles);
    const std::size_t rows = (tiles + columns - 1u) / columns;
    const std::size_t width = columns * 8u;
    const std::size_t height = rows * 8u;
    if (width > 0xFFFFu || height > 0xFFFFu) throw std::runtime_error("GBA tile view is too large");

    std::vector<std::uint8_t> pixels(width * height, 0);
    for (std::size_t tile = 0; tile < tiles; ++tile) {
        const std::size_t tx = (tile % columns) * 8u;
        const std::size_t ty = (tile / columns) * 8u;
        const std::size_t base = tile * 32u;
        for (std::size_t y = 0; y < 8; ++y) {
            for (std::size_t pair = 0; pair < 4; ++pair) {
                const std::uint8_t value = bytes[base + y * 4u + pair];
                pixels[(ty + y) * width + tx + pair * 2u] = static_cast<std::uint8_t>((value & 0x0Fu) * 17u);
                pixels[(ty + y) * width + tx + pair * 2u + 1u] = static_cast<std::uint8_t>((value >> 4) * 17u);
            }
        }
    }
    return grayscaleTga(static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height), std::move(pixels));
}

std::vector<std::uint8_t> bgr555PaletteView(const std::vector<std::uint8_t>& bytes) {
    const std::size_t colors = bytes.size() / 2u;
    if (colors == 0) return {};
    const std::size_t columns = std::min<std::size_t>(16, colors);
    const std::size_t rows = (colors + columns - 1u) / columns;
    constexpr std::size_t cell = 8;
    const std::size_t width = columns * cell;
    const std::size_t height = rows * cell;
    if (width > 0xFFFFu || height > 0xFFFFu) throw std::runtime_error("Palette view is too large");

    std::vector<std::uint8_t> out(18 + width * height * 3u, 0);
    out[2] = 2;
    out[12] = static_cast<std::uint8_t>(width & 0xFFu);
    out[13] = static_cast<std::uint8_t>(width >> 8);
    out[14] = static_cast<std::uint8_t>(height & 0xFFu);
    out[15] = static_cast<std::uint8_t>(height >> 8);
    out[16] = 24;
    out[17] = 0x20;

    for (std::size_t index = 0; index < colors; ++index) {
        const std::uint16_t c = static_cast<std::uint16_t>(bytes[index * 2u]) |
                                static_cast<std::uint16_t>(bytes[index * 2u + 1u] << 8);
        const std::uint8_t r = static_cast<std::uint8_t>((c & 0x1Fu) * 255u / 31u);
        const std::uint8_t g = static_cast<std::uint8_t>(((c >> 5) & 0x1Fu) * 255u / 31u);
        const std::uint8_t b = static_cast<std::uint8_t>(((c >> 10) & 0x1Fu) * 255u / 31u);
        const std::size_t cx = (index % columns) * cell;
        const std::size_t cy = (index / columns) * cell;
        for (std::size_t y = 0; y < cell; ++y) {
            for (std::size_t x = 0; x < cell; ++x) {
                const std::size_t p = 18 + ((cy + y) * width + cx + x) * 3u;
                out[p] = b;
                out[p + 1] = g;
                out[p + 2] = r;
            }
        }
    }
    return out;
}

std::vector<std::uint8_t> asciiStrings(const std::vector<std::uint8_t>& data) {
    std::ostringstream text;
    text << "offset\ttext\n";
    for (std::size_t pos = 0; pos < data.size();) {
        if (data[pos] < 0x20 || data[pos] > 0x7E) {
            ++pos;
            continue;
        }
        const std::size_t start = pos;
        while (pos < data.size() && data[pos] >= 0x20 && data[pos] <= 0x7E) ++pos;
        if (pos - start < 4) continue;

        text << "0x" << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << start
             << std::dec << '\t';
        for (std::size_t i = start; i < pos; ++i) text << static_cast<char>(data[i]);
        text << '\n';
    }
    const std::string value = text.str();
    return std::vector<std::uint8_t>(value.begin(), value.end());
}

class CartridgeResourceSource final : public IRomResourceSource {
public:
    explicit CartridgeResourceSource(RomInfo info) : info_(std::move(info)), data_(readAll(info_.path)) {
        addSlice("raw/rom" + extensionOrBin(info_.path), "Complete cartridge image", ResourceKind::RawRom, 0, data_.size());

        if (info_.format == RomFormat::GameBoy || info_.format == RomFormat::GameBoyColor) {
            if (data_.size() >= 0x150) {
                addSlice("system/header.bin", "Game Boy cartridge header", ResourceKind::System, 0x100, 0x50);
            }
            indexGameBoy();
        } else if (info_.format == RomFormat::Gba) {
            if (data_.size() >= 0xC0) {
                addSlice("system/header.bin", "Game Boy Advance cartridge header", ResourceKind::System, 0, 0xC0);
            }
            indexGba();
        }

        addGenerated("analysis/ascii_strings.tsv", "Printable ASCII strings with ROM offsets", ResourceKind::Text,
                     Generated::AsciiStrings, 0, 0);
    }

    const RomInfo& romInfo() const noexcept override { return info_; }

    std::string providerName() const override {
        if (info_.format == RomFormat::Gba) return "GBA cartridge resources";
        return info_.format == RomFormat::GameBoyColor
            ? "Game Boy Color cartridge resources"
            : "Game Boy cartridge resources";
    }

    std::vector<RomResource> resources() const override { return resources_; }
    bool has(const std::string& id) const override { return entries_.find(id) != entries_.end(); }

    std::vector<std::uint8_t> extract(const std::string& id) const override {
        const auto found = entries_.find(id);
        if (found == entries_.end()) throw std::runtime_error("Cartridge resource not found: " + id);
        const Entry& entry = found->second;

        if (entry.type == EntryType::Slice) return slice(entry.offset, entry.size);

        if (entry.type == EntryType::Compressed) {
            const auto& rec = compressed_.at(entry.index);
            return rec.type == CompressionRecord::Type::GbaLz77
                ? decompressGbaLz77(data_, rec)
                : decompressAt4px(data_, rec);
        }

        if (entry.generated == Generated::AsciiStrings) return asciiStrings(data_);
        if (entry.generated == Generated::Gb2bpp) return gb2bppView(slice(entry.offset, entry.size));

        const auto& rec = compressed_.at(entry.index);
        const auto decoded = rec.type == CompressionRecord::Type::GbaLz77
            ? decompressGbaLz77(data_, rec)
            : decompressAt4px(data_, rec);
        if (entry.generated == Generated::Gba4bpp) return gba4bppView(decoded);
        if (entry.generated == Generated::Palette) return bgr555PaletteView(decoded);
        return {};
    }

private:
    enum class EntryType { Slice, Compressed, Generated };
    enum class Generated { None, AsciiStrings, Gb2bpp, Gba4bpp, Palette };

    struct Entry {
        EntryType type = EntryType::Slice;
        std::size_t offset = 0;
        std::size_t size = 0;
        std::size_t index = 0;
        Generated generated = Generated::None;
    };

    RomInfo info_;
    std::vector<std::uint8_t> data_;
    std::vector<RomResource> resources_;
    std::unordered_map<std::string, Entry> entries_;
    std::vector<CompressionRecord> compressed_;

    std::vector<std::uint8_t> slice(std::size_t offset, std::size_t size) const {
        if (offset + size > data_.size()) throw std::runtime_error("Cartridge slice is out of bounds");
        return {
            data_.begin() + static_cast<std::ptrdiff_t>(offset),
            data_.begin() + static_cast<std::ptrdiff_t>(offset + size)
        };
    }

    void addSlice(const std::string& id, const std::string& name, ResourceKind kind,
                  std::size_t offset, std::size_t size) {
        entries_[id] = {EntryType::Slice, offset, size, 0, Generated::None};
        resources_.push_back({id, name, kind, static_cast<std::uint64_t>(size)});
    }

    void addGenerated(const std::string& id, const std::string& name, ResourceKind kind,
                      Generated generated, std::size_t offset, std::size_t size,
                      std::size_t index = 0, std::uint64_t outputSize = 0) {
        entries_[id] = {EntryType::Generated, offset, size, index, generated};
        resources_.push_back({id, name, kind, outputSize});
    }

    void addCompressed(const CompressionRecord& rec, const std::string& family) {
        const std::size_t index = compressed_.size();
        compressed_.push_back(rec);
        const std::string stem = hexOffset(rec.offset);
        const std::string id = "decoded/" + family + "/" + stem + ".bin";
        entries_[id] = {EntryType::Compressed, 0, 0, index, Generated::None};
        resources_.push_back({id, family + " resource at 0x" + stem, ResourceKind::Data, rec.decodedSize});

        if (rec.decodedSize >= 32 && rec.decodedSize <= 512u * 1024u && (rec.decodedSize % 32u) == 0) {
            const auto pixels = static_cast<std::uint64_t>(rec.decodedSize) * 2u;
            addGenerated(
                "views/" + family + "/" + stem + "_4bpp.tga",
                family + " 4bpp tile view at 0x" + stem,
                ResourceKind::Graphics,
                Generated::Gba4bpp,
                0,
                0,
                index,
                18u + pixels);
        }

        if (rec.decodedSize >= 32 && rec.decodedSize <= 512 && (rec.decodedSize % 2u) == 0) {
            const std::size_t colors = rec.decodedSize / 2u;
            const std::size_t columns = std::min<std::size_t>(16, colors);
            const std::size_t rows = (colors + columns - 1u) / columns;
            const std::uint64_t pixels = columns * 8u * rows * 8u;
            addGenerated(
                "views/" + family + "/" + stem + "_bgr555.tga",
                family + " BGR555 palette view at 0x" + stem,
                ResourceKind::Palette,
                Generated::Palette,
                0,
                0,
                index,
                18u + pixels * 3u);
        }
    }

    void indexGameBoy() {
        constexpr std::size_t bankSize = 0x4000;
        const std::size_t banks = (data_.size() + bankSize - 1u) / bankSize;
        for (std::size_t bank = 0; bank < banks; ++bank) {
            const std::size_t offset = bank * bankSize;
            const std::size_t size = std::min(bankSize, data_.size() - offset);
            std::ostringstream number;
            number << std::setw(3) << std::setfill('0') << bank;
            const std::string stem = number.str();

            addSlice(
                "banks/bank_" + stem + ".bin",
                "16 KiB ROM bank " + stem,
                ResourceKind::Data,
                offset,
                size);

            const std::size_t tileBytes = size - (size % 16u);
            if (tileBytes >= 16) {
                const std::size_t tiles = tileBytes / 16u;
                const std::size_t columns = std::min<std::size_t>(32, tiles);
                const std::size_t rows = (tiles + columns - 1u) / columns;
                addGenerated(
                    "views/2bpp/bank_" + stem + ".tga",
                    "2bpp tile interpretation of ROM bank " + stem,
                    ResourceKind::Graphics,
                    Generated::Gb2bpp,
                    offset,
                    tileBytes,
                    0,
                    18u + static_cast<std::uint64_t>(columns * 8u * rows * 8u));
            }
        }
    }

    void indexGba() {
        for (std::size_t pos = 0; pos + 4 <= data_.size();) {
            if (data_[pos] != 0x10) {
                ++pos;
                continue;
            }
            CompressionRecord rec;
            if (validateGbaLz77(data_, pos, rec)) {
                addCompressed(rec, "gba_lz77");
                pos += rec.compressedSize;
            } else {
                ++pos;
            }
        }

        std::size_t pos = 0;
        static const std::uint8_t magic[] = {'A', 'T', '4', 'P', 'X'};
        while (pos + sizeof(magic) <= data_.size()) {
            const auto it = std::search(
                data_.begin() + static_cast<std::ptrdiff_t>(pos),
                data_.end(),
                std::begin(magic),
                std::end(magic));
            if (it == data_.end()) break;

            const std::size_t offset = static_cast<std::size_t>(it - data_.begin());
            CompressionRecord rec;
            if (validateAt4px(data_, offset, rec)) {
                try {
                    (void)decompressAt4px(data_, rec);
                    addCompressed(rec, "at4px");
                    pos = offset + rec.compressedSize;
                    continue;
                } catch (...) {
                }
            }
            pos = offset + 1;
        }
    }
};

} // namespace

std::unique_ptr<IRomResourceSource> openCartridgeResourceSource(const RomInfo& rom) {
    if (rom.format != RomFormat::GameBoy &&
        rom.format != RomFormat::GameBoyColor &&
        rom.format != RomFormat::Gba) {
        return nullptr;
    }
    return std::make_unique<CartridgeResourceSource>(rom);
}

} // namespace promptemon::rom
