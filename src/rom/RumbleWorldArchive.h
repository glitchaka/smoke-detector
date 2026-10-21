#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace promptemon::rom {

class RumbleWorldArchive {
public:
    bool open(const std::filesystem::path& ciaPath);
    void close();
    bool isOpen() const noexcept;

    std::vector<std::string> pokemonModels() const;
    std::vector<std::string> modelResources() const;

    std::vector<std::string> resourcePaths() const {
        std::vector<std::string> result;
        result.reserve(files_.size() + nestedFiles_.size());
        for (const auto& [path, record] : files_) {
            (void)record;
            result.push_back(path);
        }
        for (const auto& [path, record] : nestedFiles_) {
            (void)record;
            result.push_back(path);
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    std::vector<std::string> resources() const { return resourcePaths(); }

    bool has(const std::string& romfsPath) const;
    std::vector<std::uint8_t> extract(const std::string& romfsPath) const;
    std::vector<std::uint8_t> extractBch(const std::string& romfsPath) const;

private:
    struct FileRecord {
        std::uint64_t absoluteOffset = 0;
        std::uint64_t size = 0;
    };

    struct NestedFileRecord {
        std::string parentPath;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
    };

    std::filesystem::path path_;
    std::unordered_map<std::string, FileRecord> files_;
    std::unordered_map<std::string, NestedFileRecord> nestedFiles_;

    std::vector<std::uint8_t> read(std::uint64_t offset, std::size_t size) const;
    void indexRomFs(std::uint64_t romFsBase, std::uint64_t romFsSize);
    void indexNestedArchives();
    void indexDarc(const std::string& containerPath,
                   const std::vector<std::uint8_t>& archiveData,
                   unsigned depth = 0);
};

} // namespace promptemon::rom
