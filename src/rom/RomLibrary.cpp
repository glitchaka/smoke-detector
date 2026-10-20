#include "rom/RomLibrary.h"

#include "rom/IRomModelSource.h"

#include <algorithm>
#include <exception>
#include <system_error>

namespace promptemon::rom {

RomLibrary RomLibrary::scan(const std::filesystem::path& directory) {
    RomLibrary library;
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) return library;

    for (std::filesystem::recursive_directory_iterator it(
             directory,
             std::filesystem::directory_options::skip_permission_denied,
             ec), end;
         it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (!it->is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }
        if (!RomInspector::looksLikeRomFile(it->path())) continue;

        RomLibraryEntry entry;
        try {
            entry.info = RomInspector::inspect(it->path());
        } catch (const std::exception& e) {
            entry.info.path = it->path();
            entry.modelError = e.what();
            library.entries_.push_back(std::move(entry));
            continue;
        }

        try {
            auto source = openModelSource(entry.info);
            if (source) {
                entry.hasModelProvider = true;
                entry.modelProvider = source->providerName();
            } else {
                switch (entry.info.format) {
                    case RomFormat::GameBoy:
                    case RomFormat::GameBoyColor:
                    case RomFormat::Gba:
                    case RomFormat::NintendoDs:
                        entry.modelError = "ROM de datos: no contiene modelos 3D nativos compatibles";
                        break;
                    default:
                        entry.modelError = "Formato sin proveedor de modelos";
                        break;
                }
            }
        } catch (const std::exception& e) {
            entry.modelError = e.what();
        }
        library.entries_.push_back(std::move(entry));
    }

    std::sort(library.entries_.begin(), library.entries_.end(), [](const auto& a, const auto& b) {
        if (a.hasModelProvider != b.hasModelProvider) return a.hasModelProvider > b.hasModelProvider;
        return a.info.path.filename().string() < b.info.path.filename().string();
    });
    return library;
}

} // namespace promptemon::rom
