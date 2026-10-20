#include "viewer/AnimationController.h"

namespace promptemon::viewer {

void AnimationController::bind(const pokemon::PokemonAsset* asset) noexcept {
    asset_ = asset;
    currentIndex_.reset();
    returnToIdle();
}

const pokemon::AnimationClip* AnimationController::current() const noexcept {
    if (!asset_ || !currentIndex_ || *currentIndex_ >= asset_->animations.size()) {
        return nullptr;
    }
    return &asset_->animations[*currentIndex_];
}

bool AnimationController::play(std::string_view clipName) noexcept {
    if (!asset_) {
        return false;
    }

    for (std::size_t i = 0; i < asset_->animations.size(); ++i) {
        if (asset_->animations[i].name == clipName) {
            currentIndex_ = i;
            return true;
        }
    }
    return false;
}

bool AnimationController::playAction(std::size_t actionIndex) noexcept {
    if (!asset_) {
        return false;
    }

    std::size_t seen = 0;
    for (std::size_t i = 0; i < asset_->animations.size(); ++i) {
        if (asset_->animations[i].kind != pokemon::AnimationKind::Action) {
            continue;
        }
        if (seen++ == actionIndex) {
            currentIndex_ = i;
            return true;
        }
    }
    return false;
}

void AnimationController::onCurrentClipFinished() noexcept {
    const auto* clip = current();
    if (!clip || !clip->loop) {
        returnToIdle();
    }
}

void AnimationController::returnToIdle() noexcept {
    if (!asset_) {
        currentIndex_.reset();
        return;
    }

    const auto* idle = findIdle();
    if (!idle) {
        currentIndex_.reset();
        return;
    }

    currentIndex_ = static_cast<std::size_t>(idle - asset_->animations.data());
}

const pokemon::AnimationClip* AnimationController::findIdle() const noexcept {
    if (!asset_) {
        return nullptr;
    }

    for (const auto& clip : asset_->animations) {
        if (clip.kind == pokemon::AnimationKind::Idle) {
            return &clip;
        }
    }
    return nullptr;
}

} // namespace promptemon::viewer
