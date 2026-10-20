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

    // Enumerates BCH model candidates from any readable CIA RomFS. Both raw
    // .bch and LZH8-compressed .bch.cx resources are accepted. Animation-only
    // resources are excluded from the model picker and can still be loaded by
    // a model provider as companions.
    std::vector<std::string> modelResources() const {
        std::vector<std::string> result;
        result.reserve(files_.size());
        for (const auto& [path, record] : files_) {
            (void)record;
            std::string lowered = path;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            const bool isBch = lowered.size() >= 4 && lowered.compare(lowered.size() - 4, 4, ".bch") == 0;
            const bool isCompressedBch = lowered.size() >= 7 && lowered.compare(lowered.size() - 7, 7, ".bch.cx") == 0;
            if (!isBch && !isCompressedBch) continue;

            const auto slash = lowered.find_last_of('/');
            const std::string leaf = lowered.substr(slash == std::string::npos ? 0 : slash + 1);
            if (leaf == "anim.bch" || leaf == "anim.bch.cx" ||
                leaf == "mii_anim.bch" || leaf == "mii_anim.bch.cx" ||
                leaf.find("animation") != std::string::npos) {
                continue;
            }
            result.push_back(path);
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    bool has(const std::string& romfsPath) const;
    std::vector<std::uint8_t> extract(const std::string& romfsPath) const;
    std::vector<std::uint8_t> extractBch(const std::string& romfsPath) const;

private:
    struct FileRecord {
        std::uint64_t absoluteOffset = 0;
        std::uint64_t size = 0;
    };

    std::filesystem::path path_;
    std::unordered_map<std::string, FileRecord> files_;

    std::vector<std::uint8_t> read(std::uint64_t offset, std::size_t size) const;
    void indexRomFs(std::uint64_t romFsBase, std::uint64_t romFsSize);
};

} // namespace promptemon::rom
