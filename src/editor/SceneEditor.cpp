#include "editor/SceneEditor.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace smoke_detector::editor {
namespace {

using promptemon::bch::Animation;
using promptemon::bch::Axis;
using promptemon::bch::Bone;
using promptemon::bch::BoneTrack;
using promptemon::bch::Quat;
using promptemon::bch::Vec3;

constexpr float kPi = 3.14159265358979323846f;

float component(const Vec3& value, Axis axis) {
    switch (axis) {
        case Axis::X: return value.x;
        case Axis::Y: return value.y;
        case Axis::Z: return value.z;
    }
    return 0.0f;
}

float& component(Vec3& value, Axis axis) {
    switch (axis) {
        case Axis::X: return value.x;
        case Axis::Y: return value.y;
        case Axis::Z: return value.z;
    }
    return value.x;
}

Quat normalize(Quat q) {
    const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (length <= 1e-8f) return {0, 0, 0, 1};
    q.x /= length;
    q.y /= length;
    q.z /= length;
    q.w /= length;
    return q;
}

Quat multiply(Quat a, Quat b) {
    return normalize({
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    });
}

Quat axisAngle(Axis axis, float radians) {
    const float half = radians * 0.5f;
    const float s = std::sin(half);
    const float c = std::cos(half);
    switch (axis) {
        case Axis::X: return {s, 0, 0, c};
        case Axis::Y: return {0, s, 0, c};
        case Axis::Z: return {0, 0, s, c};
    }
    return {0, 0, 0, 1};
}

Quat euler(Vec3 r) {
    const float cx = std::cos(r.x * 0.5f), sx = std::sin(r.x * 0.5f);
    const float cy = std::cos(r.y * 0.5f), sy = std::sin(r.y * 0.5f);
    const float cz = std::cos(r.z * 0.5f), sz = std::sin(r.z * 0.5f);
    return normalize({
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz
    });
}

void rotatePoint(Vec3& point, Vec3 center, Axis axis, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float x = point.x - center.x;
    const float y = point.y - center.y;
    const float z = point.z - center.z;
    switch (axis) {
        case Axis::X:
            point.y = center.y + y * c - z * s;
            point.z = center.z + y * s + z * c;
            break;
        case Axis::Y:
            point.x = center.x + x * c + z * s;
            point.z = center.z - x * s + z * c;
            break;
        case Axis::Z:
            point.x = center.x + x * c - y * s;
            point.y = center.y + x * s + y * c;
            break;
    }
}

void rotateVector(Vec3& value, Axis axis, float radians) {
    Vec3 center{};
    rotatePoint(value, center, axis, radians);
}

std::size_t frameCount(const Animation& animation) {
    return static_cast<std::size_t>(std::max(1, static_cast<int>(std::ceil(animation.frames)))) + 1;
}

} // namespace

void SceneEditor::bind(promptemon::bch::Scene* scene) {
    scene_ = scene;
    selectedMesh_ = scene_ && !scene_->meshes().empty() ? 0 : npos;
    selectedBone_ = scene_ && !scene_->bones().empty() ? 0 : npos;
    selectedAnimation_ = scene_ && !scene_->animations().empty() ? 0 : npos;
    frame_ = 0;
    dirty_ = false;
    undo_.clear();
}

void SceneEditor::clear() {
    scene_ = nullptr;
    selectedMesh_ = selectedBone_ = selectedAnimation_ = npos;
    frame_ = 0;
    dirty_ = false;
    undo_.clear();
}

void SceneEditor::selectMesh(std::size_t index) {
    if (!scene_ || index >= scene_->meshes().size()) selectedMesh_ = npos;
    else selectedMesh_ = index;
}

void SceneEditor::selectBone(std::size_t index) {
    if (!scene_ || index >= scene_->bones().size()) selectedBone_ = npos;
    else selectedBone_ = index;
}

void SceneEditor::selectAnimation(std::size_t index) {
    if (!scene_ || index >= scene_->animations().size()) {
        selectedAnimation_ = npos;
        frame_ = 0;
        return;
    }
    selectedAnimation_ = index;
    setFrame(frame_);
}

void SceneEditor::setFrame(int frame) {
    frame_ = std::max(0, frame);
    if (const auto* animation = currentAnimation()) {
        frame_ = std::min(frame_, std::max(0, static_cast<int>(std::ceil(animation->frames))));
    }
}

std::size_t SceneEditor::createAnimation(const std::string& name, int frames, bool loop) {
    if (!scene_) throw std::runtime_error("No editable scene is loaded");
    snapshot();
    Animation animation;
    animation.name = name.empty() ? "Nueva_animacion" : name;
    animation.frames = static_cast<float>(std::max(1, frames));
    animation.loop = loop;
    scene_->animationsMutable().push_back(std::move(animation));
    selectedAnimation_ = scene_->animations().size() - 1;
    frame_ = 0;
    dirty_ = true;
    return selectedAnimation_;
}

void SceneEditor::snapshot() {
    if (!scene_) return;
    Snapshot state;
    state.meshes = scene_->meshes();
    state.materials = scene_->materials();
    state.animations = scene_->animations();
    undo_.push_back(std::move(state));
    constexpr std::size_t maxUndo = 16;
    if (undo_.size() > maxUndo) undo_.erase(undo_.begin());
}

bool SceneEditor::undo() {
    if (!scene_ || undo_.empty()) return false;
    Snapshot state = std::move(undo_.back());
    undo_.pop_back();
    scene_->meshesMutable() = std::move(state.meshes);
    scene_->materialsMutable() = std::move(state.materials);
    scene_->animationsMutable() = std::move(state.animations);
    if (selectedMesh_ >= scene_->meshes().size()) selectedMesh_ = scene_->meshes().empty() ? npos : 0;
    if (selectedAnimation_ >= scene_->animations().size()) selectedAnimation_ = scene_->animations().empty() ? npos : 0;
    setFrame(frame_);
    dirty_ = true;
    return true;
}

bool SceneEditor::transformMesh(TransformTool tool, Axis axis, float amount) {
    if (!scene_ || selectedMesh_ >= scene_->meshes().size()) return false;
    auto& mesh = scene_->meshesMutable()[selectedMesh_];
    if (mesh.vertices.empty()) return false;

    snapshot();
    Vec3 center{};
    for (const auto& vertex : mesh.vertices) {
        center.x += vertex.position.x;
        center.y += vertex.position.y;
        center.z += vertex.position.z;
    }
    const float inverse = 1.0f / static_cast<float>(mesh.vertices.size());
    center.x *= inverse;
    center.y *= inverse;
    center.z *= inverse;

    if (tool == TransformTool::Translate) {
        for (auto& vertex : mesh.vertices) component(vertex.position, axis) += amount;
    } else if (tool == TransformTool::Scale) {
        const float factor = std::max(0.01f, 1.0f + amount);
        for (auto& vertex : mesh.vertices) {
            vertex.position.x = center.x + (vertex.position.x - center.x) * factor;
            vertex.position.y = center.y + (vertex.position.y - center.y) * factor;
            vertex.position.z = center.z + (vertex.position.z - center.z) * factor;
        }
    } else {
        for (auto& vertex : mesh.vertices) {
            rotatePoint(vertex.position, center, axis, amount);
            rotateVector(vertex.normal, axis, amount);
        }
    }

    dirty_ = true;
    return true;
}

promptemon::bch::BoneTrack& SceneEditor::ensureBoneTrack(Animation& animation, const Bone& bone) {
    auto found = std::find_if(animation.tracks.begin(), animation.tracks.end(), [&](const BoneTrack& track) {
        return track.boneName == bone.name;
    });
    if (found != animation.tracks.end()) return *found;

    BoneTrack track;
    track.boneName = bone.name;
    animation.tracks.push_back(std::move(track));
    return animation.tracks.back();
}

bool SceneEditor::keyBone(TransformTool tool, Axis axis, float amount) {
    if (!scene_ || selectedBone_ >= scene_->bones().size()) return false;
    if (selectedAnimation_ == npos || selectedAnimation_ >= scene_->animations().size()) {
        createAnimation("Nueva_animacion_" + std::to_string(scene_->animations().size() + 1), 30, true);
    } else {
        snapshot();
    }

    auto& animation = scene_->animationsMutable()[selectedAnimation_];
    const auto& bone = scene_->bones()[selectedBone_];
    auto& track = ensureBoneTrack(animation, bone);
    const std::size_t count = frameCount(animation);
    const std::size_t index = std::min<std::size_t>(static_cast<std::size_t>(std::max(frame_, 0)), count - 1);

    if (tool == TransformTool::Translate) {
        if (!track.translation.present || track.translation.constant || track.translation.samples.size() != count) {
            track.translation.present = true;
            track.translation.constant = false;
            track.translation.start = 0;
            track.translation.end = animation.frames;
            track.translation.samples.assign(count, bone.bindTranslation);
        }
        component(track.translation.samples[index], axis) += amount;
    } else if (tool == TransformTool::Scale) {
        if (!track.scale.present || track.scale.constant || track.scale.samples.size() != count) {
            track.scale.present = true;
            track.scale.constant = false;
            track.scale.start = 0;
            track.scale.end = animation.frames;
            track.scale.samples.assign(count, bone.bindScale);
        }
        component(track.scale.samples[index], axis) = std::max(0.01f, component(track.scale.samples[index], axis) + amount);
    } else {
        if (!track.rotation.present || track.rotation.constant || track.rotation.samples.size() != count) {
            track.rotation.present = true;
            track.rotation.constant = false;
            track.rotation.start = 0;
            track.rotation.end = animation.frames;
            track.rotation.samples.assign(count, euler(bone.bindRotation));
        }
        track.rotation.samples[index] = multiply(axisAngle(axis, amount), track.rotation.samples[index]);
    }

    dirty_ = true;
    return true;
}

Animation* SceneEditor::currentAnimation() {
    if (!scene_ || selectedAnimation_ >= scene_->animations().size()) return nullptr;
    return &scene_->animationsMutable()[selectedAnimation_];
}

const Animation* SceneEditor::currentAnimation() const {
    if (!scene_ || selectedAnimation_ >= scene_->animations().size()) return nullptr;
    return &scene_->animations()[selectedAnimation_];
}

} // namespace smoke_detector::editor
