#pragma once

#include "rom/RomInspector.h"

#include <filesystem>
#include <string>

namespace promptemon::rom {

struct ThreeDsReadableImage {
    std::filesystem::path path;
    bool temporary = false;
    bool encrypted = false;
    std::string diagnostic;
};

// Returns the original image when its NCCH RomFS is already plaintext.
// For encrypted NCSD/NCCH images, this looks for a user-supplied aes_keys.txt
// beside the ROM or in the current directory and creates a cached decrypted CXI
// in the system temporary directory. No keys are bundled with Smoke Detector.
ThreeDsReadableImage prepareReadableThreeDsImage(const RomInfo& rom);

} // namespace promptemon::rom
