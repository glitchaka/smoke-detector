#pragma once

#include <cstdint>
#include <vector>

namespace promptemon::rom {

class Lzh8 {
public:
    static bool isCompressed(const std::vector<std::uint8_t>& data) noexcept;
    static std::vector<std::uint8_t> decompress(const std::vector<std::uint8_t>& data);
};

} // namespace promptemon::rom
