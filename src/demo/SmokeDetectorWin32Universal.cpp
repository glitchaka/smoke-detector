#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Reuse the BCH renderer, exporter and ROM plumbing, but not its old toolbar/dialog UI.
#define WinMain SmokeDetectorModernLegacyWinMain
#include "SmokeDetectorWin32Modern.cpp"
#undef WinMain

namespace {

using promptemon::rom::ResourceKind;

constexpr int WB_SEARCH_ID = 6001;
constexpr int WB_SIDEBAR = 224;
constexpr int WB_TOPBAR = 70;
constexpr int WB_STATUS = 34;
constexpr int WB_ROW_H = 58;
constexpr int WB_MIN_W = 1080;
constexpr int WB_MIN_H = 680;

constexpr COLORREF WB_BG = RGB(12, 15, 20);
constexpr COLORREF WB_SIDEBAR_BG = RGB(16, 20, 27);
constexpr COLORREF WB_PANEL = RGB(22, 27, 35);
constexpr COLORREF WB_PANEL_ALT = RGB(27, 33, 42);
constexpr COLORREF WB_PANEL_HOVER = RGB(31, 38, 49);
constexpr COLORREF WB_BORDER = RGB(48, 56, 68);
constexpr COLORREF WB_TEXT = RGB(238, 242, 247);
constexpr COLORREF WB_MUTED = RGB(145, 155, 169);
constexpr COLORREF WB_ACCENT = RGB(74, 139, 255);
constexpr COLORREF WB_ACCENT_SOFT = RGB(32, 67, 119);
constexpr COLORREF WB_DANGER = RGB(222, 91, 91);

struct BrowserItem {
    std::string sourceLabel;
    std::string virtualPath;
    std::string id;
    std::string displayName;
    std::filesystem::path romPath;
    ResourceKind kind = ResourceKind::Unknown;
    std::uint64_t size = 0;
    int category = 6;
};

struct WorkbenchLayout {
    RECT client{};
    RECT search{};
    RECT openButton{};
    RECT exportButton{};
    RECT dumpButton{};
    RECT browserCard{};
    RECT list{};
    RECT previewCard{};
    RECT inspectorCard{};
    RECT status{};
};

HWND gSearchBox = nullptr;
HBRUSH gEditBrush = nullptr;
HFONT gWorkbenchTitleFont = nullptr;
HFONT gWorkbenchHeadingFont = nullptr;
HFONT gWorkbenchSmallFont = nullptr;

std::vector<BrowserItem> gBrowserItems;
std::vector<std::size_t> gFilteredItems;
std::size_t gSelectedItem = static_cast<std::size_t>(-1);
std::size_t gResourceScroll = 0;
std::size_t gAnimationScroll = 0;
std::size_t gMaterialScroll = 0;
std::size_t gTextureScroll = 0;
std::size_t gSelectedMaterial = 0;
int gCategory = 0;       // 0 all, 1 3D, 2 animation, 3 texture, 4 audio, 5 archive, 6 other
int gInspectorTab = 0;   // 0 info, 1 animations, 2 textures
bool gPreview3D = false;

std::string wbLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool endsWithI(const std::string& value, const std::string& suffix) {
    const std::string a = wbLower(value);
    const std::string b = wbLower(suffix);
    return a.size() >= b.size() && a.compare(a.size() - b.size(), b.size(), b) == 0;
}

std::string sourceFilename(const std::string& label) {
    const auto marker = label.find("] ");
    return marker == std::string::npos ? label : label.substr(marker + 2);
}

int classifyBrowserItem(const std::string& id, ResourceKind kind) {
    const std::string p = wbLower(id);
    const bool animationName = p.find("anim") != std::string::npos ||
                               p.find("motion") != std::string::npos ||
                               p.find("skeletal") != std::string::npos;
    if (animationName && (p.find(".bch") != std::string::npos || p.find(".bcres") != std::string::npos)) return 2;
    if (kind == ResourceKind::Model || endsWithI(p, ".bch") || endsWithI(p, ".bch.cx") ||
        endsWithI(p, ".bcmdl") || endsWithI(p, ".bcres")) return 1;
    if (kind == ResourceKind::Texture || kind == ResourceKind::Graphics || kind == ResourceKind::Palette ||
        endsWithI(p, ".bclim") || endsWithI(p, ".bclim.cx") || endsWithI(p, ".ctpk") ||
        endsWithI(p, ".ctpk.cx") || endsWithI(p, ".png") || endsWithI(p, ".tga") || endsWithI(p, ".bmp")) return 3;
    if (kind == ResourceKind::Audio || endsWithI(p, ".bcwav") || endsWithI(p, ".bcwav.cx") ||
        endsWithI(p, ".bcstm") || endsWithI(p, ".bcstm.cx") || endsWithI(p, ".bcsar") ||
        endsWithI(p, ".bcsar.cx") || endsWithI(p, ".bcseq") || endsWithI(p, ".bcseq.cx") ||
        endsWithI(p, ".cwav") || endsWithI(p, ".cstm") || endsWithI(p, ".wav") || endsWithI(p, ".ogg")) return 4;
    if (kind == ResourceKind::Archive || endsWithI(p, ".arc") || endsWithI(p, ".garc") ||
        endsWithI(p, ".narc") || endsWithI(p, ".darc") || endsWithI(p, ".carc") || endsWithI(p, ".szs")) return 5;
    return 6;
}

const char* categoryName(int category) {
    switch (category) {
        case 1: return "Modelos / escenarios";
        case 2: return "Animaciones";
        case 3: return "Texturas";
        case 4: return "Audio / SFX";
        case 5: return "Contenedores";
        case 6: return "Otros";
        default: return "Todos los recursos";
    }
}

const char* categoryBadge(int category) {
    switch (category) {
        case 1: return "3D";
        case 2: return "AN";
        case 3: return "TX";
        case 4: return "SFX";
        case 5: return "ARC";
        default: return "DAT";
    }
}

std::string humanBytes(std::uint64_t bytes) {
    std::ostringstream out;
    if (bytes >= 1024ull * 1024ull * 1024ull) out << (bytes / (1024ull * 1024ull * 1024ull)) << " GB";
    else if (bytes >= 1024ull * 1024ull) out << (bytes / (1024ull * 1024ull)) << " MB";
    else if (bytes >= 1024ull) out << (bytes / 1024ull) << " KB";
    else out << bytes << " B";
    return out.str();
}

bool pointIn(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

int rectWidth(const RECT& r) { return static_cast<int>(r.right - r.left); }
int rectHeight(const RECT& r) { return static_cast<int>(r.bottom - r.top); }

WorkbenchLayout workbenchLayout(HWND hwnd) {
    WorkbenchLayout l{};
    GetClientRect(hwnd, &l.client);
    const int w = rectWidth(l.client);
    const int h = rectHeight(l.client);
    const int browserWidth = std::clamp(w / 3, 330, 430);
    const int contentLeft = WB_SIDEBAR;
    const int right = w - 16;
    const int contentBottom = h - WB_STATUS - 12;
    const int inspectorHeight = std::clamp(h / 4, 185, 245);
    const int previewLeft = contentLeft + browserWidth + 12;

    l.search = {contentLeft + 18, 17, std::min(contentLeft + 470, right - 365), 53};
    l.openButton = {right - 350, 16, right - 248, 54};
    l.exportButton = {right - 238, 16, right - 136, 54};
    l.dumpButton = {right - 126, 16, right, 54};

    l.browserCard = {contentLeft + 12, WB_TOPBAR + 12, contentLeft + browserWidth, contentBottom};
    l.list = {l.browserCard.left + 10, l.browserCard.top + 62, l.browserCard.right - 10, l.browserCard.bottom - 10};
    l.inspectorCard = {previewLeft, contentBottom - inspectorHeight, right, contentBottom};
    l.previewCard = {previewLeft, WB_TOPBAR + 12, right, l.inspectorCard.top - 12};
    l.status = {WB_SIDEBAR, h - WB_STATUS, w, h};
    return l;
}

void ensureWorkbenchResources() {
    if (!gEditBrush) gEditBrush = CreateSolidBrush(WB_PANEL_ALT);
    if (!gWorkbenchTitleFont) gWorkbenchTitleFont = createUiFont(20, FW_SEMIBOLD);
    if (!gWorkbenchHeadingFont) gWorkbenchHeadingFont = createUiFont(15, FW_SEMIBOLD);
    if (!gWorkbenchSmallFont) gWorkbenchSmallFont = createUiFont(12, FW_NORMAL);
}

void releaseWorkbenchResources() {
    if (gEditBrush) DeleteObject(gEditBrush);
    if (gWorkbenchTitleFont) DeleteObject(gWorkbenchTitleFont);
    if (gWorkbenchHeadingFont) DeleteObject(gWorkbenchHeadingFont);
    if (gWorkbenchSmallFont) DeleteObject(gWorkbenchSmallFont);
    gEditBrush = nullptr;
    gWorkbenchTitleFont = gWorkbenchHeadingFont = gWorkbenchSmallFont = nullptr;
}

void fillRectColor(HDC dc, const RECT& r, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}

void roundBox(HDC dc, const RECT& r, COLORREF fill, COLORREF border, int radius = 12) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void drawTextLine(HDC dc, const std::string& text, RECT r, HFONT font, COLORREF color,
                  UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX) {
    SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextA(dc, text.c_str(), -1, &r, flags);
}

void drawButton(HDC dc, const RECT& r, const char* text, bool primary = false, bool enabled = true) {
    const COLORREF fill = !enabled ? RGB(30, 34, 40) : (primary ? WB_ACCENT : WB_PANEL_ALT);
    const COLORREF border = primary ? WB_ACCENT : WB_BORDER;
    roundBox(dc, r, fill, border, 10);
    drawTextLine(dc, text, r, gFontBody, enabled ? WB_TEXT : RGB(92, 99, 109), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void rebuildBrowserItems() {
    gBrowserItems.clear();
    if (!gResourceArchive) return;
    for (const auto& summary : gResourceArchive->sourceSummaries()) {
        for (const auto& resource : gResourceArchive->resourcesForSource(summary.label)) {
            BrowserItem item;
            item.sourceLabel = summary.label;
            item.virtualPath = summary.label + " :: " + resource.id;
            item.id = resource.id;
            item.displayName = resource.displayName;
            item.romPath = summary.rom.path;
            item.kind = resource.kind;
            item.size = resource.size;
            item.category = classifyBrowserItem(item.id, item.kind);
            gBrowserItems.push_back(std::move(item));
        }
    }
}

std::string searchQuery() {
    if (!gSearchBox) return {};
    char text[1024]{};
    GetWindowTextA(gSearchBox, text, static_cast<int>(sizeof(text)));
    return wbLower(text);
}

void rebuildFilter() {
    const std::string query = searchQuery();
    gFilteredItems.clear();
    for (std::size_t i = 0; i < gBrowserItems.size(); ++i) {
        const auto& item = gBrowserItems[i];
        if (gCategory != 0 && item.category != gCategory) continue;
        if (!query.empty()) {
            const std::string searchable = wbLower(item.id + " " + item.displayName + " " + item.sourceLabel);
            if (searchable.find(query) == std::string::npos) continue;
        }
        gFilteredItems.push_back(i);
    }
    if (gSelectedItem != static_cast<std::size_t>(-1) &&
        std::find(gFilteredItems.begin(), gFilteredItems.end(), gSelectedItem) == gFilteredItems.end()) {
        gSelectedItem = static_cast<std::size_t>(-1);
    }
    if (gSelectedItem == static_cast<std::size_t>(-1) && !gFilteredItems.empty()) gSelectedItem = gFilteredItems.front();
    gResourceScroll = std::min(gResourceScroll, gFilteredItems.empty() ? std::size_t{0} : gFilteredItems.size() - 1);
    if (gWindow) InvalidateRect(gWindow, nullptr, FALSE);
}

std::string modelPathFor(const BrowserItem& item) {
    std::string resource = item.id;
    if (resource.rfind("romfs/", 0) == 0) resource.erase(0, 6);
    if (resource.rfind("nitrofs/", 0) == 0) resource.erase(0, 8);
    const std::string filename = sourceFilename(item.sourceLabel);
    for (const auto& model : gModels) {
        const auto split = model.find(" :: ");
        if (split == std::string::npos) continue;
        if (sourceFilename(model.substr(0, split)) == filename && model.substr(split + 4) == resource) return model;
    }
    return {};
}

void clearLoadedModel() {
    if (gGL) destroyGlTextures();
    gScene.reset();
    gSelectedModel.clear();
    gIdle = nullptr;
    gCurrent = nullptr;
    gFrame = 0.0f;
    gYaw = gPitch = 0.0f;
    gZoom = 1.0f;
    gSelectedMaterial = 0;
    gAnimationScroll = gMaterialScroll = gTextureScroll = 0;
    gPreview3D = false;
}

void selectItemForLoadedModel() {
    if (gSelectedModel.empty()) return;
    const auto split = gSelectedModel.find(" :: ");
    if (split == std::string::npos) return;
    const std::string filename = sourceFilename(gSelectedModel.substr(0, split));
    const std::string resource = gSelectedModel.substr(split + 4);
    for (std::size_t i = 0; i < gBrowserItems.size(); ++i) {
        std::string id = gBrowserItems[i].id;
        if (id.rfind("romfs/", 0) == 0) id.erase(0, 6);
        if (sourceFilename(gBrowserItems[i].sourceLabel) == filename && id == resource) {
            gSelectedItem = i;
            return;
        }
    }
}

void activateBrowserItem(std::size_t index) {
    if (index >= gBrowserItems.size()) return;
    gSelectedItem = index;
    const auto& item = gBrowserItems[index];
    if (item.category == 1) {
        const std::string path = modelPathFor(item);
        if (!path.empty()) {
            try {
                loadModel(path);
                gPreview3D = true;
                gInspectorTab = 0;
                gSelectedMaterial = 0;
                gAnimationScroll = gMaterialScroll = gTextureScroll = 0;
                if (gRenderWindow) ShowWindow(gRenderWindow, SW_SHOW);
            } catch (const std::exception& e) {
                gPreview3D = false;
                if (gRenderWindow) ShowWindow(gRenderWindow, SW_HIDE);
                setStatus(std::string("BCH indexado, pero sin malla visible: ") + e.what());
            }
        } else {
            gPreview3D = false;
            if (gRenderWindow) ShowWindow(gRenderWindow, SW_HIDE);
            setStatus("Recurso 3D indexado; el visor actual no tiene decoder para este contenedor.");
        }
    } else {
        gPreview3D = false;
        if (gRenderWindow) ShowWindow(gRenderWindow, SW_HIDE);
        setStatus(item.id);
    }
    if (gWindow) InvalidateRect(gWindow, nullptr, FALSE);
}

std::filesystem::path chooseRomFile(HWND owner) {
    wchar_t buffer[32768]{};
    static const wchar_t filter[] =
        L"ROMs (*.gb;*.gbc;*.gba;*.nds;*.cia;*.3ds;*.cci;*.cxi)\0"
        L"*.gb;*.gbc;*.gba;*.nds;*.cia;*.3ds;*.cci;*.cxi\0"
        L"Todos los archivos (*.*)\0*.*\0\0";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = static_cast<DWORD>(std::size(buffer));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = L"Abrir ROM";
    if (!GetOpenFileNameW(&dialog)) return {};
    return std::filesystem::path(buffer);
}

std::filesystem::path commandLineRom() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return {};
    std::filesystem::path result;
    if (argc > 1) {
        std::error_code ec;
        std::filesystem::path candidate(argv[1]);
        if (std::filesystem::is_regular_file(candidate, ec) && !ec) result = std::move(candidate);
    }
    LocalFree(argv);
    return result;
}

bool loadRomInput(const std::filesystem::path& input, bool reportErrors = true) {
    try {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(input, ec) && !std::filesystem::is_directory(input, ec)) {
            throw std::runtime_error("La ruta seleccionada no existe o no es accesible.");
        }

        auto resources = std::make_unique<RomResourceArchive>();
        if (!resources->open(input)) throw std::runtime_error("No se pudo indexar la ROM seleccionada.");
        auto models = std::make_unique<RomModelArchive>();
        models->open(input);
        auto modelPaths = models->modelResources();

        clearLoadedModel();
        gResourceArchive = std::move(resources);
        gArchive = std::move(models);
        gModels = std::move(modelPaths);
        gRomDirectory = input;
        gExportRoot = (std::filesystem::is_directory(input, ec) ? input.parent_path() : input.parent_path()) / "smoke-detector-exports";
        rebuildBrowserItems();
        gCategory = 0;
        gResourceScroll = 0;
        rebuildFilter();

        for (const auto& model : gModels) {
            try {
                loadModel(model);
                gPreview3D = true;
                selectItemForLoadedModel();
                break;
            } catch (...) {
            }
        }

        if (!gPreview3D && !gBrowserItems.empty()) {
            gSelectedItem = 0;
            setStatus("ROM cargada: " + input.filename().string() + " | " + std::to_string(gBrowserItems.size()) + " recursos");
        }

        if (gWindow) {
            SetWindowTextA(gWindow, ("Smoke Detector - " + input.filename().string()).c_str());
            if (gSearchBox) SetWindowTextA(gSearchBox, "");
            ShowWindow(gRenderWindow, gPreview3D ? SW_SHOW : SW_HIDE);
            InvalidateRect(gWindow, nullptr, FALSE);
        }
        return true;
    } catch (const std::exception& e) {
        if (reportErrors && gWindow) MessageBoxA(gWindow, e.what(), "No se pudo abrir la ROM", MB_OK | MB_ICONERROR);
        return false;
    }
}

void initializeUniversalArchives() {
    const auto startup = commandLineRom();
    auto defaults = findRomDirectory(std::filesystem::current_path());
    if (defaults.empty()) defaults = findRomDirectory(executableDirectory());
    gExportRoot = defaults.empty() ? executableDirectory() / "exports" : defaults.parent_path() / "exports";

    if (!startup.empty() && loadRomInput(startup, false)) return;
    if (!defaults.empty() && loadRomInput(defaults, false)) return;

    gArchive = std::make_unique<RomModelArchive>();
    gResourceArchive = std::make_unique<RomResourceArchive>();
    gModels.clear();
    gBrowserItems.clear();
    gFilteredItems.clear();
    setStatus("Abre o arrastra una ROM para comenzar.");
}

std::string safeComponent(std::string value) {
    if (value.empty()) return "resource";
    for (char& c : value) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    }
    return value;
}

std::string resourceLeaf(const std::string& id) {
    std::string value = id;
    const auto nested = value.rfind("::");
    const auto slash = value.find_last_of("/\\");
    std::size_t pos = slash;
    if (nested != std::string::npos && (pos == std::string::npos || nested > pos)) pos = nested + 1;
    if (pos != std::string::npos && pos + 1 < value.size()) value = value.substr(pos + 1);
    return safeComponent(value.empty() ? "resource.bin" : value);
}

void exportSelectedContext() {
    if (gSelectedItem >= gBrowserItems.size()) return;
    const auto& item = gBrowserItems[gSelectedItem];
    const std::string modelPath = item.category == 1 ? modelPathFor(item) : std::string{};
    if (!modelPath.empty() && gArchive) {
        setStatus("Exportando modelo...");
        const auto result = AssetExporter::exportModel(*gArchive, modelPath, gExportRoot);
        setStatus("Modelo exportado: " + result.interchangeModel.string());
        ShellExecuteA(gWindow, "open", result.interchangeModel.parent_path().string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }

    if (!gResourceArchive) return;
    const auto directory = gExportRoot / "selected" / safeComponent(sourceFilename(item.sourceLabel));
    std::filesystem::create_directories(directory);
    const auto destination = directory / resourceLeaf(item.id);
    std::error_code ec;
    if (item.kind == ResourceKind::RawRom) {
        std::filesystem::copy_file(item.romPath, destination, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) throw std::runtime_error("No se pudo exportar la ROM: " + ec.message());
    } else {
        const auto payload = gResourceArchive->extract(item.virtualPath);
        std::ofstream out(destination, std::ios::binary);
        if (!out) throw std::runtime_error("No se pudo crear el archivo exportado.");
        if (!payload.bytes.empty()) out.write(reinterpret_cast<const char*>(payload.bytes.data()), static_cast<std::streamsize>(payload.bytes.size()));
        if (!out) throw std::runtime_error("No se pudo escribir el recurso exportado.");
    }
    setStatus("Exportado: " + destination.string());
    ShellExecuteA(gWindow, "open", directory.string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void exportAllWorkbench() {
    if (!gResourceArchive || gDumping) return;
    gDumping = true;
    setProgress(0, 1, {});
    setStatus("Preparando dump completo...");
    pumpUi();
    try {
        const auto results = ResourcePackageExporter::exportAll(
            *gResourceArchive, gExportRoot, {},
            [](std::size_t current, std::size_t total, const std::string&, const std::string& resource) {
                setProgress(total ? std::min(current + 1, total) : 0, total, resource);
                setStatus(resource.empty() ? "Finalizando dump..." : "Dump: " + resource);
                pumpUi();
            }, false);
        std::size_t total = 0;
        for (const auto& result : results) total += result.resources;
        gDumping = false;
        setProgress(0, 0, {});
        setStatus("Dump terminado: " + std::to_string(total) + " recursos");
        ShellExecuteA(gWindow, "open", (gExportRoot / "resources").string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } catch (...) {
        gDumping = false;
        setProgress(0, 0, {});
        throw;
    }
}

void layoutWorkbench(HWND hwnd) {
    if (!gSearchBox || !gRenderWindow) return;
    const auto l = workbenchLayout(hwnd);
    MoveWindow(gSearchBox, l.search.left + 12, l.search.top + 2,
               std::max(20, rectWidth(l.search) - 24), rectHeight(l.search) - 4, TRUE);
    const RECT render{l.previewCard.left + 10, l.previewCard.top + 43,
                      l.previewCard.right - 10, l.previewCard.bottom - 10};
    MoveWindow(gRenderWindow, render.left, render.top,
               std::max(1, rectWidth(render)), std::max(1, rectHeight(render)), TRUE);
    ShowWindow(gRenderWindow, gPreview3D ? SW_SHOW : SW_HIDE);
}

void paintSidebar(HDC dc, const WorkbenchLayout& l) {
    RECT side{0, 0, WB_SIDEBAR, l.client.bottom};
    fillRectColor(dc, side, WB_SIDEBAR_BG);

    RECT title{18, 16, WB_SIDEBAR - 16, 44};
    drawTextLine(dc, "Smoke Detector", title, gWorkbenchTitleFont, WB_TEXT);
    RECT sub{18, 42, WB_SIDEBAR - 16, 62};
    drawTextLine(dc, "ROM resource explorer", sub, gWorkbenchSmallFont, WB_MUTED);

    RECT section{18, 83, WB_SIDEBAR - 18, 104};
    drawTextLine(dc, "BIBLIOTECA", section, gWorkbenchSmallFont, WB_MUTED);

    static const char* names[] = {"Todos", "Modelos / escenarios", "Animaciones", "Texturas", "Audio / SFX", "Contenedores", "Otros"};
    static const char* badges[] = {"ALL", "3D", "AN", "TX", "SFX", "ARC", "DAT"};
    for (int i = 0; i < 7; ++i) {
        RECT row{12, 108 + i * 46, WB_SIDEBAR - 12, 148 + i * 46};
        if (gCategory == i) roundBox(dc, row, WB_ACCENT_SOFT, WB_ACCENT_SOFT, 10);
        RECT badge{row.left + 10, row.top + 8, row.left + 46, row.bottom - 8};
        roundBox(dc, badge, gCategory == i ? WB_ACCENT : WB_PANEL_ALT, gCategory == i ? WB_ACCENT : WB_BORDER, 8);
        drawTextLine(dc, badges[i], badge, gWorkbenchSmallFont, WB_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        RECT text{row.left + 55, row.top, row.right - 8, row.bottom};
        drawTextLine(dc, names[i], text, gFontBody, gCategory == i ? WB_TEXT : RGB(196, 203, 213));
    }

    if (gResourceArchive) {
        const auto summaries = gResourceArchive->sourceSummaries();
        RECT card{12, l.client.bottom - WB_STATUS - 108, WB_SIDEBAR - 12, l.client.bottom - WB_STATUS - 12};
        roundBox(dc, card, WB_PANEL, WB_BORDER, 12);
        RECT a{card.left + 12, card.top + 10, card.right - 12, card.top + 31};
        drawTextLine(dc, summaries.empty() ? "Sin ROM" : sourceFilename(summaries.front().label), a, gWorkbenchHeadingFont, WB_TEXT);
        std::string detail = summaries.empty() ? "Abre una ROM para comenzar" :
            (summaries.size() == 1 ? std::string(RomInspector::formatName(summaries.front().rom.format)) : std::to_string(summaries.size()) + " ROMs cargadas");
        RECT b{card.left + 12, card.top + 34, card.right - 12, card.top + 55};
        drawTextLine(dc, detail, b, gWorkbenchSmallFont, WB_MUTED);
        RECT c{card.left + 12, card.top + 60, card.right - 12, card.bottom - 8};
        drawTextLine(dc, std::to_string(gBrowserItems.size()) + " recursos indexados", c, gWorkbenchSmallFont, WB_MUTED);
    }
}

void paintTopbar(HDC dc, const WorkbenchLayout& l) {
    RECT top{WB_SIDEBAR, 0, l.client.right, WB_TOPBAR};
    fillRectColor(dc, top, WB_BG);
    roundBox(dc, l.search, WB_PANEL_ALT, WB_BORDER, 10);
    RECT magnifier{l.search.left + 10, l.search.top, l.search.left + 28, l.search.bottom};
    drawTextLine(dc, "?", magnifier, gWorkbenchHeadingFont, WB_MUTED, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    drawButton(dc, l.openButton, "Abrir ROM", false, !gDumping);
    drawButton(dc, l.exportButton, "Exportar", false, !gDumping && gSelectedItem < gBrowserItems.size());
    drawButton(dc, l.dumpButton, "Dump completo", true, !gDumping && !gBrowserItems.empty());
}

void paintBrowser(HDC dc, const WorkbenchLayout& l) {
    roundBox(dc, l.browserCard, WB_PANEL, WB_BORDER, 12);
    RECT title{l.browserCard.left + 14, l.browserCard.top + 8, l.browserCard.right - 90, l.browserCard.top + 34};
    drawTextLine(dc, categoryName(gCategory), title, gWorkbenchHeadingFont, WB_TEXT);
    RECT count{l.browserCard.right - 110, l.browserCard.top + 8, l.browserCard.right - 14, l.browserCard.top + 34};
    drawTextLine(dc, std::to_string(gFilteredItems.size()), count, gWorkbenchSmallFont, WB_MUTED, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    RECT hint{l.browserCard.left + 14, l.browserCard.top + 34, l.browserCard.right - 14, l.browserCard.top + 56};
    drawTextLine(dc, "Selecciona un recurso para inspeccionarlo", hint, gWorkbenchSmallFont, WB_MUTED);

    HRGN clip = CreateRectRgn(l.list.left, l.list.top, l.list.right, l.list.bottom);
    SelectClipRgn(dc, clip);
    const int visibleRows = std::max(1, rectHeight(l.list) / WB_ROW_H);
    const std::size_t maxScroll = gFilteredItems.size() > static_cast<std::size_t>(visibleRows)
        ? gFilteredItems.size() - static_cast<std::size_t>(visibleRows) : 0;
    gResourceScroll = std::min(gResourceScroll, maxScroll);

    for (int rowIndex = 0; rowIndex < visibleRows + 1; ++rowIndex) {
        const std::size_t filteredIndex = gResourceScroll + static_cast<std::size_t>(rowIndex);
        if (filteredIndex >= gFilteredItems.size()) break;
        const std::size_t itemIndex = gFilteredItems[filteredIndex];
        const auto& item = gBrowserItems[itemIndex];
        RECT row{l.list.left, l.list.top + rowIndex * WB_ROW_H, l.list.right, l.list.top + (rowIndex + 1) * WB_ROW_H - 4};
        const bool selected = itemIndex == gSelectedItem;
        roundBox(dc, row, selected ? WB_ACCENT_SOFT : WB_PANEL_ALT, selected ? WB_ACCENT : WB_PANEL_ALT, 9);
        RECT badge{row.left + 8, row.top + 9, row.left + 48, row.bottom - 9};
        roundBox(dc, badge, selected ? WB_ACCENT : RGB(40, 47, 58), selected ? WB_ACCENT : WB_BORDER, 8);
        drawTextLine(dc, categoryBadge(item.category), badge, gWorkbenchSmallFont, WB_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        RECT name{row.left + 58, row.top + 5, row.right - 72, row.top + 29};
        drawTextLine(dc, item.id, name, gFontBody, WB_TEXT);
        RECT type{row.left + 58, row.top + 29, row.right - 72, row.bottom - 3};
        drawTextLine(dc, item.displayName.empty() ? categoryName(item.category) : item.displayName, type, gWorkbenchSmallFont, WB_MUTED);
        if (item.size) {
            RECT size{row.right - 72, row.top, row.right - 8, row.bottom};
            drawTextLine(dc, humanBytes(item.size), size, gWorkbenchSmallFont, WB_MUTED, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
    }
    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
}

void paintPreview(HDC dc, const WorkbenchLayout& l) {
    roundBox(dc, l.previewCard, WB_PANEL, WB_BORDER, 12);
    RECT title{l.previewCard.left + 14, l.previewCard.top + 8, l.previewCard.right - 14, l.previewCard.top + 36};
    if (gPreview3D && gScene) {
        drawTextLine(dc, gScene->modelName().empty() ? "Vista 3D" : gScene->modelName(), title, gWorkbenchHeadingFont, WB_TEXT);
        RECT meta{l.previewCard.right - 250, l.previewCard.top + 8, l.previewCard.right - 14, l.previewCard.top + 36};
        drawTextLine(dc, std::to_string(gScene->meshes().size()) + " meshes  |  " + std::to_string(gScene->textures().size()) + " texturas",
                     meta, gWorkbenchSmallFont, WB_MUTED, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    } else {
        drawTextLine(dc, "Vista previa", title, gWorkbenchHeadingFont, WB_TEXT);
        if (gSelectedItem < gBrowserItems.size()) {
            const auto& item = gBrowserItems[gSelectedItem];
            RECT badge{l.previewCard.left + rectWidth(l.previewCard) / 2 - 36,
                       l.previewCard.top + rectHeight(l.previewCard) / 2 - 62,
                       l.previewCard.left + rectWidth(l.previewCard) / 2 + 36,
                       l.previewCard.top + rectHeight(l.previewCard) / 2 + 10};
            roundBox(dc, badge, WB_PANEL_ALT, WB_BORDER, 16);
            drawTextLine(dc, categoryBadge(item.category), badge, gWorkbenchTitleFont, WB_ACCENT, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            RECT name{l.previewCard.left + 28, badge.bottom + 14, l.previewCard.right - 28, badge.bottom + 42};
            drawTextLine(dc, item.id, name, gWorkbenchHeadingFont, WB_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            RECT hint{l.previewCard.left + 28, name.bottom + 2, l.previewCard.right - 28, name.bottom + 30};
            drawTextLine(dc, "El recurso esta indexado y puede exportarse", hint, gWorkbenchSmallFont, WB_MUTED, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        } else {
            RECT empty{l.previewCard.left + 20, l.previewCard.top + 60, l.previewCard.right - 20, l.previewCard.bottom - 20};
            drawTextLine(dc, "Abre una ROM para explorar sus recursos", empty, gWorkbenchHeadingFont, WB_MUTED, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }
}

void drawInspectorTabs(HDC dc, const WorkbenchLayout& l) {
    static const char* labels[] = {"Informacion", "Animaciones", "Texturas"};
    for (int i = 0; i < 3; ++i) {
        RECT tab{l.inspectorCard.left + 12 + i * 122, l.inspectorCard.top + 8,
                 l.inspectorCard.left + 124 + i * 122, l.inspectorCard.top + 40};
        const bool enabled = i == 0 || (gScene && (i == 1 ? !gScene->animations().empty() : !gScene->textures().empty()));
        if (gInspectorTab == i) roundBox(dc, tab, WB_ACCENT_SOFT, WB_ACCENT_SOFT, 8);
        drawTextLine(dc, labels[i], tab, gWorkbenchSmallFont,
                     enabled ? (gInspectorTab == i ? WB_TEXT : RGB(192, 200, 211)) : RGB(86, 93, 103),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

void paintInspectorInfo(HDC dc, const WorkbenchLayout& l) {
    const int top = l.inspectorCard.top + 48;
    if (gPreview3D && gScene) {
        std::vector<std::pair<std::string, std::string>> rows = {
            {"Recurso", gSelectedModel},
            {"Meshes", std::to_string(gScene->meshes().size())},
            {"Huesos", std::to_string(gScene->bones().size())},
            {"Materiales", std::to_string(gScene->materials().size())},
            {"Texturas", std::to_string(gScene->textures().size())},
            {"Animaciones", std::to_string(gScene->animations().size())}
        };
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const int col = static_cast<int>(i % 2), row = static_cast<int>(i / 2);
            RECT key{l.inspectorCard.left + 18 + col * (rectWidth(l.inspectorCard) / 2), top + row * 40,
                     l.inspectorCard.left + 105 + col * (rectWidth(l.inspectorCard) / 2), top + row * 40 + 20};
            RECT value{key.right, key.top, l.inspectorCard.left + (col + 1) * (rectWidth(l.inspectorCard) / 2) - 16, key.bottom};
            drawTextLine(dc, rows[i].first, key, gWorkbenchSmallFont, WB_MUTED);
            drawTextLine(dc, rows[i].second, value, gWorkbenchSmallFont, WB_TEXT);
        }
    } else if (gSelectedItem < gBrowserItems.size()) {
        const auto& item = gBrowserItems[gSelectedItem];
        RECT a{l.inspectorCard.left + 18, top, l.inspectorCard.right - 18, top + 24};
        drawTextLine(dc, item.id, a, gWorkbenchHeadingFont, WB_TEXT);
        RECT b{a.left, a.bottom + 4, a.right, a.bottom + 28};
        drawTextLine(dc, std::string(categoryName(item.category)) + (item.size ? "  |  " + humanBytes(item.size) : ""), b, gWorkbenchSmallFont, WB_MUTED);
        RECT c{a.left, b.bottom + 4, a.right, b.bottom + 28};
        drawTextLine(dc, sourceFilename(item.sourceLabel), c, gWorkbenchSmallFont, WB_MUTED);
    }
}

void paintInspectorAnimations(HDC dc, const WorkbenchLayout& l) {
    if (!gScene) return;
    const int left = l.inspectorCard.left + 14;
    const int top = l.inspectorCard.top + 48;
    const int right = l.inspectorCard.right - 14;
    const int rowH = 30;
    const int visible = std::max(1, static_cast<int>((l.inspectorCard.bottom - top - 8) / rowH));
    const std::size_t totalRows = gScene->animations().size() + 1;
    const std::size_t maxScroll = totalRows > static_cast<std::size_t>(visible) ? totalRows - visible : 0;
    gAnimationScroll = std::min(gAnimationScroll, maxScroll);
    for (int i = 0; i < visible; ++i) {
        const std::size_t rowIndex = gAnimationScroll + static_cast<std::size_t>(i);
        if (rowIndex >= totalRows) break;
        RECT row{left, top + i * rowH, right, top + (i + 1) * rowH - 3};
        bool active = false;
        std::string text;
        if (rowIndex == 0) {
            text = "Pose base";
            active = gCurrent == nullptr;
        } else {
            const auto& animation = gScene->animations()[rowIndex - 1];
            text = animation.name + "   " + std::to_string(static_cast<int>(animation.frames)) + "f" + (animation.loop ? "   loop" : "");
            active = gCurrent == &animation;
        }
        if (active) roundBox(dc, row, WB_ACCENT_SOFT, WB_ACCENT_SOFT, 7);
        RECT textRect{row.left + 10, row.top, row.right - 10, row.bottom};
        drawTextLine(dc, text, textRect, gWorkbenchSmallFont, active ? WB_TEXT : RGB(196, 203, 213));
    }
}

void paintInspectorTextures(HDC dc, const WorkbenchLayout& l) {
    if (!gScene) return;
    const int top = l.inspectorCard.top + 48;
    const int bottom = l.inspectorCard.bottom - 8;
    const int split = l.inspectorCard.left + rectWidth(l.inspectorCard) * 42 / 100;
    RECT materialTitle{l.inspectorCard.left + 16, top, split - 8, top + 22};
    RECT textureTitle{split + 8, top, l.inspectorCard.right - 16, top + 22};
    drawTextLine(dc, "Material", materialTitle, gWorkbenchSmallFont, WB_MUTED);
    drawTextLine(dc, "Textura aplicada", textureTitle, gWorkbenchSmallFont, WB_MUTED);
    const int listTop = top + 26;
    const int rowH = 29;
    const int visible = std::max(1, (bottom - listTop) / rowH);

    const auto& materials = gScene->materials();
    const auto& textures = gScene->textures();
    if (!materials.empty()) gSelectedMaterial = std::min(gSelectedMaterial, materials.size() - 1);
    const std::size_t matMax = materials.size() > static_cast<std::size_t>(visible) ? materials.size() - visible : 0;
    const std::size_t texMax = textures.size() > static_cast<std::size_t>(visible) ? textures.size() - visible : 0;
    gMaterialScroll = std::min(gMaterialScroll, matMax);
    gTextureScroll = std::min(gTextureScroll, texMax);

    for (int i = 0; i < visible; ++i) {
        const std::size_t index = gMaterialScroll + static_cast<std::size_t>(i);
        if (index >= materials.size()) break;
        RECT row{l.inspectorCard.left + 14, listTop + i * rowH, split - 8, listTop + (i + 1) * rowH - 3};
        if (index == gSelectedMaterial) roundBox(dc, row, WB_ACCENT_SOFT, WB_ACCENT_SOFT, 7);
        RECT text{row.left + 9, row.top, row.right - 8, row.bottom};
        drawTextLine(dc, materials[index].name.empty() ? ("Material " + std::to_string(index)) : materials[index].name,
                     text, gWorkbenchSmallFont, index == gSelectedMaterial ? WB_TEXT : RGB(196, 203, 213));
    }
    for (int i = 0; i < visible; ++i) {
        const std::size_t index = gTextureScroll + static_cast<std::size_t>(i);
        if (index >= textures.size()) break;
        const bool active = !materials.empty() && materials[gSelectedMaterial].textureName == textures[index].name;
        RECT row{split + 6, listTop + i * rowH, l.inspectorCard.right - 14, listTop + (i + 1) * rowH - 3};
        if (active) roundBox(dc, row, WB_ACCENT_SOFT, WB_ACCENT_SOFT, 7);
        RECT text{row.left + 9, row.top, row.right - 86, row.bottom};
        drawTextLine(dc, textures[index].name, text, gWorkbenchSmallFont, active ? WB_TEXT : RGB(196, 203, 213));
        RECT dims{row.right - 82, row.top, row.right - 8, row.bottom};
        drawTextLine(dc, std::to_string(textures[index].width) + "x" + std::to_string(textures[index].height),
                     dims, gWorkbenchSmallFont, WB_MUTED, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

void paintInspector(HDC dc, const WorkbenchLayout& l) {
    roundBox(dc, l.inspectorCard, WB_PANEL, WB_BORDER, 12);
    drawInspectorTabs(dc, l);
    if (gInspectorTab == 1) paintInspectorAnimations(dc, l);
    else if (gInspectorTab == 2) paintInspectorTextures(dc, l);
    else paintInspectorInfo(dc, l);
}

void paintStatus(HDC dc, const WorkbenchLayout& l) {
    fillRectColor(dc, l.status, WB_SIDEBAR_BG);
    RECT text{l.status.left + 16, l.status.top, l.status.right - 120, l.status.bottom};
    drawTextLine(dc, gStatus, text, gWorkbenchSmallFont, WB_MUTED);
    if (gProgressTotal > 0) {
        const double ratio = std::min(1.0, static_cast<double>(gProgressCurrent) / static_cast<double>(std::max<std::size_t>(1, gProgressTotal)));
        RECT track{l.status.left, l.status.bottom - 3, l.status.right, l.status.bottom};
        fillRectColor(dc, track, RGB(35, 40, 48));
        RECT fill{track.left, track.top, track.left + static_cast<LONG>(rectWidth(track) * ratio), track.bottom};
        fillRectColor(dc, fill, WB_ACCENT);
    }
}

void paintWorkbench(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC target = BeginPaint(hwnd, &ps);
    RECT client{}; GetClientRect(hwnd, &client);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, std::max(1, rectWidth(client)), std::max(1, rectHeight(client)));
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);

    fillRectColor(dc, client, WB_BG);
    const auto l = workbenchLayout(hwnd);
    paintSidebar(dc, l);
    paintTopbar(dc, l);
    paintBrowser(dc, l);
    paintPreview(dc, l);
    paintInspector(dc, l);
    paintStatus(dc, l);

    BitBlt(target, 0, 0, rectWidth(client), rectHeight(client), dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

void scrollResourceList(HWND hwnd, int delta) {
    const auto l = workbenchLayout(hwnd);
    const int visible = std::max(1, rectHeight(l.list) / WB_ROW_H);
    const std::size_t maxScroll = gFilteredItems.size() > static_cast<std::size_t>(visible)
        ? gFilteredItems.size() - static_cast<std::size_t>(visible) : 0;
    if (delta < 0) gResourceScroll = std::min(maxScroll, gResourceScroll + 3);
    else gResourceScroll = gResourceScroll > 3 ? gResourceScroll - 3 : 0;
    InvalidateRect(hwnd, &l.browserCard, FALSE);
}

void handleInspectorClick(HWND hwnd, int x, int y) {
    const auto l = workbenchLayout(hwnd);
    for (int i = 0; i < 3; ++i) {
        RECT tab{l.inspectorCard.left + 12 + i * 122, l.inspectorCard.top + 8,
                 l.inspectorCard.left + 124 + i * 122, l.inspectorCard.top + 40};
        if (!pointIn(tab, x, y)) continue;
        if (i == 0 || (gScene && (i == 1 ? !gScene->animations().empty() : !gScene->textures().empty()))) gInspectorTab = i;
        InvalidateRect(hwnd, &l.inspectorCard, FALSE);
        return;
    }
    if (!gScene) return;
    const int top = l.inspectorCard.top + 48;
    const int listTop = top + (gInspectorTab == 2 ? 26 : 0);
    const int rowH = gInspectorTab == 2 ? 29 : 30;
    if (y < listTop || y >= l.inspectorCard.bottom - 8) return;
    const int row = (y - listTop) / rowH;

    if (gInspectorTab == 1) {
        const std::size_t index = gAnimationScroll + static_cast<std::size_t>(row);
        if (index == 0) { gCurrent = nullptr; gFrame = 0.0f; setStatus(gSelectedModel + " | pose base"); }
        else if (index - 1 < gScene->animations().size()) play(&gScene->animations()[index - 1]);
        InvalidateRect(hwnd, &l.inspectorCard, FALSE);
    } else if (gInspectorTab == 2) {
        const int split = l.inspectorCard.left + rectWidth(l.inspectorCard) * 42 / 100;
        if (x < split) {
            const std::size_t index = gMaterialScroll + static_cast<std::size_t>(row);
            if (index < gScene->materials().size()) gSelectedMaterial = index;
        } else {
            const std::size_t index = gTextureScroll + static_cast<std::size_t>(row);
            if (index < gScene->textures().size() && gSelectedMaterial < gScene->materials().size()) {
                gScene->materialsMutable()[gSelectedMaterial].textureName = gScene->textures()[index].name;
                setStatus("Textura " + gScene->textures()[index].name + " aplicada a " + gScene->materials()[gSelectedMaterial].name);
            }
        }
        InvalidateRect(hwnd, &l.inspectorCard, FALSE);
    }
}

void handleMainClick(HWND hwnd, int x, int y) {
    const auto l = workbenchLayout(hwnd);
    if (pointIn(l.openButton, x, y) && !gDumping) {
        const auto path = chooseRomFile(hwnd);
        if (!path.empty()) loadRomInput(path);
        return;
    }
    if (pointIn(l.exportButton, x, y) && !gDumping) {
        try { exportSelectedContext(); } catch (const std::exception& e) { MessageBoxA(hwnd, e.what(), "Exportacion", MB_OK | MB_ICONERROR); }
        return;
    }
    if (pointIn(l.dumpButton, x, y) && !gDumping) {
        try { exportAllWorkbench(); } catch (const std::exception& e) { MessageBoxA(hwnd, e.what(), "Dump", MB_OK | MB_ICONERROR); }
        return;
    }

    for (int i = 0; i < 7; ++i) {
        RECT row{12, 108 + i * 46, WB_SIDEBAR - 12, 148 + i * 46};
        if (pointIn(row, x, y)) {
            gCategory = i;
            gResourceScroll = 0;
            rebuildFilter();
            return;
        }
    }

    if (pointIn(l.list, x, y)) {
        const int row = (y - l.list.top) / WB_ROW_H;
        const std::size_t filtered = gResourceScroll + static_cast<std::size_t>(row);
        if (filtered < gFilteredItems.size()) activateBrowserItem(gFilteredItems[filtered]);
        return;
    }

    if (pointIn(l.inspectorCard, x, y)) handleInspectorClick(hwnd, x, y);
}

LRESULT CALLBACK WorkbenchWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            ensureUiResources();
            ensureWorkbenchResources();
            applyDarkTitleBar(hwnd);
            gWindow = hwnd;
            gSearchBox = CreateWindowExA(0, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                         0, 0, 10, 10, hwnd, controlId(WB_SEARCH_ID), GetModuleHandleA(nullptr), nullptr);
            if (gSearchBox) {
                SendMessage(gSearchBox, WM_SETFONT, reinterpret_cast<WPARAM>(gFontBody), TRUE);
                SendMessageA(gSearchBox, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(18, 8));
            }
            gRenderWindow = CreateWindowExA(0, "SmokeDetectorRenderModern", "",
                                            WS_CHILD | WS_CLIPSIBLINGS,
                                            0, 0, 1, 1, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            if (!gRenderWindow) return -1;
            DragAcceptFiles(hwnd, TRUE);
            rebuildFilter();
            layoutWorkbench(hwnd);
            return 0;
        }
        case WM_SIZE:
            layoutWorkbench(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lp);
            info->ptMinTrackSize = {WB_MIN_W, WB_MIN_H};
            return 0;
        }
        case WM_PAINT:
            paintWorkbench(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, WB_PANEL_ALT);
            SetTextColor(dc, WB_TEXT);
            return reinterpret_cast<LRESULT>(gEditBrush);
        }
        case WM_COMMAND:
            if (LOWORD(wp) == WB_SEARCH_ID && HIWORD(wp) == EN_CHANGE) {
                gResourceScroll = 0;
                rebuildFilter();
                return 0;
            }
            break;
        case WM_LBUTTONDOWN:
            if (!gDumping) handleMainClick(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        case WM_MOUSEWHEEL: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            const auto l = workbenchLayout(hwnd);
            const int direction = GET_WHEEL_DELTA_WPARAM(wp);
            if (pointIn(l.list, p.x, p.y)) {
                scrollResourceList(hwnd, direction);
                return 0;
            }
            if (pointIn(l.inspectorCard, p.x, p.y) && gScene) {
                std::size_t* scroll = nullptr;
                std::size_t count = 0;
                if (gInspectorTab == 1) { scroll = &gAnimationScroll; count = gScene->animations().size() + 1; }
                else if (gInspectorTab == 2) {
                    const int split = l.inspectorCard.left + rectWidth(l.inspectorCard) * 42 / 100;
                    if (p.x < split) { scroll = &gMaterialScroll; count = gScene->materials().size(); }
                    else { scroll = &gTextureScroll; count = gScene->textures().size(); }
                }
                if (scroll) {
                    if (direction < 0) *scroll = std::min(count ? count - 1 : 0, *scroll + 2);
                    else *scroll = *scroll > 2 ? *scroll - 2 : 0;
                    InvalidateRect(hwnd, &l.inspectorCard, FALSE);
                    return 0;
                }
            }
            break;
        }
        case WM_DROPFILES: {
            if (gDumping) return 0;
            const HDROP drop = reinterpret_cast<HDROP>(wp);
            wchar_t path[32768]{};
            if (DragQueryFileW(drop, 0, path, static_cast<UINT>(std::size(path))) > 0) loadRomInput(std::filesystem::path(path));
            DragFinish(drop);
            return 0;
        }
        case WM_CLOSE:
            if (!gDumping) DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            shutdownOpenGL();
            releaseWorkbenchResources();
            releaseUiResources();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    try {
        ensureUiResources();
        ensureWorkbenchResources();
        initializeUniversalArchives();

        WNDCLASSA renderClass{};
        renderClass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        renderClass.lpfnWndProc = RenderProc;
        renderClass.hInstance = instance;
        renderClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        renderClass.hbrBackground = gBrushBg;
        renderClass.lpszClassName = "SmokeDetectorRenderModern";
        if (!RegisterClassA(&renderClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            throw std::runtime_error("Could not register render surface");
        }

        WNDCLASSA wc{};
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WorkbenchWndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = gBrushBg;
        wc.lpszClassName = "SmokeDetectorWorkbench";
        if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            throw std::runtime_error("Could not register workbench window");
        }

        gWindow = CreateWindowExA(0, wc.lpszClassName, "Smoke Detector",
                                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 1320, 820,
                                  nullptr, nullptr, instance, nullptr);
        if (!gWindow || !gRenderWindow) throw std::runtime_error("Could not create workbench window");

        initOpenGL(gRenderWindow);
        if (gScene) uploadGlTextures();
        ShowWindow(gRenderWindow, gPreview3D ? SW_SHOW : SW_HIDE);
        layoutWorkbench(gWindow);
        ShowWindow(gWindow, show);
        UpdateWindow(gWindow);
        gLastTick = std::chrono::steady_clock::now();

        MSG msg{};
        bool running = true;
        while (running) {
            while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) { running = false; break; }
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            if (running) tick();
            Sleep(1);
        }
        return 0;
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "Smoke Detector error", MB_OK | MB_ICONERROR);
        releaseWorkbenchResources();
        releaseUiResources();
        return 1;
    }
}
