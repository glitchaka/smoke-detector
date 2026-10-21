#include "rom/RomInspector.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace promptemon::rom {
namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::string upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return text;
}

std::string trimField(const unsigned char* data, std::size_t length) {
    std::string out;
    out.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        const unsigned char c = data[i];
        if (c == 0x00 || c == 0xFF) break;
        if (c >= 0x20 && c <= 0x7E) out.push_back(static_cast<char>(c));
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::uint32_t le32(const std::vector<unsigned char>& data, std::size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

bool hasMagic(const std::vector<unsigned char>& data, std::size_t offset, const char* magic) {
    for (std::size_t i = 0; magic[i] != 0; ++i) {
        if (offset + i >= data.size() || data[offset + i] != static_cast<unsigned char>(magic[i])) return false;
    }
    return true;
}

std::string identifyPokemonGame(const std::string& code, const std::string& title) {
    // Nintendo game codes use the final character for region/language. Match the
    // stable three-character product prefix so EUR/ESP/JPN dumps are identified too.
    static const std::unordered_map<std::string, std::string> knownPrefixes = {
        {"BPR", "Pokemon FireRed"}, {"BPG", "Pokemon LeafGreen"},
        {"BPE", "Pokemon Emerald"}, {"AXV", "Pokemon Ruby"}, {"AXP", "Pokemon Sapphire"},
        {"B24", "Pokemon Mystery Dungeon: Red Rescue Team"},
        {"BPP", "Pokemon Pinball: Ruby & Sapphire"},
        {"APA", "Pokemon Pearl"}, {"ADA", "Pokemon Diamond"}, {"CPU", "Pokemon Platinum"},
        {"IPK", "Pokemon HeartGold"}, {"IPG", "Pokemon SoulSilver"},
        {"IRB", "Pokemon Black"}, {"IRA", "Pokemon White"},
        {"IRE", "Pokemon Black 2"}, {"IRD", "Pokemon White 2"},
    };
    if (code.size() >= 3) {
        const auto it = knownPrefixes.find(upper(code.substr(0, 3)));
        if (it != knownPrefixes.end()) return it->second;
    }

    // GB/GBC do not carry the four-character product code used by GBA/NDS.
    // Recognize the common Pokemon titles from the cartridge title field instead.
    const std::string t = upper(title);
    if (t == "POKEMON RED") return "Pokemon Red";
    if (t == "POKEMON BLUE") return "Pokemon Blue";
    if (t == "POKEMON YELLOW" || t == "POKEMON YEL") return "Pokemon Yellow";
    if (t == "POKEMON GOLD") return "Pokemon Gold";
    if (t == "POKEMON SILVER") return "Pokemon Silver";
    if (t == "PM_CRYSTAL" || t == "POKEMON CRYSTAL") return "Pokemon Crystal";
    return {};
}

bool looksLikeGba(const std::vector<unsigned char>& header) {
    return header.size() >= 0xC0 && header[0xB2] == 0x96;
}

bool looksLikeNds(const std::vector<unsigned char>& header, std::uintmax_t fileSize) {
    if (header.size() < 0x200 || fileSize < 0x4000) return false;
    const std::uint32_t arm9Offset = le32(header, 0x20);
    const std::uint32_t arm7Offset = le32(header, 0x30);
    return arm9Offset >= 0x200 && arm7Offset >= 0x200 && arm9Offset < fileSize && arm7Offset < fileSize;
}

bool looksLikeGb(const std::vector<unsigned char>& header) {
    if (header.size() < 0x150) return false;
    static const unsigned char logoStart[] = {0xCE, 0xED, 0x66, 0x66};
    return std::equal(std::begin(logoStart), std::end(logoStart), header.begin() + 0x104);
}

std::string gbTitle(const std::vector<unsigned char>& header) {
    if (header.size() < 0x150) return {};
    const unsigned char cgbFlag = header[0x143];
    if (cgbFlag != 0x80 && cgbFlag != 0xC0) return trimField(header.data() + 0x134, 16);

    // On later CGB cartridges 0x13F-0x142 is the manufacturer code, leaving
    // 11 bytes for the title. Using the old 15-byte title span incorrectly
    // produced strings such as MARIO DELUXAHYE on real retail dumps.
    return trimField(header.data() + 0x134, 11);
}

} // namespace

bool RomInspector::looksLikeRomFile(const std::filesystem::path& path) noexcept {
    const std::string extension = lower(path.extension().string());
    return extension == ".gb" || extension == ".gbc" || extension == ".gba" ||
           extension == ".nds" || extension == ".cia" || extension == ".3ds" ||
           extension == ".cci" || extension == ".cxi";
}

RomInfo RomInspector::inspect(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
        throw std::runtime_error("ROM file does not exist: " + path.string());
    }

    RomInfo info;
    info.path = path;
    info.size = std::filesystem::file_size(path);

    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Could not open ROM: " + path.string());

    const std::size_t bytesToRead = static_cast<std::size_t>(std::min<std::uintmax_t>(info.size, 0x400));
    std::vector<unsigned char> header(bytesToRead);
    file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));

    const std::string extension = lower(path.extension().string());

    if (looksLikeGba(header)) {
        info.format = RomFormat::Gba;
        info.title = trimField(header.data() + 0xA0, 12);
        info.gameCode = trimField(header.data() + 0xAC, 4);
        info.makerCode = trimField(header.data() + 0xB0, 2);
    } else if (looksLikeNds(header, info.size)) {
        info.format = RomFormat::NintendoDs;
        info.title = trimField(header.data(), 12);
        info.gameCode = trimField(header.data() + 0x0C, 4);
        info.makerCode = trimField(header.data() + 0x10, 2);
    } else if ((extension == ".3ds" || extension == ".cci") && hasMagic(header, 0x100, "NCSD")) {
        info.format = RomFormat::Nintendo3DsImage;
        info.title = path.stem().string();
    } else if (extension == ".cxi" && hasMagic(header, 0x100, "NCCH")) {
        info.format = RomFormat::Nintendo3DsImage;
        info.title = path.stem().string();
    } else if (extension == ".cia" && header.size() >= 0x20 && le32(header, 0) >= 0x20) {
        info.format = RomFormat::Nintendo3DsCia;
        info.title = path.stem().string();
    } else if (looksLikeGb(header)) {
        const unsigned char cgbFlag = header[0x143];
        info.format = (cgbFlag == 0x80 || cgbFlag == 0xC0) ? RomFormat::GameBoyColor : RomFormat::GameBoy;
        info.title = gbTitle(header);
    }

    info.detectedGame = identifyPokemonGame(info.gameCode, info.title);
    if (info.detectedGame.empty() && info.title.empty()) info.title = path.stem().string();
    return info;
}

const char* RomInspector::formatName(RomFormat format) noexcept {
    switch (format) {
        case RomFormat::GameBoy: return "Game Boy";
        case RomFormat::GameBoyColor: return "Game Boy Color";
        case RomFormat::Gba: return "Game Boy Advance";
        case RomFormat::NintendoDs: return "Nintendo DS";
        case RomFormat::Nintendo3DsCia: return "Nintendo 3DS CIA";
        case RomFormat::Nintendo3DsImage: return "Nintendo 3DS image";
        default: return "Unknown";
    }
}

} // namespace promptemon::rom
