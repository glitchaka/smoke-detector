#include "export/AssetExporter.h"

#include "bch/BchScene.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace smoke_detector::exporter {
namespace {

using promptemon::bch::Material;
using promptemon::bch::Scene;
using promptemon::bch::Vec2;

std::string safeName(std::string value) {
    for (char& c : value) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '-' || c == '_' || c == '.')) c = '_';
    }
    while (!value.empty() && value.front() == '.') value.erase(value.begin());
    return value.empty() ? std::string("asset") : value;
}

std::string leafName(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string modelStem(std::string leaf) {
    if (leaf.size() >= 7 && leaf.compare(leaf.size() - 7, 7, ".bch.cx") == 0) leaf.resize(leaf.size() - 7);
    else if (leaf.size() >= 4 && leaf.compare(leaf.size() - 4, 4, ".bch") == 0) leaf.resize(leaf.size() - 4);
    return safeName(leaf);
}

std::string exportedLeaf(std::string path) {
    std::string leaf = safeName(leafName(path));
    if (leaf.size() > 3 && leaf.compare(leaf.size() - 3, 3, ".cx") == 0) leaf.resize(leaf.size() - 3);
    return leaf;
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Could not create export file: " + path.string());
    if (!bytes.empty()) out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
        switch (c) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    return out.str();
}

Vec2 materialUv(const Material* material, Vec2 uv) {
    if (!material) return uv;
    const float cosine = std::cos(material->uvRotation);
    const float sine = std::sin(material->uvRotation);
    const float centeredU = uv.x - 0.5f;
    const float centeredV = uv.y - 0.5f;
    const float rotatedU = centeredU * cosine - centeredV * sine + 0.5f;
    const float rotatedV = centeredU * sine + centeredV * cosine + 0.5f;
    return {
        rotatedU * material->uvScaleU - material->uvTranslateU,
        rotatedV * material->uvScaleV - material->uvTranslateV
    };
}

void writeTga(const std::filesystem::path& path, const promptemon::bch::Texture& texture) {
    if (texture.width <= 0 || texture.height <= 0 ||
        texture.rgba.size() < static_cast<std::size_t>(texture.width) * texture.height * 4) return;

    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Could not create texture: " + path.string());

    std::uint8_t header[18]{};
    header[2] = 2;
    header[12] = static_cast<std::uint8_t>(texture.width & 0xff);
    header[13] = static_cast<std::uint8_t>((texture.width >> 8) & 0xff);
    header[14] = static_cast<std::uint8_t>(texture.height & 0xff);
    header[15] = static_cast<std::uint8_t>((texture.height >> 8) & 0xff);
    header[16] = 32;
    header[17] = 0x28;
    out.write(reinterpret_cast<const char*>(header), sizeof(header));

    for (std::size_t i = 0; i + 3 < texture.rgba.size(); i += 4) {
        const std::uint8_t bgra[4] = {
            texture.rgba[i + 2], texture.rgba[i + 1], texture.rgba[i], texture.rgba[i + 3]
        };
        out.write(reinterpret_cast<const char*>(bgra), 4);
    }
}

void writeObjAndMtl(const std::filesystem::path& modelDir, const Scene& scene,
                    const std::unordered_map<std::string, std::string>& textureFiles) {
    std::filesystem::create_directories(modelDir);
    std::ofstream obj(modelDir / "model.obj");
    std::ofstream mtl(modelDir / "model.mtl");
    if (!obj || !mtl) throw std::runtime_error("Could not create OBJ model");

    obj << "mtllib model.mtl\n";
    const auto pose = scene.samplePose(nullptr, 0.0f);
    std::size_t base = 1;

    for (std::size_t mi = 0; mi < scene.meshes().size(); ++mi) {
        const auto& mesh = scene.meshes()[mi];
        const Material* material = mesh.materialId < scene.materials().size() ? &scene.materials()[mesh.materialId] : nullptr;
        const std::string materialName = safeName(material && !material->name.empty()
            ? material->name : "material_" + std::to_string(mi));

        obj << "o " << safeName(mesh.name) << "\n";
        obj << "usemtl " << materialName << "\n";

        for (const auto& vertex : mesh.vertices) {
            const auto p = scene.skin(vertex, pose);
            obj << "v " << p.x << ' ' << p.y << ' ' << p.z << "\n";
        }
        for (const auto& vertex : mesh.vertices) {
            const Vec2 uv = materialUv(material, vertex.uv);
            obj << "vt " << uv.x << ' ' << (1.0f - uv.y) << "\n";
        }
        for (const auto& vertex : mesh.vertices) {
            obj << "vn " << vertex.normal.x << ' ' << vertex.normal.y << ' ' << vertex.normal.z << "\n";
        }
        for (std::size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
            obj << "f";
            for (int k = 0; k < 3; ++k) {
                const std::size_t index = base + i + static_cast<std::size_t>(k);
                obj << ' ' << index << '/' << index << '/' << index;
            }
            obj << "\n";
        }

        mtl << "newmtl " << materialName << "\nKd 1 1 1\nKa 0 0 0\nKs 0 0 0\n";
        if (material && !material->textureName.empty()) {
            const auto it = textureFiles.find(material->textureName);
            if (it != textureFiles.end()) mtl << "map_Kd ../textures/" << it->second << "\n";
        }
        mtl << "\n";
        base += mesh.vertices.size();
    }
}

} // namespace

ExportResult AssetExporter::exportModel(const promptemon::rom::RomModelArchive& archive,
                                        const std::string& virtualModelPath,
                                        const std::filesystem::path& exportRoot) {
    const auto payload = archive.exportModel(virtualModelPath);
    const std::string romName = safeName(payload.rom.path.stem().string());
    const std::string assetName = modelStem(leafName(payload.resourceId));
    const std::filesystem::path directory = exportRoot / romName / assetName;
    const std::filesystem::path modelDirectory = directory / "model";

    std::filesystem::create_directories(modelDirectory);
    std::filesystem::create_directories(directory / "animations");
    std::filesystem::create_directories(directory / "audio");
    std::filesystem::create_directories(directory / "textures");

    // Keep the original BCH for lossless/reversible use, but make the usable
    // interchange model the obvious file in the same directory.
    writeBytes(modelDirectory / "source.bch", payload.modelBch);

    for (std::size_t i = 0; i < payload.animations.size(); ++i) {
        std::string filename = exportedLeaf(payload.animations[i].romfsPath);
        if (filename.empty()) filename = "animation_" + std::to_string(i) + ".bch";
        writeBytes(directory / "animations" / filename, payload.animations[i].bytes);
    }

    for (std::size_t i = 0; i < payload.audio.size(); ++i) {
        std::string filename = exportedLeaf(payload.audio[i].romfsPath);
        if (filename.empty()) filename = "audio_" + std::to_string(i) + ".bin";
        writeBytes(directory / "audio" / filename, payload.audio[i].bytes);
    }

    std::vector<std::uint8_t> firstAnimation;
    if (!payload.animations.empty()) firstAnimation = payload.animations.front().bytes;

    Scene scene;
    try {
        scene = Scene::parse(payload.modelBch, firstAnimation);
    } catch (...) {
        scene = Scene::parse(payload.modelBch);
    }

    std::unordered_map<std::string, std::string> textureFiles;
    for (std::size_t i = 0; i < scene.textures().size(); ++i) {
        const auto& texture = scene.textures()[i];
        std::string filename = safeName(texture.name);
        if (filename.empty()) filename = "texture_" + std::to_string(i);
        filename += ".tga";
        writeTga(directory / "textures" / filename, texture);
        textureFiles[texture.name] = filename;
    }

    writeObjAndMtl(modelDirectory, scene, textureFiles);

    std::ofstream manifest(directory / "manifest.json");
    if (!manifest) throw std::runtime_error("Could not create export manifest");
    manifest << "{\n";
    manifest << "  \"rom\": \"" << jsonEscape(payload.rom.path.filename().string()) << "\",\n";
    manifest << "  \"provider\": \"" << jsonEscape(payload.provider) << "\",\n";
    manifest << "  \"model_resource\": \"" << jsonEscape(payload.resourceId) << "\",\n";
    manifest << "  \"model_name\": \"" << jsonEscape(scene.modelName()) << "\",\n";
    manifest << "  \"interchange_model\": \"model/model.obj\",\n";
    manifest << "  \"source_model\": \"model/source.bch\",\n";
    manifest << "  \"meshes\": " << scene.meshes().size() << ",\n";
    manifest << "  \"bones\": " << scene.bones().size() << ",\n";
    manifest << "  \"textures\": " << scene.textures().size() << ",\n";
    manifest << "  \"animation_clips\": [";
    for (std::size_t i = 0; i < scene.animations().size(); ++i) {
        if (i) manifest << ',';
        manifest << "\n    \"" << jsonEscape(scene.animations()[i].name) << "\"";
    }
    if (!scene.animations().empty()) manifest << '\n';
    manifest << "  ],\n";
    manifest << "  \"animation_containers\": [";
    for (std::size_t i = 0; i < payload.animations.size(); ++i) {
        if (i) manifest << ',';
        manifest << "\n    \"" << jsonEscape(payload.animations[i].romfsPath) << "\"";
    }
    if (!payload.animations.empty()) manifest << '\n';
    manifest << "  ],\n";
    manifest << "  \"audio_resources\": [";
    for (std::size_t i = 0; i < payload.audio.size(); ++i) {
        if (i) manifest << ',';
        manifest << "\n    \"" << jsonEscape(payload.audio[i].romfsPath) << "\"";
    }
    if (!payload.audio.empty()) manifest << '\n';
    manifest << "  ]\n}\n";

    ExportResult result;
    result.directory = directory;
    result.interchangeModel = modelDirectory / "model.obj";
    result.animationContainers = payload.animations.size();
    result.animationClips = scene.animations().size();
    result.textures = scene.textures().size();
    result.audioResources = payload.audio.size();
    return result;
}

} // namespace smoke_detector::exporter
