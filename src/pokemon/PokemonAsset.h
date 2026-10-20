#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace promptemon::pokemon {

enum class AnimationKind {
    Idle,
    Action,
    Hit,
    Faint,
    Unknown
};

struct AnimationClip {
    std::string name;
    AnimationKind kind = AnimationKind::Unknown;
    float durationSeconds = 0.0f;
    bool loop = false;
};

struct PokemonAsset {
    std::uint32_t speciesId = 0;
    std::string internalName;
    std::string displayName;

    // Provider-specific identifiers. The renderer must not assume these are files.
    std::string modelResource;
    std::vector<std::string> materialResources;
    std::vector<std::string> textureResources;
    std::vector<AnimationClip> animations;
};

} // namespace promptemon::pokemon
