#include "rom/IRomResourceSource.h"

#include "rom/RumbleWorldArchive.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace promptemon::rom {
namespace {

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

std::vector<std::uint8_t> readRange(const std::filesystem::path& path, std::uint64_t offset, std::uint64_t size) {
    if (size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("Resource is too large to load in memory: " + path.string());
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Could not open ROM: " + path.string());
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) throw std::runtime_error("Could not seek in ROM: " + path.string());

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
            throw std::runtime_error("Unexpected end of ROM while reading resource");
        }
    }
    return bytes;
}

std::string extensionOrBin(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    if (ext.empty()) return ".bin";
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

ResourceKind classifyPath(std::string path) {
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    const auto ends = [&path](const char* suffix) {
        const std::string s(suffix);
        return path.size() >= s.size() && path.compare(path.size() - s.size(), s.size(), s) == 0;
    };
    if (ends(".bch") || ends(".bch.cx") || ends(".bcmdl") || ends(".bcres")) return ResourceKind::Model;
    if (ends(".bcwav") || ends(".cwav") || ends(".bcstm") || ends(".cstm") ||
        ends(".bcsar") || ends(".csar") || ends(".bcseq") || ends(".cseq") ||
        ends(".wav") || ends(".ogg") || ends(".dsp")) return ResourceKind::Audio;
    if (ends(".png") || ends(".tga") || ends(".bmp") || ends(".jpg") || ends(".jpeg") ||
        ends(".bclim") || ends(".ctpk")) return ResourceKind::Texture;
    if (ends(".garc") || ends(".narc") || ends(".arc") || ends(".szs")) return ResourceKind::Archive;
    return ResourceKind::File;
}

class RawRomResourceSource final : public IRomResourceSource {
public:
    explicit RawRomResourceSource(RomInfo info) : info_(std::move(info)) {
        const std::uint64_t size = static_cast<std::uint64_t>(info_.size);
        resources_.push_back({"raw/rom" + extensionOrBin(info_.path), "Complete ROM image", ResourceKind::RawRom, size});

        if (info_.format == RomFormat::GameBoy || info_.format == RomFormat::GameBoyColor) {
            if (size >= 0x150) resources_.push_back({"system/header.bin", "Game Boy cartridge header", ResourceKind::System, 0x50});
        } else if (info_.format == RomFormat::Gba) {
            if (size >= 0xC0) resources_.push_back({"system/header.bin", "Game Boy Advance cartridge header", ResourceKind::System, 0xC0});
        } else if (size >= 0x200) {
            resources_.push_back({"system/header.bin", "ROM header", ResourceKind::System, 0x200});
        }
    }

    const RomInfo& romInfo() const noexcept override { return info_; }
    std::string providerName() const override { return "Raw cartridge image"; }
    std::vector<RomResource> resources() const override { return resources_; }

    bool has(const std::string& resourceId) const override {
        return std::any_of(resources_.begin(), resources_.end(), [&](const RomResource& r) { return r.id == resourceId; });
    }

    std::vector<std::uint8_t> extract(const std::string& resourceId) const override {
        if (resourceId.rfind("raw/rom", 0) == 0) return readRange(info_.path, 0, static_cast<std::uint64_t>(info_.size));
        if (resourceId == "system/header.bin") {
            if (info_.format == RomFormat::GameBoy || info_.format == RomFormat::GameBoyColor) return readRange(info_.path, 0x100, 0x50);
            if (info_.format == RomFormat::Gba) return readRange(info_.path, 0, 0xC0);
            return readRange(info_.path, 0, std::min<std::uint64_t>(0x200, static_cast<std::uint64_t>(info_.size)));
        }
        throw std::runtime_error("Unknown raw ROM resource: " + resourceId);
    }

private:
    RomInfo info_;
    std::vector<RomResource> resources_;
};

class ThreeDsRomFsResourceSource final : public IRomResourceSource {
public:
    explicit ThreeDsRomFsResourceSource(RomInfo info) : info_(std::move(info)) {
        archive_ = std::make_unique<RumbleWorldArchive>();
        if (!archive_->open(info_.path)) throw std::runtime_error("No readable 3DS RomFS");
        for (const auto& path : archive_->resources()) {
            resources_.push_back({"romfs/" + path, path, classifyPath(path), 0});
        }
        if (resources_.empty()) throw std::runtime_error("3DS RomFS is empty");
    }

    const RomInfo& romInfo() const noexcept override { return info_; }
    std::string providerName() const override { return "Nintendo 3DS RomFS"; }
    std::vector<RomResource> resources() const override { return resources_; }

    bool has(const std::string& resourceId) const override {
        return resourceId.rfind("romfs/", 0) == 0 && archive_ && archive_->has(resourceId.substr(6));
    }

    std::vector<std::uint8_t> extract(const std::string& resourceId) const override {
        if (!has(resourceId)) throw std::runtime_error("RomFS resource not found: " + resourceId);
        return archive_->extract(resourceId.substr(6));
    }

private:
    RomInfo info_;
    std::unique_ptr<RumbleWorldArchive> archive_;
    std::vector<RomResource> resources_;
};

class NdsResourceSource final : public IRomResourceSource {
public:
    explicit NdsResourceSource(RomInfo info) : info_(std::move(info)) {
        const auto header = readRange(info_.path, 0, std::min<std::uint64_t>(0x200, static_cast<std::uint64_t>(info_.size)));
        if (header.size() < 0x50) throw std::runtime_error("Nintendo DS header is truncated");

        addRange("system/header.bin", "Nintendo DS header", ResourceKind::System, 0, header.size());
        const std::uint32_t arm9Offset = le32(header, 0x20);
        const std::uint32_t arm9Size = le32(header, 0x2C);
        const std::uint32_t arm7Offset = le32(header, 0x30);
        const std::uint32_t arm7Size = le32(header, 0x3C);
        addRangeChecked("system/arm9.bin", "ARM9 executable", arm9Offset, arm9Size);
        addRangeChecked("system/arm7.bin", "ARM7 executable", arm7Offset, arm7Size);

        const std::uint32_t fntOffset = le32(header, 0x40);
        const std::uint32_t fntSize = le32(header, 0x44);
        const std::uint32_t fatOffset = le32(header, 0x48);
        const std::uint32_t fatSize = le32(header, 0x4C);
        if (fntOffset && fntSize >= 8 && fatOffset && fatSize >= 8 &&
            static_cast<std::uint64_t>(fntOffset) + fntSize <= info_.size &&
            static_cast<std::uint64_t>(fatOffset) + fatSize <= info_.size) {
            fnt_ = readRange(info_.path, fntOffset, fntSize);
            fat_ = readRange(info_.path, fatOffset, fatSize);
            indexNitroFs();
        }
    }

    const RomInfo& romInfo() const noexcept override { return info_; }
    std::string providerName() const override { return "Nintendo DS NitroFS"; }
    std::vector<RomResource> resources() const override { return resources_; }

    bool has(const std::string& resourceId) const override { return ranges_.find(resourceId) != ranges_.end(); }

    std::vector<std::uint8_t> extract(const std::string& resourceId) const override {
        const auto it = ranges_.find(resourceId);
        if (it == ranges_.end()) throw std::runtime_error("Nintendo DS resource not found: " + resourceId);
        return readRange(info_.path, it->second.offset, it->second.size);
    }

private:
    struct Range { std::uint64_t offset = 0; std::uint64_t size = 0; };

    RomInfo info_;
    std::vector<std::uint8_t> fnt_;
    std::vector<std::uint8_t> fat_;
    std::vector<RomResource> resources_;
    std::unordered_map<std::string, Range> ranges_;

    void addRange(const std::string& id, const std::string& name, ResourceKind kind, std::uint64_t offset, std::uint64_t size) {
        ranges_[id] = {offset, size};
        resources_.push_back({id, name, kind, size});
    }

    void addRangeChecked(const std::string& id, const std::string& name, std::uint64_t offset, std::uint64_t size) {
        if (!size || offset + size > info_.size) return;
        addRange(id, name, ResourceKind::System, offset, size);
    }

    void indexNitroFs() {
        if (fnt_.size() < 8) return;
        const std::uint16_t directoryCount = le16(fnt_, 6);
        if (!directoryCount || static_cast<std::size_t>(directoryCount) * 8 > fnt_.size()) return;

        std::unordered_set<std::uint16_t> visited;
        walkDirectory(0xF000u, std::string{}, directoryCount, visited);
        std::sort(resources_.begin(), resources_.end(), [](const RomResource& a, const RomResource& b) {
            return a.id < b.id;
        });
    }

    void walkDirectory(std::uint16_t directoryId, const std::string& parent, std::uint16_t directoryCount,
                       std::unordered_set<std::uint16_t>& visited) {
        if (directoryId < 0xF000u || !visited.insert(directoryId).second) return;
        const std::uint32_t index = static_cast<std::uint32_t>(directoryId - 0xF000u);
        if (index >= directoryCount) return;
        const std::size_t record = static_cast<std::size_t>(index) * 8;
        const std::uint32_t subtableOffset = le32(fnt_, record);
        std::uint16_t fileId = le16(fnt_, record + 4);
        if (subtableOffset >= fnt_.size()) return;

        std::size_t pos = subtableOffset;
        while (pos < fnt_.size()) {
            const std::uint8_t typeLength = fnt_[pos++];
            if (typeLength == 0) break;
            const bool directory = (typeLength & 0x80u) != 0;
            const std::size_t nameLength = typeLength & 0x7Fu;
            if (!nameLength || pos + nameLength > fnt_.size()) break;
            std::string name(reinterpret_cast<const char*>(fnt_.data() + pos), nameLength);
            pos += nameLength;

            if (directory) {
                if (pos + 2 > fnt_.size()) break;
                const std::uint16_t childId = le16(fnt_, pos);
                pos += 2;
                const std::string childPath = parent.empty() ? name : parent + "/" + name;
                walkDirectory(childId, childPath, directoryCount, visited);
                continue;
            }

            const std::size_t fatRecord = static_cast<std::size_t>(fileId) * 8;
            ++fileId;
            if (fatRecord + 8 > fat_.size()) continue;
            const std::uint32_t start = le32(fat_, fatRecord);
            const std::uint32_t end = le32(fat_, fatRecord + 4);
            if (end < start || end > info_.size) continue;
            const std::string path = parent.empty() ? name : parent + "/" + name;
            addRange("nitrofs/" + path, path, classifyPath(path), start, end - start);
        }
    }
};

} // namespace

const char* resourceKindName(ResourceKind kind) noexcept {
    switch (kind) {
        case ResourceKind::RawRom: return "raw-rom";
        case ResourceKind::System: return "system";
        case ResourceKind::File: return "file";
        case ResourceKind::Model: return "model";
        case ResourceKind::Animation: return "animation";
        case ResourceKind::Texture: return "texture";
        case ResourceKind::Audio: return "audio";
        case ResourceKind::Archive: return "archive";
        default: return "unknown";
    }
}

std::unique_ptr<IRomResourceSource> openResourceSource(const RomInfo& rom) {
    if (rom.format == RomFormat::Nintendo3DsCia || rom.format == RomFormat::Nintendo3DsImage) {
        try {
            return std::make_unique<ThreeDsRomFsResourceSource>(rom);
        } catch (...) {
            return std::make_unique<RawRomResourceSource>(rom);
        }
    }
    if (rom.format == RomFormat::NintendoDs) {
        try {
            return std::make_unique<NdsResourceSource>(rom);
        } catch (...) {
            return std::make_unique<RawRomResourceSource>(rom);
        }
    }
    return std::make_unique<RawRomResourceSource>(rom);
}

} // namespace promptemon::rom
