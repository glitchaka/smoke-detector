#pragma once

#include "bch/BchScene.h"
#include "rom/RomModelArchive.h"

#include <cstdint>
#include <string>
#include <vector>

namespace promptemon::bch {

struct MaterialUvMetadata {
    std::uint8_t flags = 0;
    std::uint8_t transformType = 0;
    std::uint8_t mappingType = 0;
    std::int8_t referenceCamera = -1;
};

std::vector<MaterialUvMetadata> readMaterialUvMetadata(const std::vector<std::uint8_t>& modelBch);

// Applies only metadata which Scene historically discarded. Scale/rotation/
// translation continue to come from the normal BCH parser.
void applyMaterialUvMetadata(Scene& scene,
                             const rom::RomModelArchive& archive,
                             const std::string& virtualModelPath);

} // namespace promptemon::bch
