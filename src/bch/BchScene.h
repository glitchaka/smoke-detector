#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace promptemon::bch {

struct Vec2 { float x = 0, y = 0; };
struct Vec3 { float x = 0, y = 0, z = 0; };
struct Vec4 { float x = 0, y = 0, z = 0, w = 1; };
struct Quat { float x = 0, y = 0, z = 0, w = 1; };
struct Mat4 { float m[16]{}; };

struct Bone {
    std::string name;
    int parent = -1;
    Vec3 bindScale{1, 1, 1};
    Vec3 bindRotation{};
    Vec3 bindTranslation{};
    Mat4 bindGlobal{};
    Mat4 inverseBind{};
};

struct Vertex {
    Vec3 position{};
    Vec3 normal{0, 0, 1};
    Vec2 uv{};
    std::array<std::uint16_t, 4> bones{};
    std::array<float, 4> weights{};
    std::uint8_t influenceCount = 0;
};

struct Mesh {
    std::string name;
    std::uint16_t materialId = 0;
    std::vector<Vertex> vertices; // Expanded triangle list.
};

struct Texture {
    std::string name;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba; // width * height * 4, RGBA8.
};

struct Material {
    std::string name;
    std::string textureName;

    // H3DTextureCoord[0]. The first four bytes are flags, DCC transform type,
    // mapping type and reference camera. Earlier Smoke Detector builds skipped
    // these bytes and therefore applied Maya-like UV math to every material.
    std::uint8_t uvFlags = 0;
    std::uint8_t uvTransformType = 0; // 0 Maya, 1 SoftImage, 2 3ds Max.
    std::uint8_t uvMappingType = 0;
    std::int8_t uvReferenceCamera = -1;
    float uvScaleU = 1.0f;
    float uvScaleV = 1.0f;
    float uvRotation = 0.0f;
    float uvTranslateU = 0.0f;
    float uvTranslateV = 0.0f;
};

struct Vec3Track {
    bool present = false;
    bool constant = false;
    float start = 0;
    float end = 0;
    Vec3 value{1, 1, 1};
    std::vector<Vec3> samples;
};

struct QuatTrack {
    bool present = false;
    bool constant = false;
    float start = 0;
    float end = 0;
    Quat value{};
    std::vector<Quat> samples;
};

struct BoneTrack {
    std::string boneName;
    Vec3Track scale;
    QuatTrack rotation;
    Vec3Track translation;
};

struct Animation {
    std::string name;
    bool loop = false;
    float frames = 0;
    std::vector<BoneTrack> tracks;
};

class Scene {
public:
    static Scene parse(const std::vector<std::uint8_t>& modelBch,
                       const std::vector<std::uint8_t>& animationBch = {});

    const std::string& modelName() const { return modelName_; }
    const std::vector<Bone>& bones() const { return bones_; }
    std::vector<Bone>& bonesMutable() { return bones_; }
    const std::vector<Mesh>& meshes() const { return meshes_; }
    std::vector<Mesh>& meshesMutable() { return meshes_; }
    const std::vector<Texture>& textures() const { return textures_; }
    std::vector<Texture>& texturesMutable() { return textures_; }
    const std::vector<Material>& materials() const { return materials_; }
    std::vector<Material>& materialsMutable() { return materials_; }
    const std::vector<Animation>& animations() const { return animations_; }
    std::vector<Animation>& animationsMutable() { return animations_; }
    const Animation* findAnimation(const std::string& name) const;
    const Texture* findTexture(const std::string& name) const;
    const Texture* textureForMaterial(std::uint16_t materialId) const;

    std::vector<Mat4> samplePose(const Animation* animation, float frame) const;
    Vec3 skin(const Vertex& vertex, const std::vector<Mat4>& skinMatrices) const;

private:
    std::string modelName_;
    std::vector<Bone> bones_;
    std::vector<Mesh> meshes_;
    std::vector<Texture> textures_;
    std::vector<Material> materials_;
    std::vector<Animation> animations_;
    std::unordered_map<std::string, std::size_t> boneIndex_;
};

} // namespace promptemon::bch
