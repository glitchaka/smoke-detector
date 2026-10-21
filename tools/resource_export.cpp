#include "export/ResourcePackageExporter.h"
#include "rom/RomResourceArchive.h"

#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    try {
        const std::filesystem::path romDirectory = argc > 1 ? argv[1] : "roms";
        const std::filesystem::path exportDirectory = argc > 2 ? argv[2] : "exports";

        promptemon::rom::RomResourceArchive archive;
        if (!archive.open(romDirectory)) {
            std::cerr << "No readable ROMs were found in: " << romDirectory << '\n';
            return 2;
        }

        std::filesystem::path executable = argc > 0 ? std::filesystem::path(argv[0]) : std::filesystem::path{};
        std::error_code ec;
        executable = std::filesystem::absolute(executable, ec);
        std::filesystem::path stub;
        if (!ec) {
#ifdef _WIN32
            stub = executable.parent_path() / "smoke_sfx_stub.exe";
#else
            stub = executable.parent_path() / "smoke_sfx_stub";
#endif
            if (!std::filesystem::is_regular_file(stub)) stub.clear();
        }

        const auto results = smoke_detector::exporter::ResourcePackageExporter::exportAll(archive, exportDirectory, stub);
        std::uint64_t totalBytes = 0;
        std::size_t totalResources = 0;
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
                  << " ROM(s), " << totalBytes << " bytes total.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "resource_export: " << e.what() << '\n';
        return 1;
    }
}
