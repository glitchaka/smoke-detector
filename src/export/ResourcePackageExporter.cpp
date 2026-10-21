#include "export/ResourcePackageExporter.h"

#include "rom/IRomResourceSource.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
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
    std::filesystem::path out;
    std::string current;
    auto flush = [&]() {
        if (!current.empty() && current != "." && current != "..") out /= safeComponent(current);
        current.clear();
    };
    for (char c : resourceId) {
        if (c == '/' || c == '\\') flush();
        else current.push_back(c);
    }
    flush();
    if (out.empty()) out = "asset.bin";
    return out;
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
    const std::filesystem::path& selfExtractorStub) {

    std::vector<ResourcePackageResult> results;
    const auto summaries = archive.sourceSummaries();
    const auto packageRoot = exportRoot / "resources";
    std::filesystem::create_directories(packageRoot);

    for (const auto& summary : summaries) {
        ResourcePackageResult result;
        result.sourceLabel = summary.label;

        const std::string romName = safeComponent(summary.rom.path.filename().string());
        result.directory = packageRoot / romName;
        std::filesystem::remove_all(result.directory);
        std::filesystem::create_directories(result.directory / "files");

        std::vector<PackedFile> packed;
        std::ostringstream manifest;
        manifest << "{\n";
        manifest << "  \"schema\": \"smoke-detector-resource-package-v1\",\n";
        manifest << "  \"rom\": \"" << jsonEscape(summary.rom.path.filename().string()) << "\",\n";
        manifest << "  \"format\": \"" << jsonEscape(promptemon::rom::RomInspector::formatName(summary.rom.format)) << "\",\n";
        manifest << "  \"game\": \"" << jsonEscape(summary.rom.detectedGame) << "\",\n";
        manifest << "  \"provider\": \"" << jsonEscape(summary.provider) << "\",\n";
        manifest << "  \"resources\": [\n";

        const auto resources = archive.resourcesForSource(summary.label);
        for (std::size_t i = 0; i < resources.size(); ++i) {
            const auto& resource = resources[i];
            const auto virtualPath = summary.label + " :: " + resource.id;
            const auto payload = archive.extract(virtualPath);
            const auto relative = std::filesystem::path("files") / safeRelative(resource.id);
            const auto destination = result.directory / relative;
            writeBytes(destination, payload.bytes);

            const std::uint64_t size = static_cast<std::uint64_t>(payload.bytes.size());
            result.bytes += size;
            ++result.resources;
            packed.push_back({relative.generic_string(), destination, size});

            manifest << "    {\"id\": \"" << jsonEscape(resource.id)
                     << "\", \"name\": \"" << jsonEscape(resource.displayName)
                     << "\", \"path\": \"" << jsonEscape(relative.generic_string())
                     << "\", \"kind\": \"" << manifestKind(resource.kind)
                     << "\", \"size\": " << size << "}";
            if (i + 1 != resources.size()) manifest << ',';
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
        results.push_back(std::move(result));
    }
    return results;
}

} // namespace smoke_detector::exporter
