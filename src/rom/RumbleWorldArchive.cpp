#include "rom/RumbleWorldArchive.h"

#include "rom/Lz11.h"
#include "rom/Lzh8.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <unordered_set>

namespace promptemon::rom {
namespace {

constexpr std::uint32_t kNone = 0xFFFFFFFFu;

std::uint32_t u32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 4 > data.size()) throw std::runtime_error("Unexpected end of structure");
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

std::uint64_t u64(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 8 > data.size()) throw std::runtime_error("Unexpected end of structure");
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(data[offset + i]) << (8u * i);
    return value;
}

std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

bool magicAt(const std::vector<std::uint8_t>& data, std::size_t offset, const char* magic) {
    for (std::size_t i = 0; magic[i] != 0; ++i) {
        if (offset + i >= data.size() || data[offset + i] != static_cast<std::uint8_t>(magic[i])) return false;
    }
    return true;
}

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::vector<std::uint8_t> decompressKnown(std::vector<std::uint8_t> data) {
    for (unsigned pass = 0; pass < 4; ++pass) {
        if (Lzh8::isCompressed(data)) {
            data = Lzh8::decompress(data);
            continue;
        }
        if (Lz11::isCompressed(data)) {
            data = Lz11::decompress(data);
            continue;
        }
        break;
    }
    return data;
}

std::string utf16Ascii(const std::vector<std::uint8_t>& data, std::size_t offset, std::uint32_t byteLength) {
    if (offset + byteLength > data.size() || (byteLength & 1u) != 0) {
        throw std::runtime_error("Invalid UTF-16 name");
    }
    std::string out;
    out.reserve(byteLength / 2u);
    for (std::uint32_t i = 0; i < byteLength; i += 2) {
        const std::uint16_t ch = static_cast<std::uint16_t>(data[offset + i]) |
                                 static_cast<std::uint16_t>(data[offset + i + 1] << 8);
        out.push_back(ch <= 0x7Fu ? static_cast<char>(ch) : '?');
    }
    return out;
}

std::string utf16ZAscii(const std::vector<std::uint8_t>& data, std::size_t offset, std::size_t limit) {
    std::string out;
    while (offset + 1 < data.size() && offset + 1 < limit) {
        const std::uint16_t ch = static_cast<std::uint16_t>(data[offset]) |
                                 static_cast<std::uint16_t>(data[offset + 1] << 8);
        offset += 2;
        if (ch == 0) break;
        out.push_back(ch <= 0x7Fu ? static_cast<char>(ch) : '?');
    }
    return out;
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool isBchPath(const std::string& path) {
    const std::string p = lowerCopy(path);
    return endsWith(p, ".bch") || endsWith(p, ".bch.cx");
}

bool isAnimationLeaf(const std::string& path) {
    const std::string p = lowerCopy(path);
    const auto slash = p.find_last_of("/:");
    const std::string leaf = p.substr(slash == std::string::npos ? 0 : slash + 1);
    return leaf == "anim.bch" || leaf == "anim.bch.cx" ||
           leaf == "mii_anim.bch" || leaf == "mii_anim.bch.cx" ||
           leaf.find("animation") != std::string::npos;
}

bool couldBeDarcContainer(const std::string& path) {
    const std::string p = lowerCopy(path);
    if (endsWith(p, ".bch.cx")) return false;
    return endsWith(p, ".arc") || endsWith(p, ".arc.xc") || endsWith(p, ".arc.cx") ||
           endsWith(p, ".darc") || endsWith(p, ".darc.cx") ||
           endsWith(p, ".carc") || endsWith(p, ".carc.cx") ||
           endsWith(p, ".garc") || endsWith(p, ".garc.cx") ||
           endsWith(p, ".xc") || endsWith(p, ".cx");
}

struct DirectoryEntry {
    std::uint32_t sibling = kNone;
    std::uint32_t child = kNone;
    std::uint32_t firstFile = kNone;
    std::string name;
};

struct FileEntry {
    std::uint32_t sibling = kNone;
    std::uint64_t dataOffset = 0;
    std::uint64_t dataSize = 0;
    std::string name;
};

} // namespace

bool RumbleWorldArchive::open(const std::filesystem::path& romPath) {
    close();
    path_ = romPath;

    if (!std::filesystem::exists(path_) || !std::filesystem::is_regular_file(path_)) {
        close();
        return false;
    }

    try {
        const std::uint64_t fileSize = std::filesystem::file_size(path_);
        const auto outer = read(0, static_cast<std::size_t>(std::min<std::uint64_t>(fileSize, 0x400u)));

        const auto tryNcch = [&](std::uint64_t ncchBase, std::uint64_t& romFsBase, std::uint64_t& romFsSize) -> bool {
            if (ncchBase + 0x200 > fileSize) return false;
            const auto ncch = read(ncchBase, 0x200);
            if (!magicAt(ncch, 0x100, "NCCH")) return false;

            const std::uint64_t offset = static_cast<std::uint64_t>(u32(ncch, 0x1B0)) * 0x200u;
            const std::uint64_t size = static_cast<std::uint64_t>(u32(ncch, 0x1B4)) * 0x200u;
            if (!offset || !size || ncchBase + offset + 4 > fileSize) return false;

            const auto ivfc = read(ncchBase + offset, 4);
            if (!magicAt(ivfc, 0, "IVFC")) return false;
            romFsBase = ncchBase + offset;
            romFsSize = std::min<std::uint64_t>(size, fileSize - romFsBase);
            return true;
        };

        std::uint64_t romFsBase = 0;
        std::uint64_t romFsSize = 0;
        bool found = false;

        if (magicAt(outer, 0x100, "NCCH")) found = tryNcch(0, romFsBase, romFsSize);

        if (!found && magicAt(outer, 0x100, "NCSD")) {
            for (std::size_t i = 0; i < 8 && 0x128 + i * 8 <= outer.size(); ++i) {
                const std::uint64_t partitionBase = static_cast<std::uint64_t>(u32(outer, 0x120 + i * 8)) * 0x200u;
                if (!partitionBase) continue;
                if (tryNcch(partitionBase, romFsBase, romFsSize)) {
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            if (outer.size() < 0x20) throw std::runtime_error("Unsupported 3DS container");
            const std::uint32_t headerSize = u32(outer, 0x00);
            const std::uint32_t certificateSize = u32(outer, 0x08);
            const std::uint32_t ticketSize = u32(outer, 0x0C);
            const std::uint32_t tmdSize = u32(outer, 0x10);
            if (headerSize >= 0x20 && headerSize < fileSize) {
                std::uint64_t contentOffset = alignUp(headerSize, 0x40);
                contentOffset = alignUp(contentOffset + certificateSize, 0x40);
                contentOffset = alignUp(contentOffset + ticketSize, 0x40);
                contentOffset = alignUp(contentOffset + tmdSize, 0x40);
                found = tryNcch(contentOffset, romFsBase, romFsSize);
            }
        }

        if (!found) throw std::runtime_error("No readable RomFS found; the 3DS content may be encrypted or unsupported");

        indexRomFs(romFsBase, romFsSize);
        indexNestedArchives();
        return !files_.empty();
    } catch (...) {
        close();
        throw;
    }
}

void RumbleWorldArchive::close() {
    nestedFiles_.clear();
    files_.clear();
    path_.clear();
}

bool RumbleWorldArchive::isOpen() const noexcept {
    return !path_.empty() && !files_.empty();
}

std::vector<std::string> RumbleWorldArchive::pokemonModels() const {
    std::vector<std::string> result;
    for (const auto& [path, record] : files_) {
        (void)record;
        const std::string lowered = lowerCopy(path);
        if (lowered.rfind("pii/", 0) == 0 && isBchPath(lowered) && !isAnimationLeaf(lowered)) result.push_back(path);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> RumbleWorldArchive::modelResources() const {
    std::vector<std::string> result;
    result.reserve(files_.size() + nestedFiles_.size());
    for (const auto& [path, record] : files_) {
        (void)record;
        if (isBchPath(path) && !isAnimationLeaf(path)) result.push_back(path);
    }
    for (const auto& [path, record] : nestedFiles_) {
        (void)record;
        if (isBchPath(path) && !isAnimationLeaf(path)) result.push_back(path);
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool RumbleWorldArchive::has(const std::string& romfsPath) const {
    return files_.find(romfsPath) != files_.end() || nestedFiles_.find(romfsPath) != nestedFiles_.end();
}

std::vector<std::uint8_t> RumbleWorldArchive::extract(const std::string& romfsPath) const {
    if (const auto direct = files_.find(romfsPath); direct != files_.end()) {
        if (direct->second.size > static_cast<std::uint64_t>(static_cast<std::size_t>(-1))) {
            throw std::runtime_error("RomFS file is too large to extract");
        }
        return read(direct->second.absoluteOffset, static_cast<std::size_t>(direct->second.size));
    }

    const auto nested = nestedFiles_.find(romfsPath);
    if (nested == nestedFiles_.end()) throw std::runtime_error("RomFS resource not found: " + romfsPath);

    auto parentData = decompressKnown(extract(nested->second.parentPath));
    const std::size_t begin = nested->second.offset;
    const std::size_t size = nested->second.size;
    if (begin > parentData.size() || size > parentData.size() - begin) {
        throw std::runtime_error("Invalid nested DARC resource bounds: " + romfsPath);
    }
    return std::vector<std::uint8_t>(parentData.begin() + begin, parentData.begin() + begin + size);
}

std::vector<std::uint8_t> RumbleWorldArchive::extractBch(const std::string& romfsPath) const {
    auto data = decompressKnown(extract(romfsPath));
    if (data.size() < 4 || data[0] != 'B' || data[1] != 'C' || data[2] != 'H' || data[3] != 0) {
        throw std::runtime_error("Extracted resource is not BCH: " + romfsPath);
    }
    return data;
}

std::vector<std::uint8_t> RumbleWorldArchive::read(std::uint64_t offset, std::size_t size) const {
    std::ifstream input(path_, std::ios::binary);
    if (!input) throw std::runtime_error("Could not open ROM container: " + path_.string());
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) throw std::runtime_error("Could not seek in ROM container");

    std::vector<std::uint8_t> data(size);
    if (size != 0) {
        input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
        if (input.gcount() != static_cast<std::streamsize>(size)) throw std::runtime_error("Unexpected end of ROM container");
    }
    return data;
}

void RumbleWorldArchive::indexNestedArchives() {
    std::vector<std::string> containers;
    containers.reserve(files_.size());
    for (const auto& [path, record] : files_) {
        (void)record;
        if (couldBeDarcContainer(path)) containers.push_back(path);
    }

    for (const auto& containerPath : containers) {
        try {
            const auto direct = files_.find(containerPath);
            if (direct == files_.end()) continue;
            auto data = read(direct->second.absoluteOffset, static_cast<std::size_t>(direct->second.size));
            data = decompressKnown(std::move(data));
            if (magicAt(data, 0, "darc")) indexDarc(containerPath, data, 0);
        } catch (...) {
            // Archive-looking files may use another Nintendo container format.
            // Keep the original RomFS file exportable and continue with the rest.
        }
    }
}

void RumbleWorldArchive::indexDarc(const std::string& containerPath,
                                   const std::vector<std::uint8_t>& archiveData,
                                   unsigned depth) {
    if (depth > 8) return;
    if (archiveData.size() < 0x1C || !magicAt(archiveData, 0, "darc")) return;
    if (!(archiveData[4] == 0xFF && archiveData[5] == 0xFE)) return;

    const std::uint32_t tableOffset = u32(archiveData, 0x10);
    const std::uint32_t tableSize = u32(archiveData, 0x14);
    if (tableOffset > archiveData.size() || tableSize > archiveData.size() - tableOffset || tableSize < 12) return;

    const std::uint32_t rootLength = u32(archiveData, tableOffset + 8);
    if (rootLength == 0 || rootLength > 0x100000u) return;
    const std::uint64_t entriesBytes = static_cast<std::uint64_t>(rootLength) * 12u;
    if (entriesBytes > tableSize) return;

    const std::size_t namesOffset = static_cast<std::size_t>(tableOffset + entriesBytes);
    const std::size_t namesLimit = static_cast<std::size_t>(tableOffset + tableSize);

    struct DirectoryFrame {
        std::string path;
        std::uint32_t endIndex = 0;
    };
    std::vector<DirectoryFrame> directories;
    directories.push_back({"", rootLength});

    for (std::uint32_t i = 1; i < rootLength; ++i) {
        while (!directories.empty() && i >= directories.back().endIndex) directories.pop_back();
        const std::size_t entryOffset = static_cast<std::size_t>(tableOffset) + static_cast<std::size_t>(i) * 12u;
        if (entryOffset + 12 > archiveData.size()) break;

        const std::uint32_t flagsAndName = u32(archiveData, entryOffset);
        const bool isDirectory = (flagsAndName & 0x01000000u) != 0;
        const std::uint32_t nameOffset = flagsAndName & 0x00FFFFFFu;
        const std::size_t namePos = namesOffset + nameOffset;
        if (namePos >= namesLimit) continue;

        const std::string name = utf16ZAscii(archiveData, namePos, namesLimit);
        const std::string parent = directories.empty() ? std::string{} : directories.back().path;
        const std::string fullPath = parent.empty() ? name : parent + "/" + name;

        const std::uint32_t dataOffset = u32(archiveData, entryOffset + 4);
        const std::uint32_t length = u32(archiveData, entryOffset + 8);

        if (isDirectory) {
            if (!name.empty() && name != ".") directories.push_back({fullPath, length});
            else directories.push_back({parent, length});
            continue;
        }

        if (static_cast<std::uint64_t>(dataOffset) + length > archiveData.size()) continue;
        const std::string nestedPath = containerPath + "::" + fullPath;
        nestedFiles_[nestedPath] = NestedFileRecord{containerPath, dataOffset, length};

        if (depth < 8 && couldBeDarcContainer(fullPath)) {
            try {
                std::vector<std::uint8_t> child(archiveData.begin() + dataOffset,
                                                archiveData.begin() + dataOffset + length);
                child = decompressKnown(std::move(child));
                if (magicAt(child, 0, "darc")) indexDarc(nestedPath, child, depth + 1);
            } catch (...) {
                // Keep the child itself exportable even if its nested container
                // cannot be decoded.
            }
        }
    }
}

void RumbleWorldArchive::indexRomFs(std::uint64_t romFsBase, std::uint64_t romFsSize) {
    const std::size_t probeSize = static_cast<std::size_t>(std::min<std::uint64_t>(romFsSize, 0x20000u));
    const auto probe = read(romFsBase, probeSize);

    std::size_t level3Offset = static_cast<std::size_t>(-1);
    for (std::size_t pos = 0; pos + 0x28 <= probe.size(); pos += 4) {
        if (u32(probe, pos) != 0x28u) continue;

        const std::uint32_t dirHashOffset = u32(probe, pos + 0x04);
        const std::uint32_t dirHashSize = u32(probe, pos + 0x08);
        const std::uint32_t dirMetaOffset = u32(probe, pos + 0x0C);
        const std::uint32_t dirMetaSize = u32(probe, pos + 0x10);
        const std::uint32_t fileHashOffset = u32(probe, pos + 0x14);
        const std::uint32_t fileHashSize = u32(probe, pos + 0x18);
        const std::uint32_t fileMetaOffset = u32(probe, pos + 0x1C);
        const std::uint32_t fileMetaSize = u32(probe, pos + 0x20);
        const std::uint32_t dataOffset = u32(probe, pos + 0x24);

        const bool sequential = dirHashOffset == 0x28u &&
            dirMetaOffset == dirHashOffset + dirHashSize &&
            fileHashOffset == dirMetaOffset + dirMetaSize &&
            fileMetaOffset == fileHashOffset + fileHashSize &&
            dataOffset >= fileMetaOffset + fileMetaSize;
        if (sequential && static_cast<std::uint64_t>(pos) + dataOffset < romFsSize) {
            level3Offset = pos;
            break;
        }
    }

    if (level3Offset == static_cast<std::size_t>(-1)) throw std::runtime_error("Could not locate RomFS level-3 header");

    const std::uint32_t dirMetaOffset = u32(probe, level3Offset + 0x0C);
    const std::uint32_t dirMetaSize = u32(probe, level3Offset + 0x10);
    const std::uint32_t fileMetaOffset = u32(probe, level3Offset + 0x1C);
    const std::uint32_t fileMetaSize = u32(probe, level3Offset + 0x20);
    const std::uint32_t dataOffset = u32(probe, level3Offset + 0x24);

    const std::uint64_t level3Base = romFsBase + level3Offset;
    const auto directories = read(level3Base + dirMetaOffset, dirMetaSize);
    const auto fileMetadata = read(level3Base + fileMetaOffset, fileMetaSize);
    const std::uint64_t fileDataBase = level3Base + dataOffset;

    const auto parseDirectory = [&directories](std::uint32_t offset) -> DirectoryEntry {
        if (static_cast<std::uint64_t>(offset) + 0x18u > directories.size()) throw std::runtime_error("Invalid RomFS directory metadata offset");
        DirectoryEntry entry;
        entry.sibling = u32(directories, offset + 0x04);
        entry.child = u32(directories, offset + 0x08);
        entry.firstFile = u32(directories, offset + 0x0C);
        const std::uint32_t nameLength = u32(directories, offset + 0x14);
        entry.name = utf16Ascii(directories, offset + 0x18, nameLength);
        return entry;
    };

    const auto parseFile = [&fileMetadata](std::uint32_t offset) -> FileEntry {
        if (static_cast<std::uint64_t>(offset) + 0x20u > fileMetadata.size()) throw std::runtime_error("Invalid RomFS file metadata offset");
        FileEntry entry;
        entry.sibling = u32(fileMetadata, offset + 0x04);
        entry.dataOffset = u64(fileMetadata, offset + 0x08);
        entry.dataSize = u64(fileMetadata, offset + 0x10);
        const std::uint32_t nameLength = u32(fileMetadata, offset + 0x1C);
        entry.name = utf16Ascii(fileMetadata, offset + 0x20, nameLength);
        return entry;
    };

    std::unordered_set<std::uint32_t> visitedDirectories;
    std::function<void(std::uint32_t, const std::string&)> walk;
    walk = [&](std::uint32_t directoryOffset, const std::string& parentPath) {
        if (!visitedDirectories.insert(directoryOffset).second) return;
        const DirectoryEntry directory = parseDirectory(directoryOffset);
        const std::string currentPath = directoryOffset == 0
            ? parentPath
            : (parentPath.empty() ? directory.name : parentPath + "/" + directory.name);

        std::unordered_set<std::uint32_t> visitedFiles;
        for (std::uint32_t fileOffset = directory.firstFile;
             fileOffset != kNone && visitedFiles.insert(fileOffset).second;) {
            const FileEntry file = parseFile(fileOffset);
            const std::string fullPath = currentPath.empty() ? file.name : currentPath + "/" + file.name;
            files_[fullPath] = FileRecord{fileDataBase + file.dataOffset, file.dataSize};
            fileOffset = file.sibling;
        }

        std::unordered_set<std::uint32_t> visitedSiblings;
        for (std::uint32_t childOffset = directory.child;
             childOffset != kNone && visitedSiblings.insert(childOffset).second;) {
            const DirectoryEntry child = parseDirectory(childOffset);
            walk(childOffset, currentPath);
            childOffset = child.sibling;
        }
    };

    walk(0, "");
}

} // namespace promptemon::rom
