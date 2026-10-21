#include "export/ResourcePackageExporter.h"

#include "rom/IRomResourceSource.h"
#include "rom/Lz11.h"
#include "rom/Lzh8.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace smoke_detector::exporter {
namespace {

constexpr char kPackageMagic[8] = {'S','D','P','K','G','0','0','1'};
constexpr char kSfxMagic[8] = {'S','D','S','F','X','0','0','1'};

std::string safeComponent(std::string value) {
    for (char& c : value) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '-' || c == '_' || c == '.')) c = '_';
    }
    while (!value.empty() && value.front() == '.') value.erase(value.begin());
    return value.empty() ? std::string("asset") : value;
}

std::filesystem::path safeRelative(const std::string& resourceId) {
    std::string expanded;
    expanded.reserve(resourceId.size() + 32);
    for (std::size_t i = 0; i < resourceId.size();) {
        if (i + 1 < resourceId.size() && resourceId[i] == ':' && resourceId[i + 1] == ':') {
            expanded += ".contents/";
            i += 2;
            continue;
        }
        expanded.push_back(resourceId[i++]);
    }

    std::filesystem::path out;
    std::string current;
    auto flush = [&]() {
        if (!current.empty() && current != "." && current != "..") out /= safeComponent(current);
        current.clear();
    };
    for (char c : expanded) {
        if (c == '/' || c == '\\') flush();
        else current.push_back(c);
    }
    flush();
    if (out.empty()) out = "asset.bin";
    return out;
}

std::filesystem::path decodedRelative(const std::string& resourceId) {
    auto path = safeRelative(resourceId);
    std::string filename = path.filename().string();
    std::string lowered = filename;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (lowered.size() >= 3 && lowered.compare(lowered.size() - 3, 3, ".cx") == 0) {
        filename.resize(filename.size() - 3);
        path.replace_filename(filename.empty() ? "decoded.bin" : filename);
    } else if (lowered.size() >= 3 && lowered.compare(lowered.size() - 3, 3, ".xc") == 0) {
        filename.resize(filename.size() - 3);
        path.replace_filename(filename.empty() ? "decoded.bin" : filename);
    } else {
        path += ".decoded";
    }
    return std::filesystem::path("decoded") / path;
}

std::optional<std::vector<std::uint8_t>> decodeKnownCompression(const std::vector<std::uint8_t>& original) {
    if (!promptemon::rom::Lzh8::isCompressed(original) && !promptemon::rom::Lz11::isCompressed(original)) return std::nullopt;

    std::vector<std::uint8_t> data = original;
    bool changed = false;
    for (unsigned pass = 0; pass < 4; ++pass) {
        if (promptemon::rom::Lzh8::isCompressed(data)) {
            data = promptemon::rom::Lzh8::decompress(data);
            changed = true;
            continue;
        }
        if (promptemon::rom::Lz11::isCompressed(data)) {
            data = promptemon::rom::Lz11::decompress(data);
            changed = true;
            continue;
        }
        break;
    }
    if (!changed) return std::nullopt;
    return data;
}

void writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Could not create export file: " + path.string());
    if (!bytes.empty()) out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void copyFile(const std::filesystem::path& source, const std::filesystem::path& destination) {
    std::filesystem::create_directories(destination.parent_path());
    std::error_code ec;
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) throw std::runtime_error("Could not copy ROM image: " + ec.message());
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

const char* manifestKind(promptemon::rom::ResourceKind kind) noexcept {
    using promptemon::rom::ResourceKind;
    switch (kind) {
        case ResourceKind::Data: return "data";
        case ResourceKind::Text: return "text";
        case ResourceKind::Graphics: return "graphics";
        case ResourceKind::Palette: return "palette";
        default: return promptemon::rom::resourceKindName(kind);
    }
}

void writeU32(std::ostream& out, std::uint32_t value) {
    const char bytes[4] = {
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8) & 0xFFu),
        static_cast<char>((value >> 16) & 0xFFu),
        static_cast<char>((value >> 24) & 0xFFu)
    };
    out.write(bytes, 4);
}

void writeU64(std::ostream& out, std::uint64_t value) {
    char bytes[8]{};
    for (unsigned i = 0; i < 8; ++i) bytes[i] = static_cast<char>((value >> (8u * i)) & 0xFFu);
    out.write(bytes, 8);
}

struct PackedFile {
    std::string path;
    std::filesystem::path source;
    std::uint64_t size = 0;
};

void copyStream(std::istream& in, std::ostream& out) {
    char buffer[1024 * 1024];
    while (in) {
        in.read(buffer, sizeof(buffer));
        const auto got = in.gcount();
        if (got > 0) out.write(buffer, got);
    }
}

void writePackage(const std::filesystem::path& packagePath, const std::vector<PackedFile>& files) {
    std::ofstream out(packagePath, std::ios::binary);
    if (!out) throw std::runtime_error("Could not create package: " + packagePath.string());
    out.write(kPackageMagic, sizeof(kPackageMagic));
    writeU32(out, 1);
    writeU32(out, static_cast<std::uint32_t>(files.size()));

    for (const auto& file : files) {
        if (file.path.size() > 0xFFFFFFFFu) throw std::runtime_error("Package path is too long");
        writeU32(out, static_cast<std::uint32_t>(file.path.size()));
        writeU64(out, file.size);
        out.write(file.path.data(), static_cast<std::streamsize>(file.path.size()));
        std::ifstream input(file.source, std::ios::binary);
        if (!input) throw std::runtime_error("Could not reopen exported resource: " + file.source.string());
        copyStream(input, out);
    }
}

void buildSelfExtractor(const std::filesystem::path& stub, const std::filesystem::path& package,
                        const std::filesystem::path& output) {
    if (stub.empty() || !std::filesystem::is_regular_file(stub)) return;
    std::ifstream stubIn(stub, std::ios::binary);
    std::ifstream packageIn(package, std::ios::binary);
    std::ofstream out(output, std::ios::binary);
    if (!stubIn || !packageIn || !out) throw std::runtime_error("Could not build self-extracting package");

    copyStream(stubIn, out);
    const std::streampos position = out.tellp();
    if (position < std::streampos(0)) throw std::runtime_error("Could not determine self-extractor payload offset");
    const auto packageOffset = static_cast<std::uint64_t>(static_cast<std::streamoff>(position));
    copyStream(packageIn, out);
    out.write(kSfxMagic, sizeof(kSfxMagic));
    writeU64(out, packageOffset);
}

} // namespace

std::vector<ResourcePackageResult> ResourcePackageExporter::exportAll(
    const promptemon::rom::RomResourceArchive& archive,
    const std::filesystem::path& exportRoot,
    const std::filesystem::path& selfExtractorStub,
    ProgressCallback progress,
    bool buildPackages) {

    std::vector<ResourcePackageResult> results;
    const auto summaries = archive.sourceSummaries();
    const auto packageRoot = exportRoot / "resources";
    std::filesystem::create_directories(packageRoot);

    std::size_t totalResources = 0;
    for (const auto& summary : summaries) totalResources += archive.resourcesForSource(summary.label).size();
    std::size_t currentResource = 0;

    for (const auto& summary : summaries) {
        ResourcePackageResult result;
        result.sourceLabel = summary.label;

        const std::string romName = safeComponent(summary.rom.path.filename().string());
        result.directory = packageRoot / romName;
        std::filesystem::remove_all(result.directory);
        std::filesystem::create_directories(result.directory / "files");

        std::vector<PackedFile> packed;
        std::vector<std::string> derivedManifest;
        std::ostringstream manifest;
        manifest << "{\n";
        manifest << "  \"schema\": \"smoke-detector-resource-package-v2\",\n";
        manifest << "  \"rom\": \"" << jsonEscape(summary.rom.path.filename().string()) << "\",\n";
        manifest << "  \"format\": \"" << jsonEscape(promptemon::rom::RomInspector::formatName(summary.rom.format)) << "\",\n";
        manifest << "  \"game\": \"" << jsonEscape(summary.rom.detectedGame) << "\",\n";
        manifest << "  \"provider\": \"" << jsonEscape(summary.provider) << "\",\n";
        manifest << "  \"resources\": [\n";

        const auto resources = archive.resourcesForSource(summary.label);
        for (std::size_t i = 0; i < resources.size(); ++i) {
            const auto& resource = resources[i];
            if (progress) progress(currentResource, totalResources, summary.label, resource.id);

            const auto relative = std::filesystem::path("files") / safeRelative(resource.id);
            const auto destination = result.directory / relative;
            std::uint64_t size = 0;
            std::optional<std::vector<std::uint8_t>> decoded;

            if (resource.kind == promptemon::rom::ResourceKind::RawRom) {
                copyFile(summary.rom.path, destination);
                size = static_cast<std::uint64_t>(std::filesystem::file_size(destination));
            } else {
                const auto virtualPath = summary.label + " :: " + resource.id;
                const auto payload = archive.extract(virtualPath);
                writeBytes(destination, payload.bytes);
                size = static_cast<std::uint64_t>(payload.bytes.size());
                try {
                    decoded = decodeKnownCompression(payload.bytes);
                } catch (...) {
                    // A bad or unsupported compressed stream must not block the
                    // original resource from being preserved in the dump.
                }
            }

            result.bytes += size;
            ++result.resources;
            if (buildPackages) packed.push_back({relative.generic_string(), destination, size});

            manifest << "    {\"id\": \"" << jsonEscape(resource.id)
                     << "\", \"name\": \"" << jsonEscape(resource.displayName)
                     << "\", \"path\": \"" << jsonEscape(relative.generic_string())
                     << "\", \"kind\": \"" << manifestKind(resource.kind)
                     << "\", \"size\": " << size << "}";
            if (i + 1 != resources.size()) manifest << ',';
            manifest << '\n';

            if (decoded) {
                const auto decodedRel = std::filesystem::path("files") / decodedRelative(resource.id);
                const auto decodedDest = result.directory / decodedRel;
                writeBytes(decodedDest, *decoded);
                const std::uint64_t decodedSize = static_cast<std::uint64_t>(decoded->size());
                result.bytes += decodedSize;
                ++result.resources;
                if (buildPackages) packed.push_back({decodedRel.generic_string(), decodedDest, decodedSize});

                std::ostringstream entry;
                entry << "    {\"source\": \"" << jsonEscape(resource.id)
                      << "\", \"path\": \"" << jsonEscape(decodedRel.generic_string())
                      << "\", \"compression\": \"LZH8/LZ11\", \"size\": " << decodedSize << "}";
                derivedManifest.push_back(entry.str());
            }

            ++currentResource;
        }
        manifest << "  ],\n";
        manifest << "  \"derived\": [\n";
        for (std::size_t i = 0; i < derivedManifest.size(); ++i) {
            manifest << derivedManifest[i];
            if (i + 1 != derivedManifest.size()) manifest << ',';
            manifest << '\n';
        }
        manifest << "  ]\n}\n";

        const auto manifestPath = result.directory / "manifest.json";
        {
            std::ofstream out(manifestPath, std::ios::binary);
            if (!out) throw std::runtime_error("Could not create resource manifest");
            const std::string text = manifest.str();
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
        }

        if (buildPackages) {
            packed.push_back({"manifest.json", manifestPath, static_cast<std::uint64_t>(std::filesystem::file_size(manifestPath))});
            result.packageFile = packageRoot / (romName + ".sdpack");
            writePackage(result.packageFile, packed);

            if (!selfExtractorStub.empty() && std::filesystem::is_regular_file(selfExtractorStub)) {
#ifdef _WIN32
                result.selfExtractingFile = packageRoot / (romName + ".resources.exe");
#else
                result.selfExtractingFile = packageRoot / (romName + ".resources");
#endif
                buildSelfExtractor(selfExtractorStub, result.packageFile, result.selfExtractingFile);
            }
        }
        results.push_back(std::move(result));
    }

    if (progress) progress(totalResources, totalResources, std::string{}, std::string{});
    return results;
}

} // namespace smoke_detector::exporter
