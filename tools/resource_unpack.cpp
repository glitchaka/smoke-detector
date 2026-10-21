#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr char kPackageMagic[8] = {'S','D','P','K','G','0','0','1'};
constexpr char kSfxMagic[8] = {'S','D','S','F','X','0','0','1'};

std::uint32_t readU32(std::istream& in) {
    unsigned char b[4]{};
    in.read(reinterpret_cast<char*>(b), 4);
    if (!in) throw std::runtime_error("Unexpected end of package");
    return static_cast<std::uint32_t>(b[0]) |
           (static_cast<std::uint32_t>(b[1]) << 8) |
           (static_cast<std::uint32_t>(b[2]) << 16) |
           (static_cast<std::uint32_t>(b[3]) << 24);
}

std::uint64_t readU64(std::istream& in) {
    unsigned char b[8]{};
    in.read(reinterpret_cast<char*>(b), 8);
    if (!in) throw std::runtime_error("Unexpected end of package");
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= static_cast<std::uint64_t>(b[i]) << (8u * i);
    return value;
}

bool equalMagic(const char* a, const char* b) {
    return std::equal(a, a + 8, b);
}

std::uint64_t findPackageOffset(std::ifstream& in) {
    char magic[8]{};
    in.seekg(0, std::ios::beg);
    in.read(magic, 8);
    if (in && equalMagic(magic, kPackageMagic)) return 0;

    in.clear();
    in.seekg(0, std::ios::end);
    const auto end = in.tellg();
    if (end < static_cast<std::streamoff>(16)) throw std::runtime_error("Not a smoke-detector resource package");
    in.seekg(end - static_cast<std::streamoff>(16), std::ios::beg);
    in.read(magic, 8);
    if (!in || !equalMagic(magic, kSfxMagic)) throw std::runtime_error("Not a smoke-detector resource package");
    return readU64(in);
}

bool safeRelative(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_path()) return false;
    for (const auto& part : path) {
        if (part == ".." || part == ".") return false;
    }
    return true;
}

void copyBytes(std::istream& in, std::ostream& out, std::uint64_t bytes) {
    std::vector<char> buffer(1024 * 1024);
    while (bytes > 0) {
        const auto chunk = static_cast<std::streamsize>(std::min<std::uint64_t>(bytes, buffer.size()));
        in.read(buffer.data(), chunk);
        if (in.gcount() != chunk) throw std::runtime_error("Unexpected end of package payload");
        out.write(buffer.data(), chunk);
        bytes -= static_cast<std::uint64_t>(chunk);
    }
}

std::size_t unpack(const std::filesystem::path& package, const std::filesystem::path& output) {
    std::ifstream in(package, std::ios::binary);
    if (!in) throw std::runtime_error("Could not open package: " + package.string());
    const std::uint64_t offset = findPackageOffset(in);
    in.clear();
    in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);

    char magic[8]{};
    in.read(magic, 8);
    if (!in || !equalMagic(magic, kPackageMagic)) throw std::runtime_error("Invalid embedded package offset");
    const std::uint32_t version = readU32(in);
    if (version != 1) throw std::runtime_error("Unsupported package version");
    const std::uint32_t count = readU32(in);

    std::filesystem::create_directories(output);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t pathLength = readU32(in);
        const std::uint64_t size = readU64(in);
        if (!pathLength || pathLength > 1024 * 1024) throw std::runtime_error("Invalid package path length");
        std::string pathText(pathLength, '\0');
        in.read(pathText.data(), static_cast<std::streamsize>(pathText.size()));
        if (!in) throw std::runtime_error("Unexpected end of package path");
        const std::filesystem::path relative = std::filesystem::path(pathText).lexically_normal();
        if (!safeRelative(relative)) throw std::runtime_error("Unsafe path in package: " + pathText);
        const auto destination = output / relative;
        std::filesystem::create_directories(destination.parent_path());
        std::ofstream out(destination, std::ios::binary);
        if (!out) throw std::runtime_error("Could not create: " + destination.string());
        copyBytes(in, out, size);
    }
    return count;
}

std::filesystem::path defaultOutput(const std::filesystem::path& package) {
    std::string stem = package.stem().string();
    const std::string suffix = ".resources";
    if (stem.size() > suffix.size() && stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0) {
        stem.resize(stem.size() - suffix.size());
    }
    if (stem.empty()) stem = "resources";
    return package.parent_path() / (stem + "_resources");
}

} // namespace

int main(int argc, char** argv) {
    try {
#ifdef SMOKE_SFX_STUB
        const std::filesystem::path package = argc > 0 ? std::filesystem::path(argv[0]) : std::filesystem::path{};
        const auto output = defaultOutput(package);
#else
        if (argc < 2) {
            std::cerr << "Usage: resource_unpack <package.sdpack|self-extracting-file> [output-directory]\n";
            return 2;
        }
        const std::filesystem::path package = argv[1];
        const auto output = argc > 2 ? std::filesystem::path(argv[2]) : defaultOutput(package);
#endif
        const auto count = unpack(package, output);
        std::cout << "Extracted " << count << " files to " << output << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "resource_unpack: " << e.what() << '\n';
        return 1;
    }
}
