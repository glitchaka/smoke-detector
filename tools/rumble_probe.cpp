#include "rom/RumbleWorldArchive.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void writeBinary(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("Could not create: " + path.string());
    output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!output) throw std::runtime_error("Could not write: " + path.string());
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 4) {
        std::cerr << "Usage: rumble_probe <Pokemon-Rumble-World.cia> [internal-name] [output-dir]\n";
        std::cerr << "Example: rumble_probe roms/RumbleWorld.cia PIKATYUU_M extracted\n";
        return 2;
    }

    const std::string internalName = argc >= 3 ? argv[2] : "PIKATYUU_M";
    const std::filesystem::path outputDir = argc >= 4 ? argv[3] : "extracted";

    try {
        promptemon::rom::RumbleWorldArchive archive;
        if (!archive.open(argv[1])) {
            std::cerr << "Could not open Rumble World CIA\n";
            return 1;
        }

        const auto models = archive.pokemonModels();
        std::cout << "Pokemon model resources found: " << models.size() << '\n';

        const std::string modelPath = "pii/" + internalName + ".bch.cx";
        if (!archive.has(modelPath)) {
            std::cerr << "Model not found: " << modelPath << "\n\nAvailable examples:\n";
            for (std::size_t i = 0; i < models.size() && i < 30; ++i) {
                std::cerr << "  " << models[i] << '\n';
            }
            return 1;
        }

        std::filesystem::create_directories(outputDir);

        const auto model = archive.extractBch(modelPath);
        const auto modelOutput = outputDir / (internalName + ".bch");
        writeBinary(modelOutput, model);
        std::cout << "Model:      " << modelPath << '\n';
        std::cout << "Model BCH:  " << modelOutput.string() << " (" << model.size() << " bytes)\n";

        if (archive.has("pii/anim.bch.cx")) {
            const auto animations = archive.extractBch("pii/anim.bch.cx");
            const auto animationOutput = outputDir / "anim.bch";
            writeBinary(animationOutput, animations);
            std::cout << "Animations: " << animationOutput.string() << " (" << animations.size() << " bytes)\n";
        }

        std::cout << "BCH extraction succeeded. Next stage: BCH -> W3D model/skeleton/animation adapter.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "rumble_probe: " << ex.what() << '\n';
        return 1;
    }
}
