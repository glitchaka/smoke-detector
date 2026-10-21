#include "rom/RomLibrary.h"

#include "rom/IRomModelSource.h"

#include <algorithm>
#include <exception>
#include <system_error>

namespace promptemon::rom {
namespace {

RomLibraryEntry inspectEntry(const std::filesystem::path& path) {
    RomLibraryEntry entry;
    try {
        entry.info = RomInspector::inspect(path);
    } catch (const std::exception& e) {
        entry.info.path = path;
        entry.modelError = e.what();
        return entry;
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
                case RomFormat::Unknown:
                    entry.modelError = "Formato no identificado: disponible como ROM cruda para inspeccion y dump";
                    break;
                default:
                    entry.modelError = "Formato sin proveedor de modelos";
                    break;
            }
        }
    } catch (const std::exception& e) {
        entry.modelError = e.what();
    }
    return entry;
}

} // namespace

RomLibrary RomLibrary::scan(const std::filesystem::path& input) {
    RomLibrary library;
    std::error_code ec;

    // A file passed explicitly is inspected regardless of its extension. This
    // lets Smoke Detector accept non-Pokemon ROMs and unknown cartridge images;
    // known formats get their specific provider and unknown ones remain raw.
    if (std::filesystem::is_regular_file(input, ec) && !ec) {
        library.entries_.push_back(inspectEntry(input));
    } else {
        ec.clear();
        if (!std::filesystem::is_directory(input, ec) || ec) return library;

        for (std::filesystem::recursive_directory_iterator it(
                 input,
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
            library.entries_.push_back(inspectEntry(it->path()));
        }
    }

    std::sort(library.entries_.begin(), library.entries_.end(), [](const auto& a, const auto& b) {
        if (a.hasModelProvider != b.hasModelProvider) return a.hasModelProvider > b.hasModelProvider;
        return a.info.path.filename().string() < b.info.path.filename().string();
    });
    return library;
}

} // namespace promptemon::rom
