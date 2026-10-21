#pragma once

#include "bch/BchScene.h"

#include <cstddef>
#include <string>
#include <vector>

namespace smoke_detector::editor {

enum class TransformTool {
    Translate,
    Rotate,
    Scale,
};

enum class Axis {
    X,
    Y,
    Z,
};

class SceneEditor {
public:
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    void bind(promptemon::bch::Scene* scene);
    void clear();

    bool hasScene() const noexcept { return scene_ != nullptr; }
    bool dirty() const noexcept { return dirty_; }
    void markClean() noexcept { dirty_ = false; }

    std::size_t selectedMesh() const noexcept { return selectedMesh_; }
    std::size_t selectedBone() const noexcept { return selectedBone_; }
    std::size_t selectedAnimation() const noexcept { return selectedAnimation_; }
    int frame() const noexcept { return frame_; }

    void selectMesh(std::size_t index);
    void selectBone(std::size_t index);
    void selectAnimation(std::size_t index);
    void setFrame(int frame);

    std::size_t createAnimation(const std::string& name, int frames = 30, bool loop = true);
    bool transformMesh(TransformTool tool, Axis axis, float amount);
    bool keyBone(TransformTool tool, Axis axis, float amount);
    bool undo();

    promptemon::bch::Animation* currentAnimation();
    const promptemon::bch::Animation* currentAnimation() const;

private:
    struct Snapshot {
        std::vector<promptemon::bch::Mesh> meshes;
        std::vector<promptemon::bch::Material> materials;
        std::vector<promptemon::bch::Animation> animations;
    };

    promptemon::bch::Scene* scene_ = nullptr;
    std::size_t selectedMesh_ = npos;
    std::size_t selectedBone_ = npos;
    std::size_t selectedAnimation_ = npos;
    int frame_ = 0;
    bool dirty_ = false;
    std::vector<Snapshot> undo_;

    void snapshot();
    promptemon::bch::BoneTrack& ensureBoneTrack(promptemon::bch::Animation& animation,
                                                 const promptemon::bch::Bone& bone);
};

} // namespace smoke_detector::editor
