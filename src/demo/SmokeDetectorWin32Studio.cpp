#include <windows.h>
#include <windowsx.h>
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
#include <unordered_set>
#include <vector>

#define WinMain SmokeDetectorModernLegacyWinMain
#include "SmokeDetectorWin32Modern.cpp"
#undef WinMain

namespace {

using promptemon::rom::ResourceKind;

constexpr int ST_SEARCH_ID = 7101;
constexpr int ST_TOP = 50;
constexpr int ST_BOTTOM = 26;
constexpr int ST_GAP = 10;
constexpr int ST_LEFT = 326;
constexpr int ST_RIGHT = 344;
constexpr int ST_ROW = 30;
constexpr int ST_MIN_W = 1120;
constexpr int ST_MIN_H = 700;

constexpr COLORREF ST_BG = RGB(47, 47, 47);
constexpr COLORREF ST_PANEL = RGB(38, 38, 38);
constexpr COLORREF ST_PANEL_2 = RGB(44, 44, 44);
constexpr COLORREF ST_PANEL_3 = RGB(52, 52, 52);
constexpr COLORREF ST_BORDER = RGB(63, 63, 63);
constexpr COLORREF ST_TEXT = RGB(222, 222, 222);
constexpr COLORREF ST_MUTED = RGB(150, 150, 150);
constexpr COLORREF ST_ACCENT = RGB(83, 124, 184);
constexpr COLORREF ST_ACCENT_SOFT = RGB(55, 72, 96);

struct StudioItem {
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

struct StudioLayout {
    RECT client{};
    RECT top{};
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

HWND gStudioSearch = nullptr;
HBRUSH gStudioEditBrush = nullptr;
HFONT gStudioTitle = nullptr;
HFONT gStudioSection = nullptr;
HFONT gStudioTiny = nullptr;

std::vector<StudioItem> gStudioItems;
std::vector<std::size_t> gStudioFiltered;
std::vector<TreeRow> gTreeRows;
std::unordered_set<std::string> gCollapsed;
std::size_t gStudioSelected = static_cast<std::size_t>(-1);
std::size_t gTreeScroll = 0;
std::size_t gAnimScroll = 0;
std::size_t gTexScroll = 0;
std::size_t gMaterialIndex = 0;
int gStudioCategory = 0;
int gPropertyTab = 0; // 0 properties, 1 animation, 2 textures
bool gStudioPreview3D = false;

std::string stLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool stEnds(const std::string& value, const char* suffix) {
    const std::string a = stLower(value);
    const std::string b = stLower(suffix);
    return a.size() >= b.size() && a.compare(a.size() - b.size(), b.size(), b) == 0;
}

int stCategory(const std::string& id, ResourceKind kind) {
    const std::string p = stLower(id);
    if ((p.find("anim") != std::string::npos || p.find("motion") != std::string::npos) &&
        (p.find(".bch") != std::string::npos || p.find(".bcres") != std::string::npos)) return 2;
    if (kind == ResourceKind::Model || stEnds(p, ".bch") || stEnds(p, ".bch.cx") ||
        stEnds(p, ".bcmdl") || stEnds(p, ".bcres")) return 1;
    if (kind == ResourceKind::Texture || kind == ResourceKind::Graphics || kind == ResourceKind::Palette ||
        stEnds(p, ".bclim") || stEnds(p, ".ctpk") || stEnds(p, ".png") || stEnds(p, ".tga") || stEnds(p, ".bmp")) return 3;
    if (kind == ResourceKind::Audio || stEnds(p, ".bcwav") || stEnds(p, ".bcstm") || stEnds(p, ".bcsar") ||
        stEnds(p, ".bcseq") || stEnds(p, ".cwav") || stEnds(p, ".cstm") || stEnds(p, ".wav") || stEnds(p, ".ogg")) return 4;
    if (kind == ResourceKind::Archive || stEnds(p, ".garc") || stEnds(p, ".narc") || stEnds(p, ".darc") ||
        stEnds(p, ".arc") || stEnds(p, ".carc") || stEnds(p, ".szs") || stEnds(p, ".cx")) return 5;
    return 6;
}

const char* stBadge(int category) {
    switch (category) {
        case 1: return "3D";
        case 2: return "AN";
        case 3: return "TX";
        case 4: return "SFX";
        case 5: return "ARC";
        default: return "DAT";
    }
}

const char* stCategoryName(int category) {
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

std::string stSourceFilename(const std::string& label) {
    const auto marker = label.find("] ");
    return marker == std::string::npos ? label : label.substr(marker + 2);
}

std::string stHumanBytes(std::uint64_t size) {
    std::ostringstream out;
    if (size >= 1024ull * 1024ull * 1024ull) out << (size / (1024ull * 1024ull * 1024ull)) << " GB";
    else if (size >= 1024ull * 1024ull) out << (size / (1024ull * 1024ull)) << " MB";
    else if (size >= 1024ull) out << (size / 1024ull) << " KB";
    else out << size << " B";
    return out.str();
}

int stW(const RECT& r) { return static_cast<int>(r.right - r.left); }
int stH(const RECT& r) { return static_cast<int>(r.bottom - r.top); }
bool stIn(const RECT& r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

StudioLayout stLayout(HWND hwnd) {
    StudioLayout l{};
    GetClientRect(hwnd, &l.client);
    const int w = stW(l.client);
    const int h = stH(l.client);
    l.top = {0, 0, w, ST_TOP};
    l.status = {0, h - ST_BOTTOM, w, h};
    const int contentTop = ST_TOP + ST_GAP;
    const int contentBottom = h - ST_BOTTOM - ST_GAP;
    l.left = {ST_GAP, contentTop, ST_GAP + ST_LEFT, contentBottom};
    l.right = {w - ST_GAP - ST_RIGHT, contentTop, w - ST_GAP, contentBottom};
    l.preview = {l.left.right + ST_GAP, contentTop, l.right.left - ST_GAP, contentBottom};
    l.search = {l.left.left + 12, l.left.top + 47, l.left.right - 12, l.left.top + 79};
    l.filters = {l.left.left + 12, l.left.top + 87, l.left.right - 12, l.left.top + 117};
    l.tree = {l.left.left + 8, l.left.top + 126, l.left.right - 8, l.left.bottom - 8};
    l.dumpButton = {w - 114, 9, w - 10, 41};
    l.exportButton = {w - 222, 9, w - 122, 41};
    l.openButton = {w - 330, 9, w - 230, 41};
    return l;
}

void stEnsureUi() {
    ensureUiResources();
    if (!gStudioEditBrush) gStudioEditBrush = CreateSolidBrush(ST_PANEL_3);
    if (!gStudioTitle) gStudioTitle = createUiFont(18, FW_SEMIBOLD);
    if (!gStudioSection) gStudioSection = createUiFont(13, FW_SEMIBOLD);
    if (!gStudioTiny) gStudioTiny = createUiFont(11, FW_NORMAL);
}

void stReleaseUi() {
    if (gStudioEditBrush) DeleteObject(gStudioEditBrush);
    if (gStudioTitle) DeleteObject(gStudioTitle);
    if (gStudioSection) DeleteObject(gStudioSection);
    if (gStudioTiny) DeleteObject(gStudioTiny);
    gStudioEditBrush = nullptr;
    gStudioTitle = gStudioSection = gStudioTiny = nullptr;
    releaseUiResources();
}

void stFill(HDC dc, const RECT& r, COLORREF color) {
    HBRUSH b = CreateSolidBrush(color);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

void stBox(HDC dc, const RECT& r, COLORREF fill, COLORREF border = ST_BORDER, int radius = 5) {
    HBRUSH b = CreateSolidBrush(fill);
    HPEN p = CreatePen(PS_SOLID, 1, border);
    const auto ob = SelectObject(dc, b);
    const auto op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(b); DeleteObject(p);
}

void stText(HDC dc, const std::string& text, RECT r, HFONT font, COLORREF color,
            UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    SelectObject(dc, font);
    DrawTextA(dc, text.c_str(), -1, &r, flags);
}

void stButton(HDC dc, const RECT& r, const char* label, bool accent, bool enabled) {
    stBox(dc, r, enabled ? (accent ? ST_ACCENT : ST_PANEL_3) : ST_PANEL_2,
          enabled ? (accent ? ST_ACCENT : ST_BORDER) : ST_PANEL_2, 5);
    stText(dc, label, r, gFontSmall, enabled ? ST_TEXT : RGB(92,92,92),
           DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

std::string stSearchText() {
    char value[1024]{};
    if (gStudioSearch) GetWindowTextA(gStudioSearch, value, static_cast<int>(sizeof(value)));
    return stLower(value);
}

void stBuildItems() {
    gStudioItems.clear();
    if (!gResourceArchive) return;
    for (const auto& source : gResourceArchive->sourceSummaries()) {
        for (const auto& resource : gResourceArchive->resourcesForSource(source.label)) {
            StudioItem item;
            item.sourceLabel = source.label;
            item.virtualPath = source.label + " :: " + resource.id;
            item.id = resource.id;
            item.displayName = resource.displayName;
            item.romPath = source.rom.path;
            item.kind = resource.kind;
            item.size = resource.size;
            item.category = stCategory(item.id, item.kind);
            gStudioItems.push_back(std::move(item));
        }
    }
}

std::vector<std::string> stSegments(std::string path) {
    if (path.rfind("romfs/", 0) == 0) path.erase(0, 6);
    if (path.rfind("nitrofs/", 0) == 0) path.erase(0, 8);
    std::replace(path.begin(), path.end(), '\\', '/');
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto slash = path.find('/', start);
        const auto part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!part.empty()) parts.push_back(part);
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return parts;
}

void stBuildTree() {
    gTreeRows.clear();
    std::unordered_set<std::string> emitted;
    for (const std::size_t itemIndex : gStudioFiltered) {
        const auto parts = stSegments(gStudioItems[itemIndex].id);
        if (parts.empty()) continue;
        std::string current;
        bool hidden = false;
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
            if (!current.empty()) current += '/';
            current += parts[i];
            if (hidden) continue;
            if (emitted.insert(current).second) gTreeRows.push_back({true, current, parts[i], static_cast<int>(i), static_cast<std::size_t>(-1)});
            if (gCollapsed.find(current) != gCollapsed.end()) hidden = true;
        }
        if (!hidden) gTreeRows.push_back({false, gStudioItems[itemIndex].id, parts.back(), static_cast<int>(parts.size() - 1), itemIndex});
    }
    const std::size_t maxScroll = gTreeRows.empty() ? 0 : gTreeRows.size() - 1;
    gTreeScroll = std::min(gTreeScroll, maxScroll);
}

void stFilter() {
    const std::string query = stSearchText();
    gStudioFiltered.clear();
    for (std::size_t i = 0; i < gStudioItems.size(); ++i) {
        const auto& item = gStudioItems[i];
        if (gStudioCategory && item.category != gStudioCategory) continue;
        if (!query.empty()) {
            const std::string text = stLower(item.id + " " + item.displayName + " " + item.sourceLabel);
            if (text.find(query) == std::string::npos) continue;
        }
        gStudioFiltered.push_back(i);
    }
    std::sort(gStudioFiltered.begin(), gStudioFiltered.end(), [](std::size_t a, std::size_t b) {
        return stLower(gStudioItems[a].id) < stLower(gStudioItems[b].id);
    });
    if (gStudioSelected != static_cast<std::size_t>(-1) &&
        std::find(gStudioFiltered.begin(), gStudioFiltered.end(), gStudioSelected) == gStudioFiltered.end()) {
        gStudioSelected = static_cast<std::size_t>(-1);
    }
    if (gStudioSelected == static_cast<std::size_t>(-1) && !gStudioFiltered.empty()) gStudioSelected = gStudioFiltered.front();
    stBuildTree();
    if (gWindow) InvalidateRect(gWindow, nullptr, FALSE);
}

std::string stModelPath(const StudioItem& item) {
    std::string resource = item.id;
    if (resource.rfind("romfs/", 0) == 0) resource.erase(0, 6);
    if (resource.rfind("nitrofs/", 0) == 0) resource.erase(0, 8);
    const std::string filename = stSourceFilename(item.sourceLabel);
    for (const auto& model : gModels) {
        const auto split = model.find(" :: ");
        if (split == std::string::npos) continue;
        if (stSourceFilename(model.substr(0, split)) == filename && model.substr(split + 4) == resource) return model;
    }
    return {};
}

void stClearModel() {
    if (gGL) destroyGlTextures();
    gScene.reset();
    gSelectedModel.clear();
    gIdle = gCurrent = nullptr;
    gFrame = gYaw = gPitch = 0.0f;
    gZoom = 1.0f;
    gStudioPreview3D = false;
    gAnimScroll = gTexScroll = 0;
    gMaterialIndex = 0;
}

void stActivate(std::size_t itemIndex) {
    if (itemIndex >= gStudioItems.size()) return;
    gStudioSelected = itemIndex;
    const auto& item = gStudioItems[itemIndex];
    const std::string model = item.category == 1 ? stModelPath(item) : std::string{};
    if (!model.empty()) {
        try {
            loadModel(model);
            gStudioPreview3D = true;
            gPropertyTab = 0;
            ShowWindow(gRenderWindow, SW_SHOW);
        } catch (const std::exception& e) {
            gStudioPreview3D = false;
            ShowWindow(gRenderWindow, SW_HIDE);
            setStatus(std::string("3D resource indexed, preview unavailable: ") + e.what());
        }
    } else {
        gStudioPreview3D = false;
        ShowWindow(gRenderWindow, SW_HIDE);
        setStatus(item.id);
    }
    InvalidateRect(gWindow, nullptr, FALSE);
}

std::filesystem::path stChooseRom(HWND owner) {
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

std::filesystem::path stCommandLineRom() {
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

bool stLoadRom(const std::filesystem::path& path, bool report = true) {
    try {
        auto resources = std::make_unique<RomResourceArchive>();
        if (!resources->open(path)) throw std::runtime_error("The ROM could not be indexed.");
        auto models = std::make_unique<RomModelArchive>();
        models->open(path);
        auto modelPaths = models->modelResources();

        stClearModel();
        gResourceArchive = std::move(resources);
        gArchive = std::move(models);
        gModels = std::move(modelPaths);
        gRomDirectory = path;
        gExportRoot = path.parent_path() / "smoke-detector-exports";
        stBuildItems();
        gStudioCategory = 0;
        gTreeScroll = 0;
        if (gStudioSearch) SetWindowTextA(gStudioSearch, "");
        stFilter();

        for (const auto& model : gModels) {
            try {
                loadModel(model);
                gStudioPreview3D = true;
                break;
            } catch (...) {}
        }
        if (!gStudioPreview3D && !gStudioItems.empty()) gStudioSelected = gStudioFiltered.empty() ? 0 : gStudioFiltered.front();

        const auto summaries = gResourceArchive->sourceSummaries();
        std::string provider = summaries.empty() ? std::string{} : summaries.front().provider;
        if (provider.find("Encrypted 3DS RomFS") != std::string::npos || provider.find("key") != std::string::npos) {
            setStatus(provider);
        } else {
            setStatus("Loaded " + path.filename().string() + " | " + std::to_string(gStudioItems.size()) + " resources");
        }
        if (gWindow) {
            SetWindowTextA(gWindow, ("Smoke Detector - " + path.filename().string()).c_str());
            ShowWindow(gRenderWindow, gStudioPreview3D ? SW_SHOW : SW_HIDE);
            InvalidateRect(gWindow, nullptr, FALSE);
        }
        return true;
    } catch (const std::exception& e) {
        if (report && gWindow) MessageBoxA(gWindow, e.what(), "Smoke Detector", MB_OK | MB_ICONERROR);
        return false;
    }
}

void stInitArchives() {
    const auto cli = stCommandLineRom();
    if (!cli.empty() && stLoadRom(cli, false)) return;
    gArchive = std::make_unique<RomModelArchive>();
    gResourceArchive = std::make_unique<RomResourceArchive>();
    gModels.clear();
    gStudioItems.clear();
    gStudioFiltered.clear();
    gTreeRows.clear();
    gExportRoot = executableDirectory() / "exports";
    setStatus("Open or drag a ROM to begin.");
}

std::string stSafe(std::string value) {
    for (char& c : value) if (c=='/' || c=='\\' || c==':' || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|') c='_';
    return value.empty() ? "resource" : value;
}

std::string stLeaf(const std::string& value) {
    const auto pos = value.find_last_of("/\\");
    return stSafe(pos == std::string::npos ? value : value.substr(pos + 1));
}

void stExportSelected() {
    if (gStudioSelected >= gStudioItems.size()) return;
    const auto& item = gStudioItems[gStudioSelected];
    const std::string model = item.category == 1 ? stModelPath(item) : std::string{};
    if (!model.empty() && gArchive) {
        const auto result = AssetExporter::exportModel(*gArchive, model, gExportRoot);
        setStatus("Exported 3D: " + result.interchangeModel.string());
        ShellExecuteA(gWindow, "open", result.interchangeModel.parent_path().string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    const auto outDir = gExportRoot / "selected" / stSafe(stSourceFilename(item.sourceLabel));
    std::filesystem::create_directories(outDir);
    const auto outPath = outDir / stLeaf(item.id);
    if (item.kind == ResourceKind::RawRom) {
        std::error_code ec;
        std::filesystem::copy_file(item.romPath, outPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) throw std::runtime_error(ec.message());
    } else {
        const auto payload = gResourceArchive->extract(item.virtualPath);
        std::ofstream out(outPath, std::ios::binary);
        if (!out) throw std::runtime_error("Could not create export file");
        if (!payload.bytes.empty()) out.write(reinterpret_cast<const char*>(payload.bytes.data()), static_cast<std::streamsize>(payload.bytes.size()));
    }
    setStatus("Exported: " + outPath.string());
    ShellExecuteA(gWindow, "open", outDir.string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void stDumpAll() {
    if (!gResourceArchive || gDumping) return;
    gDumping = true;
    setStatus("Dumping all resources...");
    setProgress(0, 1, {});
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
        setProgress(0,0,{});
        setStatus("Dump complete: " + std::to_string(count) + " resources");
        ShellExecuteA(gWindow, "open", (gExportRoot / "resources").string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } catch (...) {
        gDumping = false;
        setProgress(0,0,{});
        throw;
    }
}

void stLayoutControls(HWND hwnd) {
    const auto l = stLayout(hwnd);
    if (gStudioSearch) MoveWindow(gStudioSearch, l.search.left + 8, l.search.top + 2,
                                  std::max(10, stW(l.search) - 16), stH(l.search) - 4, TRUE);
    if (gRenderWindow) {
        RECT render{l.preview.left + 1, l.preview.top + 37, l.preview.right - 1, l.preview.bottom - 1};
        MoveWindow(gRenderWindow, render.left, render.top, std::max(1, stW(render)), std::max(1, stH(render)), TRUE);
        ShowWindow(gRenderWindow, gStudioPreview3D ? SW_SHOW : SW_HIDE);
    }
}

void stPaintTop(HDC dc, const StudioLayout& l) {
    stFill(dc, l.top, ST_PANEL);
    RECT title{16, 0, 280, ST_TOP};
    stText(dc, "Smoke Detector", title, gStudioTitle, ST_TEXT);
    stButton(dc, l.openButton, "Open ROM", false, !gDumping);
    stButton(dc, l.exportButton, "Export", false, !gDumping && gStudioSelected < gStudioItems.size());
    stButton(dc, l.dumpButton, "Dump all", true, !gDumping && !gStudioItems.empty());
}

void stPaintLeft(HDC dc, const StudioLayout& l) {
    stBox(dc, l.left, ST_PANEL, ST_BORDER, 4);
    RECT heading{l.left.left + 12, l.left.top + 8, l.left.right - 12, l.left.top + 38};
    stText(dc, "Resources", heading, gStudioSection, ST_TEXT);
    RECT count{l.left.right - 80, heading.top, l.left.right - 12, heading.bottom};
    stText(dc, std::to_string(gStudioFiltered.size()), count, gStudioTiny, ST_MUTED, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    stBox(dc, l.search, ST_PANEL_3, ST_BORDER, 4);

    const int gap = 4;
    const int countFilters = 7;
    const int width = (stW(l.filters) - gap * (countFilters - 1)) / countFilters;
    for (int i = 0; i < countFilters; ++i) {
        RECT r{l.filters.left + i * (width + gap), l.filters.top,
               l.filters.left + i * (width + gap) + width, l.filters.bottom};
        const bool active = gStudioCategory == i;
        stBox(dc, r, active ? ST_ACCENT_SOFT : ST_PANEL_2, active ? ST_ACCENT : ST_BORDER, 4);
        stText(dc, stCategoryName(i), r, gStudioTiny, active ? ST_TEXT : ST_MUTED,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    HRGN clip = CreateRectRgn(l.tree.left, l.tree.top, l.tree.right, l.tree.bottom);
    SelectClipRgn(dc, clip);
    const int visible = std::max(1, stH(l.tree) / ST_ROW);
    const std::size_t maxScroll = gTreeRows.size() > static_cast<std::size_t>(visible) ? gTreeRows.size() - visible : 0;
    gTreeScroll = std::min(gTreeScroll, maxScroll);
    for (int i = 0; i <= visible; ++i) {
        const std::size_t rowIndex = gTreeScroll + static_cast<std::size_t>(i);
        if (rowIndex >= gTreeRows.size()) break;
        const auto& row = gTreeRows[rowIndex];
        RECT rr{l.tree.left, l.tree.top + i * ST_ROW, l.tree.right, l.tree.top + (i + 1) * ST_ROW - 1};
        const bool selected = !row.folder && row.itemIndex == gStudioSelected;
        if (selected) stFill(dc, rr, ST_ACCENT_SOFT);
        const int indent = std::min(76, row.depth * 14);
        RECT icon{rr.left + 7 + indent, rr.top, rr.left + 27 + indent, rr.bottom};
        const std::string glyph = row.folder ? (gCollapsed.count(row.key) ? ">" : "v") : stBadge(gStudioItems[row.itemIndex].category);
        stText(dc, glyph, icon, gStudioTiny, row.folder ? ST_MUTED : ST_ACCENT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT text{icon.right + 2, rr.top, rr.right - 8, rr.bottom};
        stText(dc, row.label, text, gFontSmall, selected ? ST_TEXT : (row.folder ? RGB(190,190,190) : ST_TEXT));
    }
    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
}

void stPaintPreview(HDC dc, const StudioLayout& l) {
    stBox(dc, l.preview, ST_PANEL, ST_BORDER, 4);
    RECT head{l.preview.left + 12, l.preview.top, l.preview.right - 12, l.preview.top + 36};
    stText(dc, "Preview", head, gStudioSection, ST_TEXT);
    if (gStudioPreview3D && gScene) {
        RECT meta{l.preview.left + 100, l.preview.top, l.preview.right - 12, l.preview.top + 36};
        stText(dc, gScene->modelName(), meta, gStudioTiny, ST_MUTED, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else if (gStudioSelected < gStudioItems.size()) {
        const auto& item = gStudioItems[gStudioSelected];
        const int cx = (l.preview.left + l.preview.right) / 2;
        const int cy = (l.preview.top + 37 + l.preview.bottom) / 2;
        RECT badge{cx - 34, cy - 48, cx + 34, cy + 20};
        stBox(dc, badge, ST_PANEL_3, ST_BORDER, 6);
        stText(dc, stBadge(item.category), badge, gStudioTitle, ST_ACCENT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT name{l.preview.left + 30, badge.bottom + 12, l.preview.right - 30, badge.bottom + 38};
        stText(dc, item.id, name, gFontBody, ST_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT hint{name.left, name.bottom, name.right, name.bottom + 24};
        stText(dc, "Indexed resource - export available", hint, gStudioTiny, ST_MUTED,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void stSection(HDC dc, const StudioLayout& l, int& y, const char* title) {
    RECT row{l.right.left + 12, y, l.right.right - 12, y + 28};
    stFill(dc, row, ST_PANEL_2);
    RECT arrow{row.left + 7, row.top, row.left + 23, row.bottom};
    stText(dc, "v", arrow, gStudioTiny, ST_MUTED, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT text{arrow.right + 2, row.top, row.right - 8, row.bottom};
    stText(dc, title, text, gStudioSection, ST_TEXT);
    y += 34;
}

void stPropertyLine(HDC dc, const StudioLayout& l, int& y, const std::string& key, const std::string& value) {
    RECT k{l.right.left + 18, y, l.right.left + 112, y + 25};
    RECT v{k.right, y, l.right.right - 18, y + 25};
    stText(dc, key, k, gStudioTiny, ST_MUTED);
    stText(dc, value, v, gStudioTiny, ST_TEXT);
    y += 26;
}

void stPaintProperties(HDC dc, const StudioLayout& l) {
    int y = l.right.top + 52;
    if (gStudioSelected >= gStudioItems.size()) return;
    const auto& item = gStudioItems[gStudioSelected];
    stSection(dc, l, y, "Resource");
    stPropertyLine(dc, l, y, "Name", item.displayName.empty() ? item.id : item.displayName);
    stPropertyLine(dc, l, y, "Type", stCategoryName(item.category));
    stPropertyLine(dc, l, y, "Size", item.size ? stHumanBytes(item.size) : "unknown");
    y += 5;
    if (gStudioPreview3D && gScene) {
        stSection(dc, l, y, "Geometry");
        stPropertyLine(dc, l, y, "Meshes", std::to_string(gScene->meshes().size()));
        stPropertyLine(dc, l, y, "Bones", std::to_string(gScene->bones().size()));
        stPropertyLine(dc, l, y, "Materials", std::to_string(gScene->materials().size()));
        stPropertyLine(dc, l, y, "Textures", std::to_string(gScene->textures().size()));
        stPropertyLine(dc, l, y, "Animations", std::to_string(gScene->animations().size()));
        y += 5;
    }
    stSection(dc, l, y, "Source");
    stPropertyLine(dc, l, y, "ROM", stSourceFilename(item.sourceLabel));
    stPropertyLine(dc, l, y, "Path", item.id);
}

void stPaintAnimations(HDC dc, const StudioLayout& l) {
    if (!gScene) return;
    const int top = l.right.top + 52;
    const int rowH = 30;
    const int visible = std::max(1, (l.right.bottom - top - 12) / rowH);
    const std::size_t total = gScene->animations().size() + 1;
    const std::size_t maxScroll = total > static_cast<std::size_t>(visible) ? total - visible : 0;
    gAnimScroll = std::min(gAnimScroll, maxScroll);
    for (int i = 0; i < visible; ++i) {
        const std::size_t idx = gAnimScroll + static_cast<std::size_t>(i);
        if (idx >= total) break;
        RECT row{l.right.left + 10, top + i * rowH, l.right.right - 10, top + (i + 1) * rowH - 2};
        bool active = false;
        std::string text;
        if (idx == 0) { text = "Base pose"; active = gCurrent == nullptr; }
        else {
            const auto& animation = gScene->animations()[idx - 1];
            text = animation.name + "   " + std::to_string(static_cast<int>(animation.frames)) + "f";
            active = gCurrent == &animation;
        }
        if (active) stFill(dc, row, ST_ACCENT_SOFT);
        RECT t{row.left + 8, row.top, row.right - 8, row.bottom};
        stText(dc, text, t, gStudioTiny, active ? ST_TEXT : RGB(195,195,195));
    }
}

void stPaintTextures(HDC dc, const StudioLayout& l) {
    if (!gScene) return;
    int y = l.right.top + 52;
    stSection(dc, l, y, "Material");
    const auto& materials = gScene->materials();
    const auto& textures = gScene->textures();
    if (!materials.empty()) gMaterialIndex = std::min(gMaterialIndex, materials.size() - 1);
    if (!materials.empty()) {
        RECT current{l.right.left + 18, y, l.right.right - 18, y + 30};
        stBox(dc, current, ST_PANEL_3, ST_BORDER, 4);
        stText(dc, materials[gMaterialIndex].name.empty() ? ("Material " + std::to_string(gMaterialIndex)) : materials[gMaterialIndex].name,
               current, gStudioTiny, ST_TEXT);
        y += 40;
    }
    stSection(dc, l, y, "Textures");
    const int rowH = 31;
    const int visible = std::max(1, (l.right.bottom - y - 10) / rowH);
    const std::size_t maxScroll = textures.size() > static_cast<std::size_t>(visible) ? textures.size() - visible : 0;
    gTexScroll = std::min(gTexScroll, maxScroll);
    for (int i = 0; i < visible; ++i) {
        const std::size_t idx = gTexScroll + static_cast<std::size_t>(i);
        if (idx >= textures.size()) break;
        const bool active = !materials.empty() && materials[gMaterialIndex].textureName == textures[idx].name;
        RECT row{l.right.left + 10, y + i * rowH, l.right.right - 10, y + (i + 1) * rowH - 2};
        if (active) stFill(dc, row, ST_ACCENT_SOFT);
        RECT name{row.left + 8, row.top, row.right - 82, row.bottom};
        RECT dims{row.right - 78, row.top, row.right - 8, row.bottom};
        stText(dc, textures[idx].name, name, gStudioTiny, active ? ST_TEXT : RGB(195,195,195));
        stText(dc, std::to_string(textures[idx].width) + "x" + std::to_string(textures[idx].height), dims,
               gStudioTiny, ST_MUTED, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

void stPaintRight(HDC dc, const StudioLayout& l) {
    stBox(dc, l.right, ST_PANEL, ST_BORDER, 4);
    static const char* tabs[] = {"Properties", "Animation", "Textures"};
    const int tabW = (stW(l.right) - 20) / 3;
    for (int i = 0; i < 3; ++i) {
        RECT tab{l.right.left + 10 + i * tabW, l.right.top + 8,
                 l.right.left + 10 + (i + 1) * tabW, l.right.top + 40};
        const bool enabled = i == 0 || (gScene && (i == 1 ? !gScene->animations().empty() : !gScene->textures().empty()));
        if (gPropertyTab == i) stFill(dc, tab, ST_PANEL_3);
        stText(dc, tabs[i], tab, gStudioTiny, enabled ? (gPropertyTab == i ? ST_TEXT : ST_MUTED) : RGB(82,82,82),
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    if (gPropertyTab == 1) stPaintAnimations(dc, l);
    else if (gPropertyTab == 2) stPaintTextures(dc, l);
    else stPaintProperties(dc, l);
}

void stPaintStatus(HDC dc, const StudioLayout& l) {
    stFill(dc, l.status, ST_PANEL);
    RECT text{12, l.status.top, l.status.right - 12, l.status.bottom};
    stText(dc, gStatus, text, gStudioTiny, ST_MUTED);
    if (gProgressTotal) {
        const double ratio = std::min(1.0, static_cast<double>(gProgressCurrent) / static_cast<double>(std::max<std::size_t>(1, gProgressTotal)));
        RECT fill{0, l.status.bottom - 2, static_cast<LONG>(stW(l.status) * ratio), l.status.bottom};
        stFill(dc, fill, ST_ACCENT);
    }
}

void stPaint(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC target = BeginPaint(hwnd, &ps);
    RECT client{}; GetClientRect(hwnd, &client);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, std::max(1, stW(client)), std::max(1, stH(client)));
    HGDIOBJ old = SelectObject(dc, bmp);
    stFill(dc, client, ST_BG);
    const auto l = stLayout(hwnd);
    stPaintTop(dc, l);
    stPaintLeft(dc, l);
    stPaintPreview(dc, l);
    stPaintRight(dc, l);
    stPaintStatus(dc, l);
    BitBlt(target, 0, 0, stW(client), stH(client), dc, 0, 0, SRCCOPY);
    SelectObject(dc, old); DeleteObject(bmp); DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

void stClickRight(HWND hwnd, int x, int y) {
    const auto l = stLayout(hwnd);
    const int tabW = (stW(l.right) - 20) / 3;
    for (int i = 0; i < 3; ++i) {
        RECT tab{l.right.left + 10 + i * tabW, l.right.top + 8,
                 l.right.left + 10 + (i + 1) * tabW, l.right.top + 40};
        if (!stIn(tab, x, y)) continue;
        if (i == 0 || (gScene && (i == 1 ? !gScene->animations().empty() : !gScene->textures().empty()))) gPropertyTab = i;
        InvalidateRect(hwnd, &l.right, FALSE);
        return;
    }
    if (!gScene) return;
    if (gPropertyTab == 1) {
        const int top = l.right.top + 52;
        if (y < top) return;
        const int row = (y - top) / 30;
        const std::size_t idx = gAnimScroll + static_cast<std::size_t>(row);
        if (idx == 0) { gCurrent = nullptr; gFrame = 0.0f; }
        else if (idx - 1 < gScene->animations().size()) play(&gScene->animations()[idx - 1]);
        InvalidateRect(hwnd, &l.right, FALSE);
    } else if (gPropertyTab == 2) {
        int top = l.right.top + 52 + 34;
        if (!gScene->materials().empty()) top += 40;
        top += 34;
        if (y < top) return;
        const int row = (y - top) / 31;
        const std::size_t idx = gTexScroll + static_cast<std::size_t>(row);
        if (idx < gScene->textures().size() && gMaterialIndex < gScene->materials().size()) {
            gScene->materialsMutable()[gMaterialIndex].textureName = gScene->textures()[idx].name;
            setStatus("Texture " + gScene->textures()[idx].name + " applied");
            InvalidateRect(hwnd, &l.right, FALSE);
        }
    }
}

void stClick(HWND hwnd, int x, int y) {
    const auto l = stLayout(hwnd);
    if (stIn(l.openButton, x, y)) {
        const auto path = stChooseRom(hwnd);
        if (!path.empty()) stLoadRom(path);
        return;
    }
    if (stIn(l.exportButton, x, y) && gStudioSelected < gStudioItems.size()) { stExportSelected(); return; }
    if (stIn(l.dumpButton, x, y) && !gStudioItems.empty()) { stDumpAll(); return; }

    const int gap = 4;
    const int width = (stW(l.filters) - gap * 6) / 7;
    for (int i = 0; i < 7; ++i) {
        RECT r{l.filters.left + i * (width + gap), l.filters.top,
               l.filters.left + i * (width + gap) + width, l.filters.bottom};
        if (stIn(r, x, y)) {
            gStudioCategory = i;
            gTreeScroll = 0;
            stFilter();
            return;
        }
    }

    if (stIn(l.tree, x, y)) {
        const int row = (y - l.tree.top) / ST_ROW;
        const std::size_t idx = gTreeScroll + static_cast<std::size_t>(row);
        if (idx < gTreeRows.size()) {
            const auto selected = gTreeRows[idx];
            if (selected.folder) {
                if (gCollapsed.count(selected.key)) gCollapsed.erase(selected.key);
                else gCollapsed.insert(selected.key);
                stBuildTree();
                InvalidateRect(hwnd, &l.left, FALSE);
            } else {
                stActivate(selected.itemIndex);
            }
        }
        return;
    }
    if (stIn(l.right, x, y)) stClickRight(hwnd, x, y);
}

LRESULT CALLBACK StudioWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            stEnsureUi();
            applyDarkTitleBar(hwnd);
            gWindow = hwnd;
            gStudioSearch = CreateWindowExA(0, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                            0,0,10,10, hwnd, controlId(ST_SEARCH_ID), GetModuleHandleA(nullptr), nullptr);
            SendMessage(gStudioSearch, WM_SETFONT, reinterpret_cast<WPARAM>(gFontSmall), TRUE);
            SendMessageA(gStudioSearch, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8,8));
            gRenderWindow = CreateWindowExA(0, "SmokeDetectorRenderModern", "",
                                            WS_CHILD | WS_CLIPSIBLINGS, 0,0,1,1,
                                            hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            if (!gRenderWindow) return -1;
            DragAcceptFiles(hwnd, TRUE);
            stFilter();
            stLayoutControls(hwnd);
            return 0;
        case WM_SIZE:
            stLayoutControls(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = {ST_MIN_W, ST_MIN_H};
            return 0;
        case WM_PAINT:
            stPaint(hwnd);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, ST_PANEL_3); SetTextColor(dc, ST_TEXT);
            return reinterpret_cast<LRESULT>(gStudioEditBrush);
        }
        case WM_COMMAND:
            if (LOWORD(wp) == ST_SEARCH_ID && HIWORD(wp) == EN_CHANGE) {
                gTreeScroll = 0;
                stFilter();
                return 0;
            }
            break;
        case WM_LBUTTONDOWN:
            if (!gDumping) {
                try { stClick(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); }
                catch (const std::exception& e) { MessageBoxA(hwnd, e.what(), "Smoke Detector", MB_OK | MB_ICONERROR); }
            }
            return 0;
        case WM_MOUSEWHEEL: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            const auto l = stLayout(hwnd);
            const int delta = GET_WHEEL_DELTA_WPARAM(wp);
            if (stIn(l.tree, p.x, p.y)) {
                const int visible = std::max(1, stH(l.tree) / ST_ROW);
                const std::size_t maxScroll = gTreeRows.size() > static_cast<std::size_t>(visible) ? gTreeRows.size() - visible : 0;
                if (delta < 0) gTreeScroll = std::min(maxScroll, gTreeScroll + 3);
                else gTreeScroll = gTreeScroll > 3 ? gTreeScroll - 3 : 0;
                InvalidateRect(hwnd, &l.left, FALSE);
                return 0;
            }
            if (stIn(l.right, p.x, p.y) && gScene) {
                std::size_t* scroll = gPropertyTab == 1 ? &gAnimScroll : (gPropertyTab == 2 ? &gTexScroll : nullptr);
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
            if (DragQueryFileW(drop, 0, path, static_cast<UINT>(std::size(path))) > 0) stLoadRom(std::filesystem::path(path));
            DragFinish(drop);
            return 0;
        }
        case WM_CLOSE:
            if (!gDumping) DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            shutdownOpenGL();
            stReleaseUi();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    try {
        stEnsureUi();
        stInitArchives();

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
        wc.lpfnWndProc = StudioWndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = gBrushBg;
        wc.lpszClassName = "SmokeDetectorStudio";
        if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            throw std::runtime_error("Could not register Smoke Detector studio window");
        }

        gWindow = CreateWindowExA(0, wc.lpszClassName, "Smoke Detector",
                                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 1360, 820,
                                  nullptr, nullptr, instance, nullptr);
        if (!gWindow || !gRenderWindow) throw std::runtime_error("Could not create Smoke Detector window");

        initOpenGL(gRenderWindow);
        if (gScene) uploadGlTextures();
        ShowWindow(gRenderWindow, gStudioPreview3D ? SW_SHOW : SW_HIDE);
        stLayoutControls(gWindow);
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
        stReleaseUi();
        return 1;
    }
}
