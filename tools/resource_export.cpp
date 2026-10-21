#include "export/ResourcePackageExporter.h"
#include "rom/RomResourceArchive.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

std::filesystem::path executablePath(const char* argv0) {
    std::error_code ec;
    auto path = std::filesystem::absolute(argv0 ? std::filesystem::path(argv0) : std::filesystem::path{}, ec);
    if (ec) return {};
    return path;
}

std::filesystem::path findRepositoryRoot(std::filesystem::path start) {
    std::error_code ec;
    start = std::filesystem::absolute(start, ec);
    if (ec) return {};

    for (;;) {
        if (std::filesystem::is_regular_file(start / "CMakeLists.txt", ec) &&
            std::filesystem::is_directory(start / "roms", ec)) {
            return start;
        }
        ec.clear();
        const auto parent = start.parent_path();
        if (parent.empty() || parent == start) break;
        start = parent;
    }
    return {};
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto executable = executablePath(argc > 0 ? argv[0] : nullptr);
        auto repositoryRoot = findRepositoryRoot(std::filesystem::current_path());
        if (repositoryRoot.empty() && !executable.empty()) {
            repositoryRoot = findRepositoryRoot(executable.parent_path());
        }

        const std::filesystem::path romDirectory = argc > 1
            ? std::filesystem::path(argv[1])
            : (repositoryRoot.empty() ? std::filesystem::path("roms") : repositoryRoot / "roms");
        const std::filesystem::path exportDirectory = argc > 2
            ? std::filesystem::path(argv[2])
            : (repositoryRoot.empty() ? std::filesystem::path("exports") : repositoryRoot / "exports");

        std::cout << "ROMs:    " << romDirectory << '\n'
                  << "Destino: " << exportDirectory << "\n\n"
                  << "Buscando recursos..." << std::endl;

        promptemon::rom::RomResourceArchive archive;
        if (!archive.open(romDirectory)) {
            std::cerr << "No readable ROMs were found in: " << romDirectory << std::endl;
            return 2;
        }

        std::filesystem::path stub;
        if (!executable.empty()) {
#ifdef _WIN32
            stub = executable.parent_path() / "smoke_sfx_stub.exe";
#else
            stub = executable.parent_path() / "smoke_sfx_stub";
#endif
            if (!std::filesystem::is_regular_file(stub)) stub.clear();
        }

        std::cout << "Extrayendo..." << std::endl;
        const auto results = smoke_detector::exporter::ResourcePackageExporter::exportAll(
            archive,
            exportDirectory,
            stub,
            [](std::size_t current, std::size_t total,
               const std::string& sourceLabel,
               const std::string& resourceId) {
                if (total == 0) return;
                if (current >= total) {
                    std::cout << "[" << total << "/" << total << "] Finalizando paquetes..." << std::endl;
                    return;
                }
                std::cout << "[" << (current + 1) << "/" << total << "] ";
                if (!sourceLabel.empty()) std::cout << sourceLabel << " :: ";
                std::cout << resourceId << std::endl;
            },
            true);

        std::uint64_t totalBytes = 0;
        std::size_t totalResources = 0;
        std::cout << '\n';
        for (const auto& result : results) {
            totalBytes += result.bytes;
            totalResources += result.resources;
            std::cout << result.sourceLabel << '\n'
                      << "  directory: " << result.directory << '\n'
                      << "  package:   " << result.packageFile << '\n';
            if (!result.selfExtractingFile.empty()) std::cout << "  self-extracting: " << result.selfExtractingFile << '\n';
            std::cout << "  resources: " << result.resources << " | bytes: " << result.bytes << "\n\n";
        }
        std::cout << "Exported " << totalResources << " resources from " << results.size()
                  << " ROM(s), " << totalBytes << " bytes total." << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "resource_export: " << e.what() << std::endl;
        return 1;
    }
}
