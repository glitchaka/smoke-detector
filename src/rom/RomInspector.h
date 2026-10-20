#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace promptemon::rom {

enum class RomFormat {
    Unknown,
    GameBoy,
    GameBoyColor,
    Gba,
    NintendoDs,
    Nintendo3DsCia,
    Nintendo3DsImage,
};

struct RomInfo {
    RomFormat format = RomFormat::Unknown;
    std::filesystem::path path;
    std::uintmax_t size = 0;
    std::string title;
    std::string gameCode;
    std::string makerCode;
    std::string detectedGame;

    bool isRecognized() const noexcept { return format != RomFormat::Unknown; }
};

class RomInspector {
public:
    static RomInfo inspect(const std::filesystem::path& path);
    static bool looksLikeRomFile(const std::filesystem::path& path) noexcept;
    static const char* formatName(RomFormat format) noexcept;
};

} // namespace promptemon::rom
