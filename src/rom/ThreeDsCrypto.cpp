#include "rom/ThreeDsCrypto.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace promptemon::rom {
namespace {

using Block = std::array<std::uint8_t, 16>;

std::uint16_t le16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 2 > data.size()) return 0;
    return static_cast<std::uint16_t>(data[offset]) |
           static_cast<std::uint16_t>(data[offset + 1] << 8);
}

std::uint32_t le32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

bool magicAt(const std::vector<std::uint8_t>& data, std::size_t offset, const char* magic) {
    for (std::size_t i = 0; magic[i]; ++i) {
        if (offset + i >= data.size() || data[offset + i] != static_cast<std::uint8_t>(magic[i])) return false;
    }
    return true;
}

std::vector<std::uint8_t> readRange(const std::filesystem::path& path,
                                    std::uint64_t offset,
                                    std::size_t size) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!in) return {};
    std::vector<std::uint8_t> out(size);
    if (size) {
        in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
        if (in.gcount() != static_cast<std::streamsize>(size)) return {};
    }
    return out;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

bool parseHexBlock(std::string value, Block& out) {
    value = trim(value);
    if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0) value.erase(0, 2);
    if (value.size() != 32) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < out.size(); ++i) {
        const int hi = nibble(value[i * 2]);
        const int lo = nibble(value[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return true;
}

struct KeyFile {
    std::unordered_map<int, Block> keyX;
    std::unordered_map<int, Block> normal;
    std::filesystem::path path;
};

std::optional<KeyFile> loadKeys(const std::filesystem::path& romPath) {
    std::vector<std::filesystem::path> candidates = {
        romPath.parent_path() / "aes_keys.txt",
        romPath.parent_path() / "keys.txt",
        std::filesystem::current_path() / "aes_keys.txt",
        std::filesystem::current_path() / "keys.txt"
    };

    for (const auto& candidate : candidates) {
        std::ifstream in(candidate);
        if (!in) continue;
        KeyFile keys;
        keys.path = candidate;
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == ':') continue;
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string name = lower(trim(line.substr(0, eq)));
            const std::string value = trim(line.substr(eq + 1));

            int slot = -1;
            if (name.find("slot0x2c") != std::string::npos) slot = 0x2C;
            else if (name.find("slot0x25") != std::string::npos) slot = 0x25;
            else if (name.find("slot0x18") != std::string::npos) slot = 0x18;
            else if (name.find("slot0x1b") != std::string::npos) slot = 0x1B;
            if (slot < 0) continue;

            Block block{};
            if (!parseHexBlock(value, block)) continue;
            if (name.find("keynormal") != std::string::npos || name.find("normalkey") != std::string::npos) {
                keys.normal[slot] = block;
            } else if (name.find("keyx") != std::string::npos) {
                keys.keyX[slot] = block;
            }
        }
        if (!keys.keyX.empty() || !keys.normal.empty()) return keys;
    }
    return std::nullopt;
}

struct U128 {
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;
};

U128 fromBlock(const Block& value) {
    U128 out{};
    for (int i = 0; i < 8; ++i) out.hi = (out.hi << 8) | value[static_cast<std::size_t>(i)];
    for (int i = 8; i < 16; ++i) out.lo = (out.lo << 8) | value[static_cast<std::size_t>(i)];
    return out;
}

Block toBlock(U128 value) {
    Block out{};
    for (int i = 7; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value.hi & 0xFFu);
        value.hi >>= 8;
    }
    for (int i = 15; i >= 8; --i) {
        out[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(value.lo & 0xFFu);
        value.lo >>= 8;
    }
    return out;
}

U128 rotl128(U128 v, unsigned n) {
    n &= 127u;
    if (!n) return v;
    if (n == 64) return {v.lo, v.hi};
    if (n < 64) return {(v.hi << n) | (v.lo >> (64 - n)),
                        (v.lo << n) | (v.hi >> (64 - n))};
    const unsigned m = n - 64;
    return {(v.lo << m) | (v.hi >> (64 - m)),
            (v.hi << m) | (v.lo >> (64 - m))};
}

U128 xor128(U128 a, U128 b) { return {a.hi ^ b.hi, a.lo ^ b.lo}; }

U128 add128(U128 a, U128 b) {
    U128 out{};
    out.lo = a.lo + b.lo;
    out.hi = a.hi + b.hi + (out.lo < a.lo ? 1u : 0u);
    return out;
}

Block deriveNormalKey(const Block& keyX, const Block& keyY) {
    const U128 constant{0x1FF9E9AAC5FE0408ull, 0x024591DC5D52768Aull};
    U128 value = xor128(rotl128(fromBlock(keyX), 2), fromBlock(keyY));
    value = add128(value, constant);
    value = rotl128(value, 87); // ROR 41 in 128-bit arithmetic.
    return toBlock(value);
}

constexpr std::uint8_t kSbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

std::uint8_t xtime(std::uint8_t x) {
    return static_cast<std::uint8_t>((x << 1) ^ ((x & 0x80u) ? 0x1Bu : 0u));
}

void expandKey(const Block& key, std::array<std::uint8_t, 176>& roundKeys) {
    std::copy(key.begin(), key.end(), roundKeys.begin());
    static const std::uint8_t rcon[10] = {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1B,0x36};
    std::size_t generated = 16;
    int rconIndex = 0;
    std::uint8_t temp[4]{};
    while (generated < roundKeys.size()) {
        for (int i = 0; i < 4; ++i) temp[i] = roundKeys[generated - 4 + static_cast<std::size_t>(i)];
        if ((generated % 16) == 0) {
            const std::uint8_t first = temp[0];
            temp[0] = kSbox[temp[1]];
            temp[1] = kSbox[temp[2]];
            temp[2] = kSbox[temp[3]];
            temp[3] = kSbox[first];
            temp[0] ^= rcon[rconIndex++];
        }
        for (int i = 0; i < 4; ++i) {
            roundKeys[generated] = roundKeys[generated - 16] ^ temp[i];
            ++generated;
        }
    }
}

void addRoundKey(Block& state, const std::array<std::uint8_t,176>& keys, int round) {
    const std::size_t base = static_cast<std::size_t>(round) * 16;
    for (std::size_t i = 0; i < 16; ++i) state[i] ^= keys[base + i];
}

void subBytes(Block& state) {
    for (auto& b : state) b = kSbox[b];
}

void shiftRows(Block& s) {
    Block t = s;
    s[0]=t[0];  s[4]=t[4];  s[8]=t[8];   s[12]=t[12];
    s[1]=t[5];  s[5]=t[9];  s[9]=t[13];  s[13]=t[1];
    s[2]=t[10]; s[6]=t[14]; s[10]=t[2];  s[14]=t[6];
    s[3]=t[15]; s[7]=t[3];  s[11]=t[7];  s[15]=t[11];
}

void mixColumns(Block& s) {
    for (int c = 0; c < 4; ++c) {
        const int i = c * 4;
        const std::uint8_t a0=s[i], a1=s[i+1], a2=s[i+2], a3=s[i+3];
        const std::uint8_t x = a0 ^ a1 ^ a2 ^ a3;
        s[i]   ^= x ^ xtime(static_cast<std::uint8_t>(a0 ^ a1));
        s[i+1] ^= x ^ xtime(static_cast<std::uint8_t>(a1 ^ a2));
        s[i+2] ^= x ^ xtime(static_cast<std::uint8_t>(a2 ^ a3));
        s[i+3] ^= x ^ xtime(static_cast<std::uint8_t>(a3 ^ a0));
    }
}

Block aesEncrypt(const Block& input, const Block& key) {
    std::array<std::uint8_t,176> keys{};
    expandKey(key, keys);
    Block state = input;
    addRoundKey(state, keys, 0);
    for (int round = 1; round < 10; ++round) {
        subBytes(state);
        shiftRows(state);
        mixColumns(state);
        addRoundKey(state, keys, round);
    }
    subBytes(state);
    shiftRows(state);
    addRoundKey(state, keys, 10);
    return state;
}

void addCounter(Block& counter, std::uint64_t blocks) {
    std::uint64_t carry = blocks;
    for (int i = 15; i >= 0 && carry; --i) {
        const std::uint64_t sum = static_cast<std::uint64_t>(counter[static_cast<std::size_t>(i)]) + (carry & 0xFFu);
        counter[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(sum & 0xFFu);
        carry = (carry >> 8) + (sum >> 8);
    }
}

void cryptCtr(std::vector<std::uint8_t>& data,
              const Block& key,
              Block counter,
              std::uint64_t blockOffset = 0) {
    addCounter(counter, blockOffset);
    std::size_t pos = 0;
    while (pos < data.size()) {
        const Block stream = aesEncrypt(counter, key);
        const std::size_t count = std::min<std::size_t>(16, data.size() - pos);
        for (std::size_t i = 0; i < count; ++i) data[pos + i] ^= stream[i];
        pos += count;
        addCounter(counter, 1);
    }
}

struct Partition {
    std::uint64_t base = 0;
    std::uint64_t size = 0;
    std::vector<std::uint8_t> header;
};

std::optional<Partition> findProgramPartition(const std::filesystem::path& path) {
    std::error_code ec;
    const std::uint64_t fileSize = std::filesystem::file_size(path, ec);
    if (ec || fileSize < 0x200) return std::nullopt;
    const auto outer = readRange(path, 0, static_cast<std::size_t>(std::min<std::uint64_t>(fileSize, 0x400)));
    if (outer.size() < 0x200) return std::nullopt;

    auto make = [&](std::uint64_t base, std::uint64_t hintedSize) -> std::optional<Partition> {
        if (base + 0x200 > fileSize) return std::nullopt;
        auto header = readRange(path, base, 0x200);
        if (!magicAt(header, 0x100, "NCCH")) return std::nullopt;
        const std::uint64_t contentSize = static_cast<std::uint64_t>(le32(header, 0x104)) * 0x200u;
        const std::uint64_t size = std::min<std::uint64_t>(hintedSize ? hintedSize : contentSize, fileSize - base);
        const std::uint64_t romFsOffset = static_cast<std::uint64_t>(le32(header, 0x1B0)) * 0x200u;
        const std::uint64_t romFsSize = static_cast<std::uint64_t>(le32(header, 0x1B4)) * 0x200u;
        if (!romFsOffset || !romFsSize || romFsOffset + 4 > size) return std::nullopt;
        return Partition{base, size, std::move(header)};
    };

    if (magicAt(outer, 0x100, "NCCH")) return make(0, fileSize);
    if (!magicAt(outer, 0x100, "NCSD")) return std::nullopt;

    for (std::size_t i = 0; i < 8; ++i) {
        const std::uint64_t base = static_cast<std::uint64_t>(le32(outer, 0x120 + i * 8)) * 0x200u;
        const std::uint64_t size = static_cast<std::uint64_t>(le32(outer, 0x124 + i * 8)) * 0x200u;
        if (!base || !size) continue;
        if (auto partition = make(base, size)) return partition;
    }
    return std::nullopt;
}

int cryptoSlot(const std::vector<std::uint8_t>& header) {
    if (header.size() <= 0x18F) return -1;
    switch (header[0x18B]) {
        case 0x00: return 0x2C;
        case 0x01: return 0x25;
        case 0x0A: return 0x18;
        case 0x0B: return 0x1B;
        default: return -1;
    }
}

Block romFsCounter(const std::vector<std::uint8_t>& header, std::uint64_t romFsOffset) {
    Block ctr{};
    const std::uint16_t version = le16(header, 0x112);
    if (version == 1) {
        for (int i = 0; i < 8; ++i) ctr[static_cast<std::size_t>(i)] = header[0x108 + static_cast<std::size_t>(i)];
        const std::uint32_t offset = static_cast<std::uint32_t>(romFsOffset);
        ctr[12] = static_cast<std::uint8_t>((offset >> 24) & 0xFFu);
        ctr[13] = static_cast<std::uint8_t>((offset >> 16) & 0xFFu);
        ctr[14] = static_cast<std::uint8_t>((offset >> 8) & 0xFFu);
        ctr[15] = static_cast<std::uint8_t>(offset & 0xFFu);
    } else {
        for (int i = 0; i < 8; ++i) ctr[static_cast<std::size_t>(i)] = header[0x10F - static_cast<std::size_t>(i)];
        ctr[8] = 3;
    }
    return ctr;
}

std::uint64_t fnv1a(const std::string& text) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::filesystem::path cachePathFor(const RomInfo& rom, const Partition& partition) {
    std::error_code ec;
    const auto modified = std::filesystem::last_write_time(rom.path, ec);
    const auto ticks = ec ? 0ll : modified.time_since_epoch().count();
    const std::string signature = rom.path.u8string() + "|" + std::to_string(rom.size) + "|" +
                                  std::to_string(ticks) + "|" + std::to_string(partition.base) + "|" +
                                  std::to_string(partition.size);
    std::ostringstream name;
    name << rom.path.stem().u8string() << "-" << std::hex << fnv1a(signature) << ".decrypted.cxi";
    auto root = std::filesystem::temp_directory_path(ec) / "smoke-detector" / "3ds";
    std::filesystem::create_directories(root, ec);
    return root / std::filesystem::u8path(name.str());
}

bool cachedLooksReadable(const std::filesystem::path& path, std::uint64_t romFsOffset) {
    const auto bytes = readRange(path, romFsOffset, 4);
    return magicAt(bytes, 0, "IVFC");
}

bool copyPartition(const std::filesystem::path& source,
                   const std::filesystem::path& destination,
                   const Partition& partition) {
    std::ifstream in(source, std::ios::binary);
    if (!in) return false;
    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    in.seekg(static_cast<std::streamoff>(partition.base), std::ios::beg);
    constexpr std::size_t kChunk = 1024u * 1024u;
    std::vector<char> buffer(kChunk);
    std::uint64_t remaining = partition.size;
    while (remaining) {
        const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
        in.read(buffer.data(), static_cast<std::streamsize>(count));
        if (in.gcount() != static_cast<std::streamsize>(count)) return false;
        out.write(buffer.data(), static_cast<std::streamsize>(count));
        if (!out) return false;
        remaining -= count;
    }
    return true;
}

bool decryptRomFsToCopy(const std::filesystem::path& source,
                        const std::filesystem::path& destination,
                        const Partition& partition,
                        const Block& normalKey,
                        const Block& counter) {
    const std::uint64_t romFsOffset = static_cast<std::uint64_t>(le32(partition.header, 0x1B0)) * 0x200u;
    const std::uint64_t romFsSize = static_cast<std::uint64_t>(le32(partition.header, 0x1B4)) * 0x200u;

    std::ifstream in(source, std::ios::binary);
    std::fstream out(destination, std::ios::binary | std::ios::in | std::ios::out);
    if (!in || !out) return false;
    constexpr std::size_t kChunk = 1024u * 1024u;
    std::vector<std::uint8_t> buffer(kChunk);
    std::uint64_t position = 0;
    while (position < romFsSize) {
        const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(kChunk, romFsSize - position));
        buffer.resize(count);
        in.seekg(static_cast<std::streamoff>(partition.base + romFsOffset + position), std::ios::beg);
        in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(count));
        if (in.gcount() != static_cast<std::streamsize>(count)) return false;
        cryptCtr(buffer, normalKey, counter, position / 16u);
        out.seekp(static_cast<std::streamoff>(romFsOffset + position), std::ios::beg);
        out.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(count));
        if (!out) return false;
        position += count;
    }

    std::uint8_t flag = partition.header[0x18F] | 0x04u;
    out.seekp(0x18F, std::ios::beg);
    out.write(reinterpret_cast<const char*>(&flag), 1);
    return static_cast<bool>(out);
}

} // namespace

ThreeDsReadableImage prepareReadableThreeDsImage(const RomInfo& rom) {
    ThreeDsReadableImage result;
    result.path = rom.path;
    if (rom.format != RomFormat::Nintendo3DsImage) {
        result.diagnostic = "3DS image preparation not required for this container";
        return result;
    }

    const auto partition = findProgramPartition(rom.path);
    if (!partition) {
        result.diagnostic = "No NCCH partition with RomFS was found";
        return result;
    }

    const std::uint64_t romFsOffset = static_cast<std::uint64_t>(le32(partition->header, 0x1B0)) * 0x200u;
    const auto rawMagic = readRange(rom.path, partition->base + romFsOffset, 4);
    if (magicAt(rawMagic, 0, "IVFC")) {
        result.diagnostic = "RomFS is already plaintext";
        return result;
    }

    result.encrypted = true;
    if (partition->header.size() <= 0x18F) {
        result.diagnostic = "NCCH header is truncated";
        return result;
    }
    if ((partition->header[0x18F] & 0x04u) != 0) {
        result.diagnostic = "NCCH says NoCrypto but RomFS is not readable";
        return result;
    }
    if ((partition->header[0x18F] & 0x20u) != 0) {
        result.diagnostic = "Seed-encrypted NCCH detected; seeddb support is required";
        return result;
    }
    if ((partition->header[0x18F] & 0x01u) != 0) {
        result.diagnostic = "Fixed-key NCCH is not supported by the user-key path";
        return result;
    }

    const int slot = cryptoSlot(partition->header);
    if (slot < 0) {
        result.diagnostic = "Unsupported NCCH crypto method";
        return result;
    }

    const auto keys = loadKeys(rom.path);
    if (!keys) {
        result.diagnostic = "Encrypted 3DS RomFS: place your aes_keys.txt beside the ROM or Smoke Detector";
        return result;
    }

    Block normalKey{};
    if (const auto normal = keys->normal.find(slot); normal != keys->normal.end()) {
        normalKey = normal->second;
    } else {
        const auto keyX = keys->keyX.find(slot);
        if (keyX == keys->keyX.end()) {
            std::ostringstream message;
            message << "aes_keys.txt does not contain slot0x" << std::hex << std::uppercase << slot << "KeyX";
            result.diagnostic = message.str();
            return result;
        }
        Block keyY{};
        std::copy_n(partition->header.begin(), 16, keyY.begin());
        normalKey = deriveNormalKey(keyX->second, keyY);
    }

    const Block counter = romFsCounter(partition->header, romFsOffset);
    auto probe = readRange(rom.path, partition->base + romFsOffset, 0x200);
    if (probe.empty()) {
        result.diagnostic = "Could not read encrypted RomFS";
        return result;
    }
    cryptCtr(probe, normalKey, counter);
    if (!magicAt(probe, 0, "IVFC")) {
        result.diagnostic = "The supplied 3DS key does not decrypt this RomFS";
        return result;
    }

    const auto cached = cachePathFor(rom, *partition);
    if (std::filesystem::is_regular_file(cached) && cachedLooksReadable(cached, romFsOffset)) {
        result.path = cached;
        result.temporary = true;
        result.diagnostic = "Using cached decrypted NCCH from " + keys->path.string();
        return result;
    }

    std::error_code ec;
    std::filesystem::create_directories(cached.parent_path(), ec);
    const auto temporary = cached.string() + ".tmp";
    std::filesystem::remove(temporary, ec);
    if (!copyPartition(rom.path, temporary, *partition) ||
        !decryptRomFsToCopy(rom.path, temporary, *partition, normalKey, counter) ||
        !cachedLooksReadable(temporary, romFsOffset)) {
        std::filesystem::remove(temporary, ec);
        result.diagnostic = "Failed while creating a readable 3DS cache image";
        return result;
    }
    std::filesystem::remove(cached, ec);
    ec.clear();
    std::filesystem::rename(temporary, cached, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        result.diagnostic = "Could not finalize the readable 3DS cache image";
        return result;
    }

    result.path = cached;
    result.temporary = true;
    result.diagnostic = "Encrypted RomFS decrypted with " + keys->path.string();
    return result;
}

} // namespace promptemon::rom
