#pragma once

#include "pokemon/PokemonAsset.h"

#include <cstddef>
#include <optional>
#include <string_view>

namespace promptemon::viewer {

class AnimationController {
public:
    void bind(const pokemon::PokemonAsset* asset) noexcept;

    const pokemon::AnimationClip* current() const noexcept;
    bool play(std::string_view clipName) noexcept;
    bool playAction(std::size_t actionIndex) noexcept;
    void onCurrentClipFinished() noexcept;
    void returnToIdle() noexcept;

private:
    const pokemon::AnimationClip* findIdle() const noexcept;

    const pokemon::PokemonAsset* asset_ = nullptr;
    std::optional<std::size_t> currentIndex_;
};

} // namespace promptemon::viewer
