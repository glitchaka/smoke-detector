#include "rom/IRomModelSource.h"

#include "rom/Lzh8.h"
#include "rom/RumbleWorldArchive.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace promptemon::rom {
namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string modelKey(std::string path) {
    path = lower(path);
    const auto slash = path.find_last_of('/');
    if (slash != std::string::npos) path = path.substr(slash + 1);
    if (endsWith(path, ".bch.cx")) path.resize(path.size() - 7);
    else if (endsWith(path, ".bch")) path.resize(path.size() - 4);
    if (path.size() > 2 && path[path.size() - 2] == '_' &&
        (path.back() == 'm' || path.back() == 'f')) {
        path.resize(path.size() - 2);
    }
    return path;
}

bool isAudioResource(const std::string& path) {
    const std::string p = lower(path);
    static const char* suffixes[] = {
        ".bcwav", ".bcwav.cx", ".cwav", ".cwav.cx",
        ".bcstm", ".bcstm.cx", ".cstm", ".cstm.cx",
        ".bcsar", ".bcsar.cx", ".csar", ".csar.cx",
        ".bcseq", ".bcseq.cx", ".cseq", ".cseq.cx",
        ".bcgrp", ".bcgrp.cx", ".cgrp", ".cgrp.cx",
        ".dsp", ".wav", ".ogg"
    };
    for (const char* suffix : suffixes) if (endsWith(p, suffix)) return true;
    return false;
}

bool isSoundBank(const std::string& path) {
    const std::string p = lower(path);
    return endsWith(p, ".bcsar") || endsWith(p, ".bcsar.cx") ||
           endsWith(p, ".csar") || endsWith(p, ".csar.cx");
}

std::vector<std::string> animationCandidates(const std::string& resourceId) {
    std::vector<std::string> out;
    const auto slash = resourceId.find_last_of('/');
    const std::string directory = slash == std::string::npos ? std::string{} : resourceId.substr(0, slash);

    if (resourceId.rfind("pii/", 0) == 0) {
        out.push_back("pii/anim.bch.cx");
        out.push_back("pii/anim.bch");
    }
    if (resourceId.rfind("mii/", 0) == 0) {
        out.push_back("mii/mii_anim.bch.cx");
        out.push_back("mii/mii_anim.bch");
    }
    if (!directory.empty()) {
        out.push_back(directory + "/anim.bch.cx");
        out.push_back(directory + "/anim.bch");
        out.push_back(directory + "/animation.bch.cx");
        out.push_back(directory + "/animation.bch");
    }
    return out;
}

std::vector<std::uint8_t> maybeDecompress(std::vector<std::uint8_t> bytes) {
    if (Lzh8::isCompressed(bytes)) return Lzh8::decompress(bytes);
    return bytes;
}

class CiaBchModelSource final : public IRomModelSource {
public:
    explicit CiaBchModelSource(RomInfo info) : info_(std::move(info)) {
        archive_ = std::make_unique<RumbleWorldArchive>();
        if (!archive_->open(info_.path)) {
            throw std::runtime_error("Could not open 3DS RomFS: " + info_.path.string());
        }

        // Pokemon Rumble World stores Pokemon models under pii/.  Prefer that
        // explicit model tree whenever it exists so unrelated BCH resources
        // (UI effects, 3D helpers, texture containers, etc.) do not appear as
        // Pokemon in the model picker.  Other 3DS titles keep the generic BCH
        // scan as a fallback.
        auto paths = archive_->pokemonModels();
        if (paths.empty()) paths = archive_->modelResources();

        resources_.reserve(paths.size());
        for (const auto& path : paths) resources_.push_back({path, path});
        if (resources_.empty()) {
            throw std::runtime_error("No BCH 3D models found in: " + info_.path.filename().string());
        }
    }

    const RomInfo& romInfo() const noexcept override { return info_; }
    std::string providerName() const override { return "Nintendo 3DS BCH"; }
    std::vector<ModelResource> models() const override { return resources_; }

    bool hasBch(const std::string& resourceId) const override {
        return archive_ && archive_->has(resourceId);
    }

    std::vector<std::uint8_t> extractBch(const std::string& resourceId) const override {
        if (!archive_) throw std::runtime_error("3DS model source is closed");
        return archive_->extractBch(resourceId);
    }

    ModelPayload load(const std::string& resourceId) const override {
        if (!hasBch(resourceId)) throw std::runtime_error("Model resource not found: " + resourceId);

        ModelPayload payload;
        payload.modelBch = extractBch(resourceId);
        for (const auto& candidate : animationCandidates(resourceId)) {
            if (!archive_->has(candidate)) continue;
            try {
                payload.animationBch = extractBch(candidate);
                break;
            } catch (...) {
            }
        }
        return payload;
    }

    ModelExportPayload exportModel(const std::string& resourceId) const override {
        if (!hasBch(resourceId)) throw std::runtime_error("Model resource not found: " + resourceId);

        ModelExportPayload payload;
        payload.rom = info_;
        payload.provider = providerName();
        payload.resourceId = resourceId;
        payload.modelBch = extractBch(resourceId);

        std::unordered_set<std::string> seenAnimations;
        for (const auto& candidate : animationCandidates(resourceId)) {
            if (!archive_->has(candidate) || !seenAnimations.insert(candidate).second) continue;
            try {
                payload.animations.push_back({candidate, archive_->extractBch(candidate)});
            } catch (...) {
            }
        }

        const auto all = archive_->resources();
        const std::string key = modelKey(resourceId);
        std::vector<std::string> directAudio;
        std::vector<std::string> soundBanks;

        for (const auto& path : all) {
            if (!isAudioResource(path)) continue;
            const std::string p = lower(path);
            if (!key.empty() && p.find(key) != std::string::npos) directAudio.push_back(path);
            if (isSoundBank(path)) soundBanks.push_back(path);
        }

        // Direct matches are preferred. Sound archives are included as a fallback
        // because many 3DS titles keep Pokemon SFX/cries inside a global BCSAR.
        std::vector<std::string> audio = directAudio;
        if (audio.empty()) audio = soundBanks;

        std::unordered_set<std::string> seenAudio;
        for (const auto& path : audio) {
            if (!seenAudio.insert(path).second) continue;
            try {
                payload.audio.push_back({path, maybeDecompress(archive_->extract(path))});
            } catch (...) {
            }
        }
        return payload;
    }

private:
    RomInfo info_;
    std::unique_ptr<RumbleWorldArchive> archive_;
    std::vector<ModelResource> resources_;
};

} // namespace

std::unique_ptr<IRomModelSource> openModelSource(const RomInfo& rom) {
    if (rom.format == RomFormat::Nintendo3DsCia || rom.format == RomFormat::Nintendo3DsImage) {
        return std::make_unique<CiaBchModelSource>(rom);
    }
    return nullptr;
}

} // namespace promptemon::rom
