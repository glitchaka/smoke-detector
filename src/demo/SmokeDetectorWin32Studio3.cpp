#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <GL/gl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define WinMain SmokeDetectorModernLegacyWinMain
#include "SmokeDetectorWin32Modern.cpp"
#undef WinMain

namespace {

using promptemon::bch::Material;
using promptemon::bch::Scene;
using promptemon::bch::Vec2;
using promptemon::rom::ResourceKind;
using promptemon::rom::RomModelArchive;
using promptemon::rom::RomResourceArchive;
using smoke_detector::exporter::AssetExporter;
using smoke_detector::exporter::ResourcePackageExporter;

constexpr int SEARCH_ID = 9101;
constexpr int TOPBAR_H = 50;
constexpr int STATUS_H = 26;
constexpr int GAP = 10;
constexpr int LEFT_W = 326;
constexpr int RIGHT_W = 360;
constexpr int TREE_ROW_H = 29;
constexpr int MIN_W = 1140;
constexpr int MIN_H = 720;

constexpr COLORREF BG = RGB(48, 48, 48);
constexpr COLORREF PANEL = RGB(38, 38, 38);
constexpr COLORREF PANEL_2 = RGB(44, 44, 44);
constexpr COLORREF FIELD = RGB(54, 54, 54);
constexpr COLORREF BORDER = RGB(67, 67, 67);
constexpr COLORREF TEXT = RGB(224, 224, 224);
constexpr COLORREF MUTED = RGB(152, 152, 152);
constexpr COLORREF ACCENT = RGB(83, 124, 184);
constexpr COLORREF ACCENT_SOFT = RGB(55, 72, 96);

struct ResourceItem {
    std::string sourceLabel;
    std::string virtualPath;
    std::string id;
    std::string displayName;
    std::filesystem::path romPath;
    ResourceKind kind = ResourceKind::Unknown;
    std::uint64_t size = 0;
    int category = 6;
};

struct TreeRow {
    bool folder = false;
    std::string key;
    std::string label;
    int depth = 0;
    std::size_t itemIndex = static_cast<std::size_t>(-1);
};

struct UvFix {
    int transformType = 0; // 0 Maya, 1 SoftImage, 2 3ds Max
    float offsetU = 0.0f;
    float offsetV = 0.0f;
    float scaleU = 1.0f;
    float scaleV = 1.0f;
    float rotation = 0.0f; // radians
};

struct Layout {
    RECT client{};
    RECT topbar{};
    RECT left{};
    RECT search{};
    RECT filters{};
    RECT tree{};
    RECT preview{};
    RECT right{};
    RECT status{};
    RECT openButton{};
    RECT exportButton{};
    RECT dumpButton{};
};

struct AdjustRowLayout {
    RECT label{};
    RECT minus{};
    RECT value{};
    RECT plus{};
};

struct UvPanelLayout {
    RECT materialPrev{};
    RECT materialName{};
    RECT materialNext{};
    RECT transformLabel{};
    RECT transformButton{};
    AdjustRowLayout offsetU{};
    AdjustRowLayout offsetV{};
    AdjustRowLayout scaleU{};
    AdjustRowLayout scaleV{};
    AdjustRowLayout rotation{};
    RECT resetButton{};
    RECT textureList{};
};

HWND gSearch = nullptr;
HBRUSH gEditBrush = nullptr;
HFONT gTitleFont = nullptr;
HFONT gSectionFont = nullptr;
HFONT gTinyFont = nullptr;

std::vector<ResourceItem> gItems;
std::vector<std::size_t> gFiltered;
std::vector<TreeRow> gTreeRows;
std::unordered_set<std::string> gCollapsedFolders;
std::size_t gSelectedItem = static_cast<std::size_t>(-1);
std::size_t gTreeScroll = 0;
std::size_t gAnimationScroll = 0;
std::size_t gTextureScroll = 0;
std::size_t gSelectedMaterial = 0;
int gCategory = 0;
int gInspectorTab = 0; // 0 Properties, 1 Animation, 2 Textures / UV
bool gPreview3D = false;

std::unordered_map<std::string, std::vector<UvFix>> gUvFixesByModel;
std::unordered_map<std::string, std::vector<std::string>> gOriginalTexturesByModel;
std::unordered_map<std::string, std::vector<std::string>> gSelectedTexturesByModel;

std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool endsWithI(const std::string& value, const char* suffix) {
    const std::string a = lowerCopy(value);
    const std::string b = lowerCopy(suffix);
    return a.size() >= b.size() && a.compare(a.size() - b.size(), b.size(), b) == 0;
}

int categoryFor(const std::string& id, ResourceKind kind) {
    const std::string p = lowerCopy(id);
    const bool animationName = p.find("anim") != std::string::npos || p.find("motion") != std::string::npos;
    if (animationName && (p.find(".bch") != std::string::npos || p.find(".bcres") != std::string::npos)) return 2;
    if (kind == ResourceKind::Model || endsWithI(p, ".bch") || endsWithI(p, ".bch.cx") ||
        endsWithI(p, ".bcres") || endsWithI(p, ".bcmdl") || endsWithI(p, ".cgfx")) return 1;
    if (kind == ResourceKind::Texture || kind == ResourceKind::Graphics || kind == ResourceKind::Palette ||
        endsWithI(p, ".bclim") || endsWithI(p, ".ctpk") || endsWithI(p, ".png") ||
        endsWithI(p, ".tga") || endsWithI(p, ".bmp")) return 3;
    if (kind == ResourceKind::Audio || endsWithI(p, ".bcwav") || endsWithI(p, ".bcstm") ||
        endsWithI(p, ".bcsar") || endsWithI(p, ".bcseq") || endsWithI(p, ".cwav") ||
        endsWithI(p, ".cstm") || endsWithI(p, ".wav") || endsWithI(p, ".ogg")) return 4;
    if (kind == ResourceKind::Archive || endsWithI(p, ".garc") || endsWithI(p, ".narc") ||
        endsWithI(p, ".darc") || endsWithI(p, ".carc") || endsWithI(p, ".szs") ||
        endsWithI(p, ".arc") || endsWithI(p, ".cx")) return 5;
    return 6;
}

const char* badgeFor(int category) {
    switch (category) {
        case 1: return "3D";
        case 2: return "AN";
        case 3: return "TX";
        case 4: return "SFX";
        case 5: return "ARC";
        default: return "DAT";
    }
}

const char* filterName(int category) {
    switch (category) {
        case 1: return "3D";
        case 2: return "Anim";
        case 3: return "Tex";
        case 4: return "SFX";
        case 5: return "Arc";
        case 6: return "Data";
        default: return "All";
    }
}

std::string sourceFilename(const std::string& label) {
    const auto marker = label.find("] ");
    return marker == std::string::npos ? label : label.substr(marker + 2);
}

std::string humanBytes(std::uint64_t size) {
    std::ostringstream out;
    if (size >= 1024ull * 1024ull * 1024ull) out << size / (1024ull * 1024ull * 1024ull) << " GB";
    else if (size >= 1024ull * 1024ull) out << size / (1024ull * 1024ull) << " MB";
    else if (size >= 1024ull) out << size / 1024ull << " KB";
    else out << size << " B";
    return out.str();
}

int width(const RECT& r) { return static_cast<int>(r.right - r.left); }
int height(const RECT& r) { return static_cast<int>(r.bottom - r.top); }
bool hit(const RECT& r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

Layout makeLayout(HWND hwnd) {
    Layout l{};
    GetClientRect(hwnd, &l.client);
    const int w = width(l.client);
    const int h = height(l.client);
    l.topbar = {0, 0, w, TOPBAR_H};
    l.status = {0, h - STATUS_H, w, h};
    const int top = TOPBAR_H + GAP;
    const int bottom = h - STATUS_H - GAP;
    l.left = {GAP, top, GAP + LEFT_W, bottom};
    l.right = {w - GAP - RIGHT_W, top, w - GAP, bottom};
    l.preview = {l.left.right + GAP, top, l.right.left - GAP, bottom};
    l.search = {l.left.left + 12, l.left.top + 47, l.left.right - 12, l.left.top + 79};
    l.filters = {l.left.left + 12, l.left.top + 87, l.left.right - 12, l.left.top + 117};
    l.tree = {l.left.left + 8, l.left.top + 126, l.left.right - 8, l.left.bottom - 8};
    l.dumpButton = {w - 112, 9, w - 10, 41};
    l.exportButton = {w - 220, 9, w - 120, 41};
    l.openButton = {w - 328, 9, w - 228, 41};
    return l;
}

void ensureStudioResources() {
    ensureUiResources();
    if (!gEditBrush) gEditBrush = CreateSolidBrush(FIELD);
    if (!gTitleFont) gTitleFont = createUiFont(18, FW_SEMIBOLD);
    if (!gSectionFont) gSectionFont = createUiFont(13, FW_SEMIBOLD);
    if (!gTinyFont) gTinyFont = createUiFont(11, FW_NORMAL);
}

void releaseStudioResources() {
    if (gEditBrush) DeleteObject(gEditBrush);
    if (gTitleFont) DeleteObject(gTitleFont);
    if (gSectionFont) DeleteObject(gSectionFont);
    if (gTinyFont) DeleteObject(gTinyFont);
    gEditBrush = nullptr;
    gTitleFont = gSectionFont = gTinyFont = nullptr;
    releaseUiResources();
}

void fillRectColor(HDC dc, const RECT& r, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}

void drawBox(HDC dc, const RECT& r, COLORREF fill, COLORREF border = BORDER, int radius = 5) {
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

void drawTextLine(HDC dc, const std::string& value, RECT r, HFONT font, COLORREF color,
                  UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    SelectObject(dc, font);
    DrawTextA(dc, value.c_str(), -1, &r, flags);
}

void drawButton(HDC dc, const RECT& r, const char* label, bool accent, bool enabled) {
    drawBox(dc, r, enabled ? (accent ? ACCENT : FIELD) : PANEL_2,
            enabled ? (accent ? ACCENT : BORDER) : PANEL_2, 5);
    drawTextLine(dc, label, r, gFontSmall, enabled ? TEXT : RGB(92, 92, 92),
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

std::string searchText() {
    char text[1024]{};
    if (gSearch) GetWindowTextA(gSearch, text, static_cast<int>(sizeof(text)));
    return lowerCopy(text);
}

void rebuildItems() {
    gItems.clear();
    if (!gResourceArchive) return;
    for (const auto& source : gResourceArchive->sourceSummaries()) {
        for (const auto& resource : gResourceArchive->resourcesForSource(source.label)) {
            ResourceItem item;
            item.sourceLabel = source.label;
            item.virtualPath = source.label + " :: " + resource.id;
            item.id = resource.id;
            item.displayName = resource.displayName;
            item.romPath = source.rom.path;
            item.kind = resource.kind;
            item.size = resource.size;
            item.category = categoryFor(item.id, item.kind);
            gItems.push_back(std::move(item));
        }
    }
}

std::vector<std::string> splitPath(std::string path) {
    if (path.rfind("romfs/", 0) == 0) path.erase(0, 6);
    if (path.rfind("nitrofs/", 0) == 0) path.erase(0, 8);
    std::replace(path.begin(), path.end(), '\\', '/');
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto slash = path.find('/', start);
        std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!part.empty()) parts.push_back(std::move(part));
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return parts;
}

void rebuildTree() {
    gTreeRows.clear();
    std::unordered_set<std::string> emitted;
    for (const std::size_t itemIndex : gFiltered) {
        const auto parts = splitPath(gItems[itemIndex].id);
        if (parts.empty()) continue;
        std::string current;
        bool hidden = false;
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
            if (!current.empty()) current += '/';
            current += parts[i];
            if (hidden) continue;
            if (emitted.insert(current).second) {
                gTreeRows.push_back({true, current, parts[i], static_cast<int>(i), static_cast<std::size_t>(-1)});
            }
            if (gCollapsedFolders.count(current)) hidden = true;
        }
        if (!hidden) {
            gTreeRows.push_back({false, gItems[itemIndex].id, parts.back(), static_cast<int>(parts.size() - 1), itemIndex});
        }
    }
}

void applyFilter() {
    const std::string query = searchText();
    gFiltered.clear();
    for (std::size_t i = 0; i < gItems.size(); ++i) {
        const auto& item = gItems[i];
        if (gCategory != 0 && item.category != gCategory) continue;
        if (!query.empty()) {
            const std::string searchable = lowerCopy(item.id + " " + item.displayName + " " + item.sourceLabel);
            if (searchable.find(query) == std::string::npos) continue;
        }
        gFiltered.push_back(i);
    }
    std::sort(gFiltered.begin(), gFiltered.end(), [](std::size_t a, std::size_t b) {
        return lowerCopy(gItems[a].id) < lowerCopy(gItems[b].id);
    });
    if (gSelectedItem != static_cast<std::size_t>(-1) &&
        std::find(gFiltered.begin(), gFiltered.end(), gSelectedItem) == gFiltered.end()) {
        gSelectedItem = static_cast<std::size_t>(-1);
    }
    if (gSelectedItem == static_cast<std::size_t>(-1) && !gFiltered.empty()) gSelectedItem = gFiltered.front();
    rebuildTree();
    if (gWindow) InvalidateRect(gWindow, nullptr, FALSE);
}

std::string modelPathFor(const ResourceItem& item) {
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

void selectItemForLoadedModel() {
    if (gSelectedModel.empty()) return;
    const auto split = gSelectedModel.find(" :: ");
    if (split == std::string::npos) return;
    const std::string source = sourceFilename(gSelectedModel.substr(0, split));
    const std::string resource = gSelectedModel.substr(split + 4);
    for (std::size_t i = 0; i < gItems.size(); ++i) {
        std::string id = gItems[i].id;
        if (id.rfind("romfs/", 0) == 0) id.erase(0, 6);
        if (id.rfind("nitrofs/", 0) == 0) id.erase(0, 8);
        if (sourceFilename(gItems[i].sourceLabel) == source && id == resource) {
            gSelectedItem = i;
            return;
        }
    }
}

void clearScene() {
    if (gGL) destroyGlTextures();
    gScene.reset();
    gSelectedModel.clear();
    gIdle = gCurrent = nullptr;
    gFrame = gYaw = gPitch = 0.0f;
    gZoom = 1.0f;
    gPreview3D = false;
    gSelectedMaterial = 0;
    gAnimationScroll = gTextureScroll = 0;
}

std::vector<UvFix>& currentUvFixes() {
    static std::vector<UvFix> empty;
    if (!gScene || gSelectedModel.empty()) return empty;
    auto& fixes = gUvFixesByModel[gSelectedModel];
    if (fixes.size() != gScene->materials().size()) fixes.assign(gScene->materials().size(), UvFix{});
    return fixes;
}

void rememberModelTextures() {
    if (!gScene || gSelectedModel.empty()) return;
    auto& materials = gScene->materialsMutable();
    auto& original = gOriginalTexturesByModel[gSelectedModel];
    if (original.size() != materials.size()) {
        original.clear();
        for (const auto& material : materials) original.push_back(material.textureName);
    }
    auto& selected = gSelectedTexturesByModel[gSelectedModel];
    if (selected.size() != materials.size()) selected = original;
    for (std::size_t i = 0; i < materials.size(); ++i) materials[i].textureName = selected[i];
}

void captureLoadedModel() {
    if (!gScene || gSelectedModel.empty()) return;
    currentUvFixes();
    rememberModelTextures();
    if (!gScene->materials().empty()) gSelectedMaterial = std::min(gSelectedMaterial, gScene->materials().size() - 1);
    else gSelectedMaterial = 0;
}

void activateItem(std::size_t itemIndex) {
    if (itemIndex >= gItems.size()) return;
    gSelectedItem = itemIndex;
    const auto& item = gItems[itemIndex];
    const std::string model = item.category == 1 ? modelPathFor(item) : std::string{};
    if (!model.empty()) {
        try {
            loadModel(model);
            gPreview3D = true;
            captureLoadedModel();
            gInspectorTab = 0;
            ShowWindow(gRenderWindow, SW_SHOW);
        } catch (const std::exception& e) {
            gPreview3D = false;
            ShowWindow(gRenderWindow, SW_HIDE);
            setStatus(std::string("3D indexed; preview unavailable: ") + e.what());
        }
    } else {
        gPreview3D = false;
        ShowWindow(gRenderWindow, SW_HIDE);
        setStatus(item.id);
    }
    InvalidateRect(gWindow, nullptr, FALSE);
}

std::filesystem::path chooseRom(HWND owner) {
    wchar_t buffer[32768]{};
    static const wchar_t filter[] =
        L"ROMs (*.gb;*.gbc;*.gba;*.nds;*.cia;*.3ds;*.cci;*.cxi)\0"
        L"*.gb;*.gbc;*.gba;*.nds;*.cia;*.3ds;*.cci;*.cxi\0"
        L"All files (*.*)\0*.*\0\0";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = static_cast<DWORD>(std::size(buffer));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = L"Open ROM";
    return GetOpenFileNameW(&dialog) ? std::filesystem::path(buffer) : std::filesystem::path{};
}

std::filesystem::path commandLineRom() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return {};
    std::filesystem::path result;
    if (argc > 1) {
        std::error_code ec;
        std::filesystem::path candidate(argv[1]);
        if (std::filesystem::is_regular_file(candidate, ec) && !ec) result = candidate;
    }
    LocalFree(argv);
    return result;
}

bool loadRomInput(const std::filesystem::path& path, bool report = true) {
    try {
        auto resources = std::make_unique<RomResourceArchive>();
        if (!resources->open(path)) throw std::runtime_error("The ROM could not be indexed.");
        auto models = std::make_unique<RomModelArchive>();
        models->open(path);
        auto modelPaths = models->modelResources();

        clearScene();
        gResourceArchive = std::move(resources);
        gArchive = std::move(models);
        gModels = std::move(modelPaths);
        gRomDirectory = path;
        gExportRoot = path.parent_path() / "smoke-detector-exports";
        rebuildItems();
        gCategory = 0;
        gTreeScroll = 0;
        if (gSearch) SetWindowTextA(gSearch, "");
        applyFilter();

        for (const auto& model : gModels) {
            try {
                loadModel(model);
                gPreview3D = true;
                captureLoadedModel();
                selectItemForLoadedModel();
                break;
            } catch (...) {
            }
        }
        if (!gPreview3D && !gFiltered.empty()) gSelectedItem = gFiltered.front();

        const auto summaries = gResourceArchive->sourceSummaries();
        const std::string provider = summaries.empty() ? std::string{} : summaries.front().provider;
        const bool encrypted = provider.find("Encrypted 3DS RomFS") != std::string::npos ||
                               provider.find("does not contain slot0x") != std::string::npos ||
                               provider.find("does not decrypt") != std::string::npos;
        if (encrypted) {
            setStatus(provider);
            if (report) {
                const std::string message = provider +
                    "\r\n\r\nThis image is encrypted. Smoke Detector does not bundle Nintendo keys. "
                    "Place your own aes_keys.txt beside the ROM, then reopen it. "
                    "A decrypted 3DS/CXI works directly.";
                MessageBoxA(gWindow, message.c_str(), "Encrypted Nintendo 3DS ROM", MB_OK | MB_ICONINFORMATION);
            }
        } else {
            setStatus("Loaded " + path.filename().string() + " | " + std::to_string(gItems.size()) + " resources");
        }

        if (gWindow) {
            SetWindowTextA(gWindow, ("Smoke Detector - " + path.filename().string()).c_str());
            ShowWindow(gRenderWindow, gPreview3D ? SW_SHOW : SW_HIDE);
            InvalidateRect(gWindow, nullptr, FALSE);
        }
        return true;
    } catch (const std::exception& e) {
        if (report && gWindow) MessageBoxA(gWindow, e.what(), "Smoke Detector", MB_OK | MB_ICONERROR);
        return false;
    }
}

void initializeArchives() {
    const auto startup = commandLineRom();
    if (!startup.empty() && loadRomInput(startup, false)) return;
    gArchive = std::make_unique<RomModelArchive>();
    gResourceArchive = std::make_unique<RomResourceArchive>();
    gModels.clear();
    gItems.clear();
    gFiltered.clear();
    gTreeRows.clear();
    gExportRoot = executableDirectory() / "exports";
    setStatus("Open or drag a ROM to begin.");
}

Vec2 sourceTextureTransform(const Material* material, Vec2 uv, int transformType) {
    if (!material) return uv;
    const float sx = material->uvScaleU;
    const float sy = material->uvScaleV;
    const float tx = material->uvTranslateU;
    const float ty = material->uvTranslateV;
    const float rotation = material->uvRotation;
    const float ca = std::cos(rotation);
    const float sa = std::sin(rotation);

    Vec2 out{};
    out.x = uv.x * (sx * ca) + uv.y * (sx * -sa);
    out.y = uv.x * (sy * sa) + uv.y * (sy * ca);

    if (transformType == 1) {
        out.x += sx * (-ca * tx - sa * ty);
        out.y += sy * ( sa * tx - ca * ty);
    } else if (transformType == 2) {
        out.x += sx * ca * (-tx - 0.5f) - sx * sa * (ty - 0.5f) + 0.5f;
        out.y += sy * sa * (-tx - 0.5f) + sy * ca * (ty - 0.5f) + 0.5f;
    } else {
        out.x += sx * ((0.5f * sa - 0.5f * ca) + 0.5f - tx);
        out.y += sy * ((-0.5f * sa - 0.5f * ca) + 0.5f - ty);
    }
    return out;
}

Vec2 adjustedUv(const Material* material, Vec2 uv, const UvFix* fix) {
    const int transformType = fix ? fix->transformType : 0;
    Vec2 out = sourceTextureTransform(material, uv, transformType);
    if (!fix) return out;

    const float u = (out.x - 0.5f) * fix->scaleU;
    const float v = (out.y - 0.5f) * fix->scaleV;
    const float ca = std::cos(fix->rotation);
    const float sa = std::sin(fix->rotation);
    return {
        u * ca - v * sa + 0.5f + fix->offsetU,
        u * sa + v * ca + 0.5f + fix->offsetV
    };
}

const UvFix* uvFixForMaterial(std::size_t materialIndex) {
    if (!gScene || gSelectedModel.empty()) return nullptr;
    auto& fixes = currentUvFixes();
    return materialIndex < fixes.size() ? &fixes[materialIndex] : nullptr;
}

void drawSceneCorrected() {
    if (!gRenderWindow || !gGL) return;
    RECT rc{};
    GetClientRect(gRenderWindow, &rc);
    setupProjection(static_cast<int>(rc.right - rc.left), static_cast<int>(rc.bottom - rc.top));
    glClearColor(0.115f, 0.115f, 0.115f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!gScene) {
        SwapBuffers(gDC);
        return;
    }

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -3.25f / std::max(0.35f, gZoom));
    glRotatef(gPitch, 1.0f, 0.0f, 0.0f);
    glRotatef(gYaw, 0.0f, 1.0f, 0.0f);
    glScalef(gFitScale, gFitScale, gFitScale);
    glTranslatef(-gModelCenter.x, -gModelCenter.y, -gModelCenter.z);

    const auto matrices = gScene->samplePose(gCurrent, gFrame);
    for (const auto& mesh : gScene->meshes()) {
        const auto* texture = gScene->textureForMaterial(mesh.materialId);
        const Material* material = mesh.materialId < gScene->materials().size() ? &gScene->materials()[mesh.materialId] : nullptr;
        GLuint textureId = 0;
        if (texture) {
            const auto found = gGlTextures.find(texture->name);
            if (found != gGlTextures.end()) textureId = found->second;
        }
        if (textureId) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, textureId);
            glColor4f(1, 1, 1, 1);
        } else {
            glDisable(GL_TEXTURE_2D);
            glColor4f(0.78f, 0.78f, 0.80f, 1.0f);
        }
        const UvFix* fix = uvFixForMaterial(mesh.materialId);
        glBegin(GL_TRIANGLES);
        for (const auto& vertex : mesh.vertices) {
            const auto p = gScene->skin(vertex, matrices);
            if (textureId) {
                const Vec2 uv = adjustedUv(material, vertex.uv, fix);
                glTexCoord2f(uv.x, 1.0f - uv.y);
            }
            glVertex3f(p.x, p.y, p.z);
        }
        glEnd();
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    SwapBuffers(gDC);
}

void tickCorrected() {
    const auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - gLastTick).count();
    gLastTick = now;
    dt = std::clamp(dt, 0.0f, 0.1f);
    if (gCurrent) {
        gFrame += dt * 30.0f;
        if (gCurrent->frames > 0.0f && gFrame > gCurrent->frames) {
            if (gCurrent->loop) gFrame = std::fmod(gFrame, gCurrent->frames);
            else {
                gCurrent = gIdle;
                gFrame = 0.0f;
            }
        }
    }
    drawSceneCorrected();
}

std::string safeName(std::string value) {
    for (char& c : value) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '-' || c == '_' || c == '.')) c = '_';
    }
    while (!value.empty() && value.front() == '.') value.erase(value.begin());
    return value.empty() ? "asset" : value;
}

void writeLiveObj(const std::filesystem::path& modelDir, const Scene& scene, const std::vector<UvFix>& fixes) {
    std::ofstream obj(modelDir / "model.obj");
    std::ofstream mtl(modelDir / "model.mtl");
    if (!obj || !mtl) throw std::runtime_error("Could not update corrected OBJ");
    obj << "mtllib model.mtl\n";
    const auto pose = scene.samplePose(nullptr, 0.0f);
    std::size_t base = 1;
    for (std::size_t meshIndex = 0; meshIndex < scene.meshes().size(); ++meshIndex) {
        const auto& mesh = scene.meshes()[meshIndex];
        const Material* material = mesh.materialId < scene.materials().size() ? &scene.materials()[mesh.materialId] : nullptr;
        const UvFix* fix = mesh.materialId < fixes.size() ? &fixes[mesh.materialId] : nullptr;
        const std::string materialName = safeName(material && !material->name.empty()
            ? material->name : "material_" + std::to_string(meshIndex));
        obj << "o " << safeName(mesh.name) << "\nusemtl " << materialName << "\n";
        for (const auto& vertex : mesh.vertices) {
            const auto p = scene.skin(vertex, pose);
            obj << "v " << p.x << ' ' << p.y << ' ' << p.z << "\n";
        }
        for (const auto& vertex : mesh.vertices) {
            const Vec2 uv = adjustedUv(material, vertex.uv, fix);
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
            mtl << "map_Kd ../textures/" << safeName(material->textureName) << ".tga\n";
        }
        mtl << "\n";
        base += mesh.vertices.size();
    }

    std::ofstream uv(modelDir / "uv-adjustments.json");
    if (uv) {
        uv << "{\n  \"materials\": [\n";
        for (std::size_t i = 0; i < scene.materials().size(); ++i) {
            const auto fix = i < fixes.size() ? fixes[i] : UvFix{};
            uv << "    {\"name\":\"" << safeName(scene.materials()[i].name)
               << "\",\"transform\":" << fix.transformType
               << ",\"offset_u\":" << fix.offsetU
               << ",\"offset_v\":" << fix.offsetV
               << ",\"scale_u\":" << fix.scaleU
               << ",\"scale_v\":" << fix.scaleV
               << ",\"rotation\":" << fix.rotation << "}";
            if (i + 1 < scene.materials().size()) uv << ',';
            uv << "\n";
        }
        uv << "  ]\n}\n";
    }
}

void exportSelected() {
    if (gSelectedItem >= gItems.size()) return;
    const auto& item = gItems[gSelectedItem];
    const std::string model = item.category == 1 ? modelPathFor(item) : std::string{};
    if (!model.empty() && gArchive) {
        const auto result = AssetExporter::exportModel(*gArchive, model, gExportRoot);
        if (gScene && gSelectedModel == model) {
            writeLiveObj(result.interchangeModel.parent_path(), *gScene, currentUvFixes());
        }
        setStatus("Exported: " + result.interchangeModel.string());
        ShellExecuteA(gWindow, "open", result.interchangeModel.parent_path().string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }

    const auto outDir = gExportRoot / "selected" / safeName(sourceFilename(item.sourceLabel));
    std::filesystem::create_directories(outDir);
    const auto slash = item.id.find_last_of("/\\");
    const auto outPath = outDir / safeName(slash == std::string::npos ? item.id : item.id.substr(slash + 1));
    if (item.kind == ResourceKind::RawRom) {
        std::error_code ec;
        std::filesystem::copy_file(item.romPath, outPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) throw std::runtime_error(ec.message());
    } else {
        const auto payload = gResourceArchive->extract(item.virtualPath);
        std::ofstream out(outPath, std::ios::binary);
        if (!out) throw std::runtime_error("Could not create export file");
        if (!payload.bytes.empty()) {
            out.write(reinterpret_cast<const char*>(payload.bytes.data()), static_cast<std::streamsize>(payload.bytes.size()));
        }
    }
    setStatus("Exported: " + outPath.string());
    ShellExecuteA(gWindow, "open", outDir.string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void dumpAll() {
    if (!gResourceArchive || gDumping) return;
    gDumping = true;
    setProgress(0, 1, {});
    setStatus("Dumping all resources...");
    pumpUi();
    try {
        const auto results = ResourcePackageExporter::exportAll(
            *gResourceArchive, gExportRoot, {},
            [](std::size_t current, std::size_t total, const std::string&, const std::string& resource) {
                setProgress(total ? std::min(current + 1, total) : 0, total, resource);
                setStatus(resource.empty() ? "Finalizing dump..." : "Dump: " + resource);
                pumpUi();
            }, false);
        std::size_t count = 0;
        for (const auto& result : results) count += result.resources;
        gDumping = false;
        setProgress(0, 0, {});
        setStatus("Dump complete: " + std::to_string(count) + " resources");
        ShellExecuteA(gWindow, "open", (gExportRoot / "resources").string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } catch (...) {
        gDumping = false;
        setProgress(0, 0, {});
        throw;
    }
}

void layoutControls(HWND hwnd) {
    const auto l = makeLayout(hwnd);
    if (gSearch) {
        MoveWindow(gSearch, l.search.left + 8, l.search.top + 2,
                   std::max(10, width(l.search) - 16), height(l.search) - 4, TRUE);
    }
    if (gRenderWindow) {
        RECT render{l.preview.left + 1, l.preview.top + 37, l.preview.right - 1, l.preview.bottom - 1};
        MoveWindow(gRenderWindow, render.left, render.top,
                   std::max(1, width(render)), std::max(1, height(render)), TRUE);
        ShowWindow(gRenderWindow, gPreview3D ? SW_SHOW : SW_HIDE);
    }
}

void drawSection(HDC dc, const Layout& l, int& y, const char* label) {
    RECT row{l.right.left + 10, y, l.right.right - 10, y + 27};
    fillRectColor(dc, row, PANEL_2);
    RECT arrow{row.left + 7, row.top, row.left + 23, row.bottom};
    drawTextLine(dc, "v", arrow, gTinyFont, MUTED, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT textRect{arrow.right + 2, row.top, row.right - 8, row.bottom};
    drawTextLine(dc, label, textRect, gSectionFont, TEXT);
    y += 33;
}

void drawProperty(HDC dc, const Layout& l, int& y, const std::string& key, const std::string& value) {
    RECT keyRect{l.right.left + 18, y, l.right.left + 112, y + 24};
    RECT valueRect{keyRect.right, y, l.right.right - 18, y + 24};
    drawTextLine(dc, key, keyRect, gTinyFont, MUTED);
    drawTextLine(dc, value, valueRect, gTinyFont, TEXT);
    y += 25;
}

AdjustRowLayout makeAdjustRow(const Layout& l, int y, const char* label) {
    (void)label;
    AdjustRowLayout row;
    row.label = {l.right.left + 18, y, l.right.left + 138, y + 24};
    row.minus = {l.right.right - 122, y, l.right.right - 96, y + 24};
    row.value = {l.right.right - 94, y, l.right.right - 38, y + 24};
    row.plus = {l.right.right - 36, y, l.right.right - 10, y + 24};
    return row;
}

UvPanelLayout makeUvPanelLayout(const Layout& l) {
    UvPanelLayout p{};
    int y = l.right.top + 52;
    y += 33; // Material section header.
    p.materialPrev = {l.right.left + 18, y, l.right.left + 46, y + 27};
    p.materialNext = {l.right.right - 46, y, l.right.right - 18, y + 27};
    p.materialName = {p.materialPrev.right + 5, y, p.materialNext.left - 5, y + 27};
    y += 36;
    y += 33; // UV correction section header.
    p.transformLabel = {l.right.left + 18, y, l.right.left + 138, y + 24};
    p.transformButton = {l.right.right - 122, y, l.right.right - 10, y + 24};
    y += 28;
    p.offsetU = makeAdjustRow(l, y, "Offset U"); y += 27;
    p.offsetV = makeAdjustRow(l, y, "Offset V"); y += 27;
    p.scaleU = makeAdjustRow(l, y, "Scale U"); y += 27;
    p.scaleV = makeAdjustRow(l, y, "Scale V"); y += 27;
    p.rotation = makeAdjustRow(l, y, "Rotation"); y += 31;
    p.resetButton = {l.right.left + 18, y, l.right.right - 18, y + 27};
    y += 36;
    y += 33; // Texture section header.
    p.textureList = {l.right.left + 10, y, l.right.right - 10, l.right.bottom - 8};
    return p;
}

std::string fixed3(float value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3) << value;
    return out.str();
}

std::string degreesLabel(float radians) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << (radians * 57.2957795131f) << " deg";
    return out.str();
}

const char* transformName(int type) {
    return type == 1 ? "SoftImage" : (type == 2 ? "3ds Max" : "Maya");
}

void paintAdjustRow(HDC dc, const AdjustRowLayout& row, const char* label, const std::string& value) {
    drawTextLine(dc, label, row.label, gTinyFont, MUTED);
    drawBox(dc, row.minus, FIELD);
    drawBox(dc, row.value, PANEL_2);
    drawBox(dc, row.plus, FIELD);
    drawTextLine(dc, "-", row.minus, gTinyFont, TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    drawTextLine(dc, value, row.value, gTinyFont, TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    drawTextLine(dc, "+", row.plus, gTinyFont, TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void paintTopbar(HDC dc, const Layout& l) {
    fillRectColor(dc, l.topbar, PANEL);
    RECT title{16, 0, 280, TOPBAR_H};
    drawTextLine(dc, "Smoke Detector", title, gTitleFont, TEXT);
    drawButton(dc, l.openButton, "Open ROM", false, !gDumping);
    drawButton(dc, l.exportButton, "Export", false, !gDumping && gSelectedItem < gItems.size());
    drawButton(dc, l.dumpButton, "Dump all", true, !gDumping && !gItems.empty());
}

void paintLeftPanel(HDC dc, const Layout& l) {
    drawBox(dc, l.left, PANEL);
    RECT heading{l.left.left + 12, l.left.top + 8, l.left.right - 12, l.left.top + 38};
    drawTextLine(dc, "Resources", heading, gSectionFont, TEXT);
    RECT count{l.left.right - 70, heading.top, l.left.right - 12, heading.bottom};
    drawTextLine(dc, std::to_string(gFiltered.size()), count, gTinyFont, MUTED,
                 DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    drawBox(dc, l.search, FIELD);

    const int chipGap = 4;
    const int chipWidth = (width(l.filters) - chipGap * 6) / 7;
    for (int i = 0; i < 7; ++i) {
        RECT chip{l.filters.left + i * (chipWidth + chipGap), l.filters.top,
                  l.filters.left + i * (chipWidth + chipGap) + chipWidth, l.filters.bottom};
        const bool selected = i == gCategory;
        drawBox(dc, chip, selected ? ACCENT_SOFT : PANEL_2, selected ? ACCENT : BORDER);
        drawTextLine(dc, filterName(i), chip, gTinyFont, selected ? TEXT : MUTED,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    HRGN clip = CreateRectRgn(l.tree.left, l.tree.top, l.tree.right, l.tree.bottom);
    SelectClipRgn(dc, clip);
    const int visibleRows = std::max(1, height(l.tree) / TREE_ROW_H);
    const std::size_t maxScroll = gTreeRows.size() > static_cast<std::size_t>(visibleRows)
        ? gTreeRows.size() - visibleRows : 0;
    gTreeScroll = std::min(gTreeScroll, maxScroll);
    for (int i = 0; i <= visibleRows; ++i) {
        const std::size_t rowIndex = gTreeScroll + static_cast<std::size_t>(i);
        if (rowIndex >= gTreeRows.size()) break;
        const auto& row = gTreeRows[rowIndex];
        RECT rowRect{l.tree.left, l.tree.top + i * TREE_ROW_H,
                     l.tree.right, l.tree.top + (i + 1) * TREE_ROW_H - 1};
        const bool selected = !row.folder && row.itemIndex == gSelectedItem;
        if (selected) fillRectColor(dc, rowRect, ACCENT_SOFT);
        const int indent = std::min(76, row.depth * 14);
        RECT icon{rowRect.left + 6 + indent, rowRect.top, rowRect.left + 28 + indent, rowRect.bottom};
        const std::string glyph = row.folder
            ? (gCollapsedFolders.count(row.key) ? ">" : "v")
            : badgeFor(gItems[row.itemIndex].category);
        drawTextLine(dc, glyph, icon, gTinyFont, row.folder ? MUTED : ACCENT,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT label{icon.right + 1, rowRect.top, rowRect.right - 8, rowRect.bottom};
        drawTextLine(dc, row.label, label, gFontSmall, selected ? TEXT : (row.folder ? RGB(192, 192, 192) : TEXT));
    }
    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
}

void paintPreview(HDC dc, const Layout& l) {
    drawBox(dc, l.preview, PANEL);
    RECT heading{l.preview.left + 12, l.preview.top, l.preview.right - 12, l.preview.top + 36};
    drawTextLine(dc, "Preview", heading, gSectionFont, TEXT);
    if (gPreview3D && gScene) {
        RECT modelName{l.preview.left + 100, l.preview.top, l.preview.right - 12, l.preview.top + 36};
        drawTextLine(dc, gScene->modelName(), modelName, gTinyFont, MUTED,
                     DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return;
    }
    if (gSelectedItem >= gItems.size()) return;
    const auto& item = gItems[gSelectedItem];
    const int cx = (l.preview.left + l.preview.right) / 2;
    const int cy = (l.preview.top + 37 + l.preview.bottom) / 2;
    RECT badge{cx - 34, cy - 48, cx + 34, cy + 20};
    drawBox(dc, badge, FIELD);
    drawTextLine(dc, badgeFor(item.category), badge, gTitleFont, ACCENT,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT name{l.preview.left + 30, badge.bottom + 12, l.preview.right - 30, badge.bottom + 38};
    drawTextLine(dc, item.id, name, gFontBody, TEXT,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT hint{name.left, name.bottom, name.right, name.bottom + 24};
    drawTextLine(dc, "Indexed resource - export available", hint, gTinyFont, MUTED,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void paintProperties(HDC dc, const Layout& l) {
    if (gSelectedItem >= gItems.size()) return;
    int y = l.right.top + 52;
    const auto& item = gItems[gSelectedItem];
    drawSection(dc, l, y, "Resource");
    drawProperty(dc, l, y, "Name", item.displayName.empty() ? item.id : item.displayName);
    drawProperty(dc, l, y, "Type", filterName(item.category));
    drawProperty(dc, l, y, "Size", item.size ? humanBytes(item.size) : "unknown");
    y += 5;
    if (gPreview3D && gScene) {
        drawSection(dc, l, y, "Geometry");
        drawProperty(dc, l, y, "Meshes", std::to_string(gScene->meshes().size()));
        drawProperty(dc, l, y, "Bones", std::to_string(gScene->bones().size()));
        drawProperty(dc, l, y, "Materials", std::to_string(gScene->materials().size()));
        drawProperty(dc, l, y, "Textures", std::to_string(gScene->textures().size()));
        drawProperty(dc, l, y, "Animations", std::to_string(gScene->animations().size()));
        y += 5;
    }
    drawSection(dc, l, y, "Source");
    drawProperty(dc, l, y, "ROM", sourceFilename(item.sourceLabel));
    drawProperty(dc, l, y, "Path", item.id);
}

void paintAnimations(HDC dc, const Layout& l) {
    if (!gScene) return;
    const int top = l.right.top + 52;
    const int rowHeight = 30;
    const int visible = std::max(1, (l.right.bottom - top - 12) / rowHeight);
    const std::size_t total = gScene->animations().size() + 1;
    const std::size_t maxScroll = total > static_cast<std::size_t>(visible) ? total - visible : 0;
    gAnimationScroll = std::min(gAnimationScroll, maxScroll);
    for (int i = 0; i < visible; ++i) {
        const std::size_t index = gAnimationScroll + static_cast<std::size_t>(i);
        if (index >= total) break;
        RECT row{l.right.left + 10, top + i * rowHeight,
                 l.right.right - 10, top + (i + 1) * rowHeight - 2};
        bool active = false;
        std::string label;
        if (index == 0) {
            label = "Base pose";
            active = gCurrent == nullptr;
        } else {
            const auto& animation = gScene->animations()[index - 1];
            label = animation.name + "   " + std::to_string(static_cast<int>(animation.frames)) + "f";
            active = gCurrent == &animation;
        }
        if (active) fillRectColor(dc, row, ACCENT_SOFT);
        RECT textRect{row.left + 8, row.top, row.right - 8, row.bottom};
        drawTextLine(dc, label, textRect, gTinyFont, active ? TEXT : RGB(195, 195, 195));
    }
}

void paintTexturesAndUv(HDC dc, const Layout& l) {
    if (!gScene) return;
    auto& materials = gScene->materialsMutable();
    auto& fixes = currentUvFixes();
    if (materials.empty()) return;
    gSelectedMaterial = std::min(gSelectedMaterial, materials.size() - 1);
    const auto p = makeUvPanelLayout(l);

    int y = l.right.top + 52;
    drawSection(dc, l, y, "Material");
    drawBox(dc, p.materialPrev, FIELD);
    drawBox(dc, p.materialName, PANEL_2);
    drawBox(dc, p.materialNext, FIELD);
    drawTextLine(dc, "<", p.materialPrev, gTinyFont, TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    drawTextLine(dc, ">", p.materialNext, gTinyFont, TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    const std::string materialName = materials[gSelectedMaterial].name.empty()
        ? "Material " + std::to_string(gSelectedMaterial) : materials[gSelectedMaterial].name;
    drawTextLine(dc, materialName, p.materialName, gTinyFont, TEXT,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    y = p.materialPrev.bottom + 9;

    drawSection(dc, l, y, "UV correction");
    auto& fix = fixes[gSelectedMaterial];
    drawTextLine(dc, "Transform", p.transformLabel, gTinyFont, MUTED);
    drawBox(dc, p.transformButton, FIELD);
    drawTextLine(dc, transformName(fix.transformType), p.transformButton, gTinyFont, TEXT,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    paintAdjustRow(dc, p.offsetU, "Offset U", fixed3(fix.offsetU));
    paintAdjustRow(dc, p.offsetV, "Offset V", fixed3(fix.offsetV));
    paintAdjustRow(dc, p.scaleU, "Scale U", fixed3(fix.scaleU));
    paintAdjustRow(dc, p.scaleV, "Scale V", fixed3(fix.scaleV));
    paintAdjustRow(dc, p.rotation, "Rotation", degreesLabel(fix.rotation));
    drawButton(dc, p.resetButton, "Reset material", false, true);
    y = p.resetButton.bottom + 9;

    drawSection(dc, l, y, "Texture");
    const auto& textures = gScene->textures();
    const int visible = std::max(1, height(p.textureList) / TREE_ROW_H);
    const std::size_t maxScroll = textures.size() > static_cast<std::size_t>(visible)
        ? textures.size() - visible : 0;
    gTextureScroll = std::min(gTextureScroll, maxScroll);
    for (int i = 0; i < visible; ++i) {
        const std::size_t index = gTextureScroll + static_cast<std::size_t>(i);
        if (index >= textures.size()) break;
        const bool active = materials[gSelectedMaterial].textureName == textures[index].name;
        RECT row{p.textureList.left, p.textureList.top + i * TREE_ROW_H,
                 p.textureList.right, p.textureList.top + (i + 1) * TREE_ROW_H - 1};
        if (active) fillRectColor(dc, row, ACCENT_SOFT);
        RECT name{row.left + 8, row.top, row.right - 76, row.bottom};
        RECT dims{row.right - 72, row.top, row.right - 8, row.bottom};
        drawTextLine(dc, textures[index].name, name, gTinyFont, active ? TEXT : RGB(195, 195, 195));
        drawTextLine(dc, std::to_string(textures[index].width) + "x" + std::to_string(textures[index].height),
                     dims, gTinyFont, MUTED, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

void paintRightPanel(HDC dc, const Layout& l) {
    drawBox(dc, l.right, PANEL);
    static const char* tabs[] = {"Properties", "Animation", "Textures / UV"};
    const int tabWidth = (width(l.right) - 20) / 3;
    for (int i = 0; i < 3; ++i) {
        RECT tab{l.right.left + 10 + i * tabWidth, l.right.top + 8,
                 l.right.left + 10 + (i + 1) * tabWidth, l.right.top + 40};
        const bool enabled = i == 0 || (gScene && (i == 1 ? !gScene->animations().empty() : !gScene->textures().empty()));
        if (gInspectorTab == i) fillRectColor(dc, tab, FIELD);
        drawTextLine(dc, tabs[i], tab, gTinyFont,
                     enabled ? (gInspectorTab == i ? TEXT : MUTED) : RGB(82, 82, 82),
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if (gInspectorTab == 1) paintAnimations(dc, l);
    else if (gInspectorTab == 2) paintTexturesAndUv(dc, l);
    else paintProperties(dc, l);
}

void paintStatus(HDC dc, const Layout& l) {
    fillRectColor(dc, l.status, PANEL);
    RECT textRect{12, l.status.top, l.status.right - 12, l.status.bottom};
    drawTextLine(dc, gStatus, textRect, gTinyFont, MUTED);
    if (gProgressTotal) {
        const double ratio = std::min(1.0, static_cast<double>(gProgressCurrent) /
            static_cast<double>(std::max<std::size_t>(1, gProgressTotal)));
        RECT progress{0, l.status.bottom - 2,
                      static_cast<LONG>(width(l.status) * ratio), l.status.bottom};
        fillRectColor(dc, progress, ACCENT);
    }
}

void paintWindow(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC target = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, std::max(1, width(client)), std::max(1, height(client)));
    HGDIOBJ old = SelectObject(dc, bitmap);
    fillRectColor(dc, client, BG);
    const auto l = makeLayout(hwnd);
    paintTopbar(dc, l);
    paintLeftPanel(dc, l);
    paintPreview(dc, l);
    paintRightPanel(dc, l);
    paintStatus(dc, l);
    BitBlt(target, 0, 0, width(client), height(client), dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

float coarseMultiplier() {
    return (GetKeyState(VK_SHIFT) & 0x8000) ? 10.0f : 1.0f;
}

bool adjustField(const AdjustRowLayout& row, int x, int y, float& value, float baseStep) {
    const float step = baseStep * coarseMultiplier();
    if (hit(row.minus, x, y)) {
        value -= step;
        return true;
    }
    if (hit(row.plus, x, y)) {
        value += step;
        return true;
    }
    return false;
}

void handleTexturesUvClick(HWND hwnd, int x, int y) {
    if (!gScene || gScene->materials().empty()) return;
    const auto l = makeLayout(hwnd);
    const auto p = makeUvPanelLayout(l);
    auto& materials = gScene->materialsMutable();
    auto& fixes = currentUvFixes();
    gSelectedMaterial = std::min(gSelectedMaterial, materials.size() - 1);

    if (hit(p.materialPrev, x, y)) {
        gSelectedMaterial = gSelectedMaterial == 0 ? materials.size() - 1 : gSelectedMaterial - 1;
        gTextureScroll = 0;
        InvalidateRect(hwnd, &l.right, FALSE);
        return;
    }
    if (hit(p.materialNext, x, y)) {
        gSelectedMaterial = (gSelectedMaterial + 1) % materials.size();
        gTextureScroll = 0;
        InvalidateRect(hwnd, &l.right, FALSE);
        return;
    }

    auto& fix = fixes[gSelectedMaterial];
    if (hit(p.transformButton, x, y)) {
        fix.transformType = (fix.transformType + 1) % 3;
        InvalidateRect(hwnd, &l.right, FALSE);
        return;
    }
    if (adjustField(p.offsetU, x, y, fix.offsetU, 0.01f) ||
        adjustField(p.offsetV, x, y, fix.offsetV, 0.01f) ||
        adjustField(p.scaleU, x, y, fix.scaleU, 0.02f) ||
        adjustField(p.scaleV, x, y, fix.scaleV, 0.02f) ||
        adjustField(p.rotation, x, y, fix.rotation, 0.01745329252f)) {
        fix.scaleU = std::max(0.02f, fix.scaleU);
        fix.scaleV = std::max(0.02f, fix.scaleV);
        InvalidateRect(hwnd, &l.right, FALSE);
        return;
    }

    if (hit(p.resetButton, x, y)) {
        fix = UvFix{};
        auto original = gOriginalTexturesByModel.find(gSelectedModel);
        if (original != gOriginalTexturesByModel.end() && gSelectedMaterial < original->second.size()) {
            materials[gSelectedMaterial].textureName = original->second[gSelectedMaterial];
            auto& selected = gSelectedTexturesByModel[gSelectedModel];
            if (selected.size() < materials.size()) selected.resize(materials.size());
            selected[gSelectedMaterial] = materials[gSelectedMaterial].textureName;
        }
        setStatus("UV and texture reset for " + materials[gSelectedMaterial].name);
        InvalidateRect(hwnd, &l.right, FALSE);
        return;
    }

    if (hit(p.textureList, x, y)) {
        const int row = (y - p.textureList.top) / TREE_ROW_H;
        const std::size_t textureIndex = gTextureScroll + static_cast<std::size_t>(row);
        if (textureIndex < gScene->textures().size()) {
            materials[gSelectedMaterial].textureName = gScene->textures()[textureIndex].name;
            auto& selected = gSelectedTexturesByModel[gSelectedModel];
            if (selected.size() < materials.size()) selected.resize(materials.size());
            selected[gSelectedMaterial] = materials[gSelectedMaterial].textureName;
            setStatus("Texture " + materials[gSelectedMaterial].textureName + " applied to " + materials[gSelectedMaterial].name);
            InvalidateRect(hwnd, &l.right, FALSE);
        }
    }
}

void handleRightClick(HWND hwnd, int x, int y) {
    const auto l = makeLayout(hwnd);
    const int tabWidth = (width(l.right) - 20) / 3;
    for (int i = 0; i < 3; ++i) {
        RECT tab{l.right.left + 10 + i * tabWidth, l.right.top + 8,
                 l.right.left + 10 + (i + 1) * tabWidth, l.right.top + 40};
        if (!hit(tab, x, y)) continue;
        if (i == 0 || (gScene && (i == 1 ? !gScene->animations().empty() : !gScene->textures().empty()))) {
            gInspectorTab = i;
        }
        InvalidateRect(hwnd, &l.right, FALSE);
        return;
    }
    if (!gScene) return;
    if (gInspectorTab == 1) {
        const int top = l.right.top + 52;
        if (y < top) return;
        const std::size_t index = gAnimationScroll + static_cast<std::size_t>((y - top) / 30);
        if (index == 0) {
            gCurrent = nullptr;
            gFrame = 0.0f;
        } else if (index - 1 < gScene->animations().size()) {
            play(&gScene->animations()[index - 1]);
        }
        InvalidateRect(hwnd, &l.right, FALSE);
    } else if (gInspectorTab == 2) {
        handleTexturesUvClick(hwnd, x, y);
    }
}

void handleClick(HWND hwnd, int x, int y) {
    const auto l = makeLayout(hwnd);
    if (hit(l.openButton, x, y)) {
        const auto path = chooseRom(hwnd);
        if (!path.empty()) loadRomInput(path);
        return;
    }
    if (hit(l.exportButton, x, y) && gSelectedItem < gItems.size()) {
        exportSelected();
        return;
    }
    if (hit(l.dumpButton, x, y) && !gItems.empty()) {
        dumpAll();
        return;
    }

    const int chipGap = 4;
    const int chipWidth = (width(l.filters) - chipGap * 6) / 7;
    for (int i = 0; i < 7; ++i) {
        RECT chip{l.filters.left + i * (chipWidth + chipGap), l.filters.top,
                  l.filters.left + i * (chipWidth + chipGap) + chipWidth, l.filters.bottom};
        if (hit(chip, x, y)) {
            gCategory = i;
            gTreeScroll = 0;
            applyFilter();
            return;
        }
    }

    if (hit(l.tree, x, y)) {
        const std::size_t rowIndex = gTreeScroll + static_cast<std::size_t>((y - l.tree.top) / TREE_ROW_H);
        if (rowIndex < gTreeRows.size()) {
            const auto row = gTreeRows[rowIndex];
            if (row.folder) {
                if (gCollapsedFolders.count(row.key)) gCollapsedFolders.erase(row.key);
                else gCollapsedFolders.insert(row.key);
                rebuildTree();
                InvalidateRect(hwnd, &l.left, FALSE);
            } else {
                activateItem(row.itemIndex);
            }
        }
        return;
    }
    if (hit(l.right, x, y)) handleRightClick(hwnd, x, y);
}

LRESULT CALLBACK StudioProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            ensureStudioResources();
            applyDarkTitleBar(hwnd);
            gWindow = hwnd;
            gSearch = CreateWindowExA(0, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                      0, 0, 10, 10, hwnd, controlId(SEARCH_ID), GetModuleHandleA(nullptr), nullptr);
            if (gSearch) {
                SendMessage(gSearch, WM_SETFONT, reinterpret_cast<WPARAM>(gFontSmall), TRUE);
                SendMessageA(gSearch, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
            }
            gRenderWindow = CreateWindowExA(0, "SmokeDetectorRenderModern", "",
                                            WS_CHILD | WS_CLIPSIBLINGS,
                                            0, 0, 1, 1, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            if (!gRenderWindow) return -1;
            DragAcceptFiles(hwnd, TRUE);
            applyFilter();
            layoutControls(hwnd);
            return 0;
        case WM_SIZE:
            layoutControls(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = {MIN_W, MIN_H};
            return 0;
        case WM_PAINT:
            paintWindow(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, FIELD);
            SetTextColor(dc, TEXT);
            return reinterpret_cast<LRESULT>(gEditBrush);
        }
        case WM_COMMAND:
            if (LOWORD(wp) == SEARCH_ID && HIWORD(wp) == EN_CHANGE) {
                gTreeScroll = 0;
                applyFilter();
                return 0;
            }
            break;
        case WM_LBUTTONDOWN:
            if (!gDumping) {
                try {
                    handleClick(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
                } catch (const std::exception& e) {
                    MessageBoxA(hwnd, e.what(), "Smoke Detector", MB_OK | MB_ICONERROR);
                }
            }
            return 0;
        case WM_MOUSEWHEEL: {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &point);
            const auto l = makeLayout(hwnd);
            const int delta = GET_WHEEL_DELTA_WPARAM(wp);
            if (hit(l.tree, point.x, point.y)) {
                const int visible = std::max(1, height(l.tree) / TREE_ROW_H);
                const std::size_t maxScroll = gTreeRows.size() > static_cast<std::size_t>(visible)
                    ? gTreeRows.size() - visible : 0;
                if (delta < 0) gTreeScroll = std::min(maxScroll, gTreeScroll + 3);
                else gTreeScroll = gTreeScroll > 3 ? gTreeScroll - 3 : 0;
                InvalidateRect(hwnd, &l.left, FALSE);
                return 0;
            }
            if (hit(l.right, point.x, point.y) && gScene) {
                std::size_t* scroll = gInspectorTab == 1 ? &gAnimationScroll
                    : (gInspectorTab == 2 ? &gTextureScroll : nullptr);
                if (scroll) {
                    if (delta < 0) *scroll += 2;
                    else *scroll = *scroll > 2 ? *scroll - 2 : 0;
                    InvalidateRect(hwnd, &l.right, FALSE);
                    return 0;
                }
            }
            break;
        }
        case WM_DROPFILES: {
            const HDROP drop = reinterpret_cast<HDROP>(wp);
            wchar_t path[32768]{};
            if (DragQueryFileW(drop, 0, path, static_cast<UINT>(std::size(path))) > 0) {
                loadRomInput(std::filesystem::path(path));
            }
            DragFinish(drop);
            return 0;
        }
        case WM_CLOSE:
            if (!gDumping) DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            shutdownOpenGL();
            releaseStudioResources();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    try {
        ensureStudioResources();
        initializeArchives();

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
        wc.lpfnWndProc = StudioProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = gBrushBg;
        wc.lpszClassName = "SmokeDetectorStudio3";
        if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            throw std::runtime_error("Could not register main window");
        }

        gWindow = CreateWindowExA(0, wc.lpszClassName, "Smoke Detector",
                                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 1400, 840,
                                  nullptr, nullptr, instance, nullptr);
        if (!gWindow || !gRenderWindow) throw std::runtime_error("Could not create Smoke Detector window");

        initOpenGL(gRenderWindow);
        if (gScene) uploadGlTextures();
        ShowWindow(gRenderWindow, gPreview3D ? SW_SHOW : SW_HIDE);
        layoutControls(gWindow);
        ShowWindow(gWindow, show);
        UpdateWindow(gWindow);
        gLastTick = std::chrono::steady_clock::now();

        MSG msg{};
        bool running = true;
        while (running) {
            while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    running = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            if (running) tickCorrected();
            Sleep(1);
        }
        return 0;
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "Smoke Detector error", MB_OK | MB_ICONERROR);
        releaseStudioResources();
        return 1;
    }
}
