#include "rom/RomInspector.h"

#include <iomanip>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: rom_probe <path-to-rom>\n";
        return 2;
    }

    try {
        const auto info = promptemon::rom::RomInspector::inspect(argv[1]);

        std::cout << "ROM:       " << info.path.string() << '\n';
        std::cout << "Format:    " << promptemon::rom::RomInspector::formatName(info.format) << '\n';
        std::cout << "Size:      " << info.size << " bytes\n";
        std::cout << "Title:     " << (info.title.empty() ? "(none)" : info.title) << '\n';
        std::cout << "Game code: " << (info.gameCode.empty() ? "(none)" : info.gameCode) << '\n';
        std::cout << "Maker:     " << (info.makerCode.empty() ? "(none)" : info.makerCode) << '\n';
        std::cout << "Detected:  " << (info.detectedGame.empty() ? "unknown / not in table" : info.detectedGame) << '\n';

        return info.format == promptemon::rom::RomFormat::Unknown ? 1 : 0;
    } catch (const std::exception& ex) {
        std::cerr << "rom_probe: " << ex.what() << '\n';
        return 1;
    }
}
