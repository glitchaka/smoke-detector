#pragma once

#include "pokemon/PokemonAsset.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace promptemon::rom {

class IRomPokemonProvider {
public:
    virtual ~IRomPokemonProvider() = default;

    virtual bool open(const std::filesystem::path& romPath) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const noexcept = 0;

    virtual std::vector<std::uint32_t> species() const = 0;
    virtual pokemon::PokemonAsset loadPokemon(std::uint32_t speciesId) = 0;
};

using RomPokemonProviderPtr = std::unique_ptr<IRomPokemonProvider>;

} // namespace promptemon::rom
