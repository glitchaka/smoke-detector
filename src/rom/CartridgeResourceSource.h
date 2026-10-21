#pragma once

#include "rom/IRomResourceSource.h"

#include <memory>

namespace promptemon::rom {

std::unique_ptr<IRomResourceSource> openCartridgeResourceSource(const RomInfo& rom);

} // namespace promptemon::rom
