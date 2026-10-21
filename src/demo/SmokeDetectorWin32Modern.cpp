#include "bch/BchScene.h"
#include "export/AssetExporter.h"
#include "export/ResourcePackageExporter.h"
#include "rom/RomModelArchive.h"
#include "rom/RomResourceArchive.h"

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <GL/gl.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
using promptemon::bch::Animation;
using promptemon::bch::Material;
using promptemon::bch::Scene;
using promptemon::bch::Vec2;
using promptemon::bch::Vec3;
using promptemon::rom::RomInspector;
using promptemon::rom::RomModelArchive;
using promptemon::rom::RomResourceArchive;
using smoke_detector::exporter::AssetExporter;
using smoke_detector::exporter::ResourcePackageExporter;

constexpr int ID_MODELS = 1001;
constexpr int ID_ROMS = 1002;
constexpr int ID_EXPORT_MODEL = 1003;
constexpr int ID_IDLE = 1004;
constexpr int ID_ACTION1 = 1005;
constexpr int ID_ACTION2 = 1006;
constexpr int ID_DUMP_ALL = 1007;
constexpr int ID_PICKER_SEARCH = 2001;
constexpr int ID_PICKER_LIST = 2002;
constexpr int ID_PICKER_LOAD = 2003;
constexpr int ID_PICKER_CANCEL = 2004;
constexpr int ID_PICKER_COUNT = 2005;

constexpr int HEADER_HEIGHT = 106;
constexpr int FOOTER_HEIGHT = 48;
constexpr int MARGIN = 16;

constexpr COLORREF C_BG = RGB(16, 18, 23);
constexpr COLORREF C_PANEL = RGB(23, 27, 34);
constexpr COLORREF C_PANEL_2 = RGB(29, 34, 43);
constexpr COLORREF C_BORDER = RGB(52, 59, 71);
constexpr COLORREF C_TEXT = RGB(238, 242, 247);
constexpr COLORREF C_MUTED = RGB(154, 164, 178);
constexpr COLORREF C_ACCENT = RGB(77, 144, 254);
constexpr COLORREF C_ACCENT_DOWN = RGB(54, 112, 210);
constexpr COLORREF C_BUTTON = RGB(37, 43, 53);
constexpr COLORREF C_BUTTON_DOWN = RGB(47, 55, 67);

HWND gWindow = nullptr;
HWND gRenderWindow = nullptr;
HDC gDC = nullptr;
HGLRC gGL = nullptr;

std::unique_ptr<RomModelArchive> gArchive;
std::unique_ptr<RomResourceArchive> gResourceArchive;
std::unique_ptr<Scene> gScene;
std::filesystem::path gRomDirectory;
std::filesystem::path gExportRoot;
std::vector<std::string> gModels;
std::string gSelectedModel;

const Animation* gIdle = nullptr;
const Animation* gCurrent = nullptr;
float gFrame = 0.0f;
float gYaw = 0.0f;
float gPitch = 0.0f;
float gZoom = 1.0f;
Vec3 gModelCenter{};
float gFitScale = 1.0f;
bool gDragging = false;
POINT gLastMouse{};
std::chrono::steady_clock::time_point gLastTick;
std::unordered_map<std::string, GLuint> gGlTextures;

HFONT gFontTitle = nullptr;
HFONT gFontBody = nullptr;
HFONT gFontSmall = nullptr;
HBRUSH gBrushBg = nullptr;
HBRUSH gBrushPanel = nullptr;

std::string gStatus = "Listo";
std::string gProgressItem;
std::size_t gProgressCurrent = 0;
std::size_t gProgressTotal = 0;
bool gDumping = false;

HWND gPicker = nullptr;
HWND gPickerSearch = nullptr;
HWND gPickerList = nullptr;
HWND gPickerCount = nullptr;
std::vector<std::string> gFilteredModels;

HMENU controlId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

void applyDarkTitleBar(HWND hwnd) {
    using DwmSetWindowAttributeFn = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    auto setAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(dwm, "DwmSetWindowAttribute"));
    if (setAttribute) {
        BOOL enabled = TRUE;
        setAttribute(hwnd, 20, &enabled, sizeof(enabled));
        setAttribute(hwnd, 19, &enabled, sizeof(enabled));
    }
    FreeLibrary(dwm);
}

HFONT createUiFont(int pixels, int weight) {
    HFONT font = CreateFontW(-pixels, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                            L"Segoe UI Variable Text");
    if (!font) {
        font = CreateFontW(-pixels, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                           L"Segoe UI");
    }
    return font;
}

void ensureUiResources() {
    if (!gFontTitle) gFontTitle = createUiFont(22, FW_SEMIBOLD);
    if (!gFontBody) gFontBody = createUiFont(15, FW_MEDIUM);
    if (!gFontSmall) gFontSmall = createUiFont(13, FW_NORMAL);
    if (!gBrushBg) gBrushBg = CreateSolidBrush(C_BG);
    if (!gBrushPanel) gBrushPanel = CreateSolidBrush(C_PANEL);
}

void releaseUiResources() {
    if (gFontTitle) DeleteObject(gFontTitle);
    if (gFontBody) DeleteObject(gFontBody);
    if (gFontSmall) DeleteObject(gFontSmall);
    if (gBrushBg) DeleteObject(gBrushBg);
    if (gBrushPanel) DeleteObject(gBrushPanel);
    gFontTitle = gFontBody = gFontSmall = nullptr;
    gBrushBg = gBrushPanel = nullptr;
}

void setStatus(const std::string& text) {
    gStatus = text;
    if (gWindow) InvalidateRect(gWindow, nullptr, FALSE);
}

void setProgress(std::size_t current, std::size_t total, std::string item) {
    gProgressCurrent = current;
    gProgressTotal = total;
    gProgressItem = std::move(item);
    if (gWindow) InvalidateRect(gWindow, nullptr, FALSE);
}

void pumpUi() {
    MSG msg{};
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) continue;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    if (gWindow) UpdateWindow(gWindow);
}

void drawFlatButton(const DRAWITEMSTRUCT& item) {
    const int id = static_cast<int>(GetWindowLongPtr(item.hwndItem, GWLP_ID));
    const bool accent = id == ID_DUMP_ALL || id == ID_PICKER_LOAD;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;

    COLORREF fill = accent ? C_ACCENT : C_BUTTON;
    if (pressed) fill = accent ? C_ACCENT_DOWN : C_BUTTON_DOWN;
    if (disabled) fill = RGB(42, 45, 50);

    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, accent ? fill : C_BORDER);
    auto oldBrush = SelectObject(item.hDC, brush);
    auto oldPen = SelectObject(item.hDC, pen);
    RoundRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom, 10, 10);
    SelectObject(item.hDC, oldBrush);
    SelectObject(item.hDC, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);

    wchar_t text[128]{};
    GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, disabled ? RGB(115, 121, 130) : C_TEXT);
    SelectObject(item.hDC, gFontBody);
    RECT textRect = item.rcItem;
    DrawTextW(item.hDC, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

HWND createCommandButton(HWND parent, int id, const char* text, int x, int width) {
    HWND button = CreateWindowA("BUTTON", text,
                                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                x, 57, width, 34,
                                parent, controlId(id),
                                GetModuleHandleA(nullptr), nullptr);
    if (button) SendMessage(button, WM_SETFONT, reinterpret_cast<WPARAM>(gFontBody), TRUE);
    return button;
}

void paintChrome(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc{};
    GetClientRect(hwnd, &rc);

    FillRect(dc, &rc, gBrushBg);

    RECT header{0, 0, rc.right, HEADER_HEIGHT};
    FillRect(dc, &header, gBrushPanel);

    const LONG footerTop = std::max<LONG>(0, rc.bottom - static_cast<LONG>(FOOTER_HEIGHT));
    RECT footer{0, footerTop, rc.right, rc.bottom};
    FillRect(dc, &footer, gBrushPanel);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, C_TEXT);
    SelectObject(dc, gFontTitle);
    RECT title{MARGIN, 12, rc.right - MARGIN, 38};
    DrawTextW(dc, L"Smoke Detector", -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SetTextColor(dc, C_MUTED);
    SelectObject(dc, gFontSmall);
    RECT subtitle{MARGIN, 34, rc.right - MARGIN, 52};
    DrawTextW(dc, L"ROM inspector, model viewer and full resource dumper", -1, &subtitle,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    std::wstring status(gStatus.begin(), gStatus.end());
    RECT statusRect{MARGIN, rc.bottom - FOOTER_HEIGHT + 8, rc.right - MARGIN, rc.bottom - 6};
    SetTextColor(dc, C_TEXT);
    DrawTextW(dc, status.c_str(), -1, &statusRect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (gProgressTotal > 0) {
        const int barLeft = MARGIN;
        const int barRight = std::max(barLeft + 1, static_cast<int>(rc.right) - MARGIN);
        const int barTop = static_cast<int>(rc.bottom) - 8;
        const int barBottom = static_cast<int>(rc.bottom) - 4;
        RECT track{barLeft, barTop, barRight, barBottom};
        HBRUSH trackBrush = CreateSolidBrush(RGB(47, 53, 63));
        FillRect(dc, &track, trackBrush);
        DeleteObject(trackBrush);

        const double ratio = std::min(1.0, static_cast<double>(gProgressCurrent) /
                                           static_cast<double>(std::max<std::size_t>(1, gProgressTotal)));
        RECT fill{barLeft, barTop, barLeft + static_cast<int>((barRight - barLeft) * ratio), barBottom};
        HBRUSH fillBrush = CreateSolidBrush(C_ACCENT);
        FillRect(dc, &fill, fillBrush);
        DeleteObject(fillBrush);
    }

    EndPaint(hwnd, &ps);
}

Vec2 materialUv(const Material* material, Vec2 uv) {
    if (!material) return uv;
    const float cosine = std::cos(material->uvRotation);
    const float sine = std::sin(material->uvRotation);
    const float u = uv.x - 0.5f;
    const float v = uv.y - 0.5f;
    const float ru = u * cosine - v * sine + 0.5f;
    const float rv = u * sine + v * cosine + 0.5f;
    return {ru * material->uvScaleU - material->uvTranslateU,
            rv * material->uvScaleV - material->uvTranslateV};
}

void destroyGlTextures() {
    if (!gGL) return;
    for (const auto& item : gGlTextures) if (item.second) glDeleteTextures(1, &item.second);
    gGlTextures.clear();
}

void uploadGlTextures() {
    if (!gGL || !gScene) return;
    destroyGlTextures();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (const auto& texture : gScene->textures()) {
        if (texture.width <= 0 || texture.height <= 0 || texture.rgba.empty()) continue;
        GLuint id = 0;
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture.width, texture.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, texture.rgba.data());
        gGlTextures[texture.name] = id;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void updateModelBounds() {
    gModelCenter = {};
    gFitScale = 1.0f;
    if (!gScene || gScene->meshes().empty()) return;

    Vec3 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3 maximum{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
    const auto pose = gScene->samplePose(nullptr, 0.0f);
    bool found = false;
    for (const auto& mesh : gScene->meshes()) {
        for (const auto& vertex : mesh.vertices) {
            const auto p = gScene->skin(vertex, pose);
            minimum.x = std::min(minimum.x, p.x); minimum.y = std::min(minimum.y, p.y); minimum.z = std::min(minimum.z, p.z);
            maximum.x = std::max(maximum.x, p.x); maximum.y = std::max(maximum.y, p.y); maximum.z = std::max(maximum.z, p.z);
            found = true;
        }
    }
    if (!found) return;
    gModelCenter = {(minimum.x + maximum.x) * 0.5f,
                    (minimum.y + maximum.y) * 0.5f,
                    (minimum.z + maximum.z) * 0.5f};
    const float extent = std::max({maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z});
    if (extent > 1e-5f) gFitScale = 1.75f / extent;
}

const Animation* actionAt(std::size_t wanted) {
    if (!gScene) return nullptr;
    std::size_t index = 0;
    for (const auto& animation : gScene->animations()) {
        if (animation.loop || animation.frames <= 2.0f) continue;
        if (index++ == wanted) return &animation;
    }
    return nullptr;
}

void play(const Animation* animation) {
    if (!animation) return;
    gCurrent = animation;
    gFrame = 0.0f;
    setStatus(gSelectedModel + " | " + animation->name);
}

void loadModel(const std::string& virtualPath) {
    if (!gArchive) throw std::runtime_error("ROM archive is not open");
    const auto payload = gArchive->load(virtualPath);
    auto next = std::make_unique<Scene>(Scene::parse(payload.modelBch, payload.animationBch));
    if (next->meshes().empty()) throw std::runtime_error("Selected BCH has no visible 3D mesh");

    gScene = std::move(next);
    gSelectedModel = virtualPath;
    gYaw = gPitch = gFrame = 0.0f;
    gZoom = 1.0f;
    gIdle = gScene->findAnimation("Pii_anim_nomove_kamae");
    if (!gIdle) {
        for (const auto& animation : gScene->animations()) {
            if (animation.loop) { gIdle = &animation; break; }
        }
    }
    gCurrent = gIdle;
    updateModelBounds();
    if (gGL) uploadGlTextures();
    setStatus(virtualPath + "  |  arrastra para rotar  |  rueda para zoom");
}

void setupProjection(int width, int height) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const double aspect = static_cast<double>(width) / static_cast<double>(height);
    const double top = 0.52;
    glFrustum(-top * aspect, top * aspect, -top, top, 0.9, 30.0);
    glMatrixMode(GL_MODELVIEW);
}

void drawScene() {
    if (!gRenderWindow || !gGL) return;
    RECT rc{};
    GetClientRect(gRenderWindow, &rc);
    setupProjection(static_cast<int>(rc.right - rc.left), static_cast<int>(rc.bottom - rc.top));

    glClearColor(0.075f, 0.085f, 0.105f, 1.0f);
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

        glBegin(GL_TRIANGLES);
        for (const auto& vertex : mesh.vertices) {
            const Vec3 p = gScene->skin(vertex, matrices);
            if (textureId) {
                const Vec2 uv = materialUv(material, vertex.uv);
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

void tick() {
    const auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - gLastTick).count();
    gLastTick = now;
    dt = std::clamp(dt, 0.0f, 0.1f);
    if (gCurrent) {
        gFrame += dt * 30.0f;
        if (gCurrent->frames > 0.0f && gFrame > gCurrent->frames) {
            if (gCurrent->loop) gFrame = std::fmod(gFrame, gCurrent->frames);
            else { gCurrent = gIdle; gFrame = 0.0f; }
        }
    }
    drawScene();
}

void initOpenGL(HWND hwnd) {
    gDC = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24; pfd.iLayerType = PFD_MAIN_PLANE;
    const int pf = ChoosePixelFormat(gDC, &pfd);
    if (!pf || !SetPixelFormat(gDC, pf, &pfd)) throw std::runtime_error("Could not set OpenGL pixel format");
    gGL = wglCreateContext(gDC);
    if (!gGL || !wglMakeCurrent(gDC, gGL)) throw std::runtime_error("Could not create OpenGL context");
    uploadGlTextures();
}

void shutdownOpenGL() {
    if (gGL) {
        destroyGlTextures();
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(gGL);
        gGL = nullptr;
    }
    if (gDC && gRenderWindow) { ReleaseDC(gRenderWindow, gDC); gDC = nullptr; }
}

void repopulatePicker() {
    if (!gPickerList) return;
    char buffer[512]{};
    if (gPickerSearch) GetWindowTextA(gPickerSearch, buffer, static_cast<int>(sizeof(buffer)));
    const std::string query = lower(buffer);
    SendMessageA(gPickerList, LB_RESETCONTENT, 0, 0);
    gFilteredModels.clear();
    for (const auto& model : gModels) {
        if (!query.empty() && lower(model).find(query) == std::string::npos) continue;
        if (SendMessageA(gPickerList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(model.c_str())) != LB_ERR) gFilteredModels.push_back(model);
    }
    if (!gFilteredModels.empty()) SendMessageA(gPickerList, LB_SETCURSEL, 0, 0);
    if (gPickerCount) {
        const std::string text = std::to_string(gFilteredModels.size()) + " modelos";
        SetWindowTextA(gPickerCount, text.c_str());
    }
}

bool loadPickerSelection() {
    const LRESULT selected = SendMessageA(gPickerList, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR || static_cast<std::size_t>(selected) >= gFilteredModels.size()) return false;
    try { loadModel(gFilteredModels[static_cast<std::size_t>(selected)]); return true; }
    catch (const std::exception& e) { MessageBoxA(gPicker, e.what(), "No se pudo cargar", MB_OK | MB_ICONERROR); return false; }
}

LRESULT CALLBACK PickerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            applyDarkTitleBar(hwnd);
            gPicker = hwnd;
            auto label = CreateWindowA("STATIC", "Buscar modelo", WS_CHILD | WS_VISIBLE,
                                       16, 16, 110, 22, hwnd, nullptr, nullptr, nullptr);
            gPickerSearch = CreateWindowExA(0, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                            16, 42, 500, 32, hwnd, controlId(ID_PICKER_SEARCH), nullptr, nullptr);
            gPickerCount = CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                                         528, 48, 130, 22, hwnd, controlId(ID_PICKER_COUNT), nullptr, nullptr);
            gPickerList = CreateWindowExA(0, "LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                                          16, 88, 642, 390, hwnd, controlId(ID_PICKER_LIST), nullptr, nullptr);
            auto load = CreateWindowA("BUTTON", "Cargar modelo", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                      438, 492, 110, 34, hwnd, controlId(ID_PICKER_LOAD), nullptr, nullptr);
            auto cancel = CreateWindowA("BUTTON", "Cancelar", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                        556, 492, 102, 34, hwnd, controlId(ID_PICKER_CANCEL), nullptr, nullptr);
            for (HWND control : {label, gPickerSearch, gPickerCount, gPickerList, load, cancel}) {
                if (control) SendMessage(control, WM_SETFONT, reinterpret_cast<WPARAM>(gFontBody), TRUE);
            }
            repopulatePicker();
            SetFocus(gPickerSearch);
            return 0;
        }
        case WM_SIZE: {
            const int width = LOWORD(lp), height = HIWORD(lp);
            if (gPickerSearch) MoveWindow(gPickerSearch, 16, 42, std::max(160, width - 176), 32, TRUE);
            if (gPickerCount) MoveWindow(gPickerCount, width - 144, 48, 128, 22, TRUE);
            if (gPickerList) MoveWindow(gPickerList, 16, 88, std::max(180, width - 32), std::max(90, height - 148), TRUE);
            if (auto h = GetDlgItem(hwnd, ID_PICKER_LOAD)) MoveWindow(h, width - 236, height - 46, 110, 34, TRUE);
            if (auto h = GetDlgItem(hwnd, ID_PICKER_CANCEL)) MoveWindow(h, width - 118, height - 46, 102, 34, TRUE);
            return 0;
        }
        case WM_DRAWITEM:
            if (lp) { drawFlatButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE; }
            break;
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT); SetTextColor(dc, C_TEXT);
            return reinterpret_cast<LRESULT>(gBrushPanel);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, C_PANEL_2); SetTextColor(dc, C_TEXT);
            return reinterpret_cast<LRESULT>(gBrushPanel);
        }
        case WM_ERASEBKGND: {
            RECT rc{}; GetClientRect(hwnd, &rc); FillRect(reinterpret_cast<HDC>(wp), &rc, gBrushPanel); return 1;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == ID_PICKER_SEARCH && HIWORD(wp) == EN_CHANGE) { repopulatePicker(); return 0; }
            if ((LOWORD(wp) == ID_PICKER_LIST && HIWORD(wp) == LBN_DBLCLK) || LOWORD(wp) == ID_PICKER_LOAD) {
                if (loadPickerSelection()) DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wp) == ID_PICKER_CANCEL) { DestroyWindow(hwnd); return 0; }
            break;
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
        case WM_DESTROY:
            gPicker = gPickerSearch = gPickerList = gPickerCount = nullptr;
            gFilteredModels.clear();
            if (gWindow) { EnableWindow(gWindow, TRUE); SetForegroundWindow(gWindow); }
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

void showModelPicker() {
    if (!gWindow || gPicker) return;
    if (gModels.empty()) {
        MessageBoxA(gWindow, "No se encontraron modelos BCH compatibles en las ROMs cargadas.", "Modelos", MB_OK | MB_ICONINFORMATION);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSA wc{}; wc.lpfnWndProc = PickerProc; wc.hInstance = GetModuleHandleA(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = gBrushPanel;
        wc.lpszClassName = "SmokeDetectorModelPickerModern";
        if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Could not register model picker");
        registered = true;
    }
    RECT owner{}; GetWindowRect(gWindow, &owner);
    const int width = 700, height = 590;
    EnableWindow(gWindow, FALSE);
    gPicker = CreateWindowExA(WS_EX_DLGMODALFRAME, "SmokeDetectorModelPickerModern", "Seleccionar modelo",
                              WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VISIBLE,
                              owner.left + ((owner.right - owner.left) - width) / 2,
                              owner.top + ((owner.bottom - owner.top) - height) / 2,
                              width, height, gWindow, nullptr, GetModuleHandleA(nullptr), nullptr);
    if (!gPicker) { EnableWindow(gWindow, TRUE); throw std::runtime_error("Could not create model picker"); }
}

void showRoms() {
    if (!gResourceArchive) return;
    std::ostringstream text;
    for (const auto& summary : gResourceArchive->sourceSummaries()) {
        text << summary.rom.path.filename().string() << "\r\n  "
             << RomInspector::formatName(summary.rom.format);
        if (!summary.rom.detectedGame.empty()) text << " | " << summary.rom.detectedGame;
        text << "\r\n  " << summary.provider
             << "\r\n  " << summary.resourceCount << " recursos indexados\r\n\r\n";
    }
    const std::string value = text.str().empty() ? "No se encontraron ROMs." : text.str();
    MessageBoxA(gWindow, value.c_str(), "ROMs detectadas", MB_OK | MB_ICONINFORMATION);
}

void exportSelected() {
    if (!gArchive || gSelectedModel.empty()) return;
    setStatus("Exportando modelo seleccionado...");
    const auto result = AssetExporter::exportModel(*gArchive, gSelectedModel, gExportRoot);
    std::ostringstream message;
    message << "Modelo exportado.\r\n\r\n"
            << result.interchangeModel.string()
            << "\r\n\r\nAnimaciones: " << result.animationClips
            << "\r\nTexturas: " << result.textures
            << "\r\nAudio: " << result.audioResources;
    MessageBoxA(gWindow, message.str().c_str(), "Exportacion de modelo", MB_OK | MB_ICONINFORMATION);
    ShellExecuteA(gWindow, "open", result.interchangeModel.parent_path().string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    setStatus(gSelectedModel);
}

std::filesystem::path executableDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) return {};
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

void exportCompleteDump() {
    if (!gResourceArchive || gDumping) return;

    gDumping = true;
    setProgress(0, 1, {});
    setStatus("Preparando dump completo de todas las ROMs...");
    pumpUi();

    std::filesystem::path stub = executableDirectory() / "smoke_sfx_stub.exe";
    if (!std::filesystem::is_regular_file(stub)) stub.clear();

    try {
        const auto results = ResourcePackageExporter::exportAll(
            *gResourceArchive,
            gExportRoot,
            stub,
            [](std::size_t current, std::size_t total, const std::string& source, const std::string& resource) {
                const std::size_t shown = total == 0 ? 0 : std::min(current + 1, total);
                setProgress(shown, total, resource);
                if (!resource.empty()) {
                    setStatus("Dump " + std::to_string(shown) + "/" + std::to_string(total) + "  |  " + resource);
                } else {
                    setStatus("Finalizando paquetes del dump...");
                }
                pumpUi();
            });

        std::size_t resources = 0;
        std::uint64_t bytes = 0;
        for (const auto& result : results) {
            resources += result.resources;
            bytes += result.bytes;
        }

        gDumping = false;
        setProgress(0, 0, {});
        setStatus("Dump completo terminado: " + std::to_string(resources) + " recursos");

        std::ostringstream message;
        message << "Dump completo terminado.\r\n\r\nROMs: " << results.size()
                << "\r\nRecursos: " << resources
                << "\r\nBytes exportados: " << bytes
                << "\r\n\r\nDestino:\r\n" << (gExportRoot / "resources").string();
        MessageBoxA(gWindow, message.str().c_str(), "Dump completo", MB_OK | MB_ICONINFORMATION);
        ShellExecuteA(gWindow, "open", (gExportRoot / "resources").string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } catch (...) {
        gDumping = false;
        setProgress(0, 0, {});
        throw;
    }
}

void layoutMain(HWND hwnd) {
    if (!gRenderWindow) return;
    RECT rc{}; GetClientRect(hwnd, &rc);
    const int width = std::max(1, static_cast<int>(rc.right - rc.left));
    const int height = std::max(1, static_cast<int>(rc.bottom - rc.top));
    const int contentHeight = std::max(1, height - HEADER_HEIGHT - FOOTER_HEIGHT - MARGIN * 2);
    MoveWindow(gRenderWindow, MARGIN, HEADER_HEIGHT + MARGIN,
               std::max(1, width - MARGIN * 2), contentHeight, TRUE);
}

LRESULT CALLBACK RenderProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_LBUTTONDOWN: gDragging = true; gLastMouse = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; SetCapture(hwnd); return 0;
        case WM_LBUTTONUP: gDragging = false; ReleaseCapture(); return 0;
        case WM_MOUSEMOVE:
            if (gDragging) {
                POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                gYaw += (p.x - gLastMouse.x) * 0.55f; gPitch += (p.y - gLastMouse.y) * 0.35f;
                gPitch = std::clamp(gPitch, -70.0f, 70.0f); gLastMouse = p;
            }
            return 0;
        case WM_MOUSEWHEEL: gZoom *= GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1.1f : 0.9f; gZoom = std::clamp(gZoom, 0.45f, 2.25f); return 0;
        case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            ensureUiResources();
            applyDarkTitleBar(hwnd);
            createCommandButton(hwnd, ID_MODELS, "Modelos", 16, 96);
            createCommandButton(hwnd, ID_ROMS, "ROMs", 120, 78);
            createCommandButton(hwnd, ID_EXPORT_MODEL, "Exportar modelo", 206, 132);
            createCommandButton(hwnd, ID_DUMP_ALL, "DUMP COMPLETO", 346, 148);
            createCommandButton(hwnd, ID_IDLE, "Idle", 514, 70);
            createCommandButton(hwnd, ID_ACTION1, "Accion 1", 592, 86);
            createCommandButton(hwnd, ID_ACTION2, "Accion 2", 686, 86);
            gRenderWindow = CreateWindowExA(0, "SmokeDetectorRenderModern", "",
                                            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                            MARGIN, HEADER_HEIGHT + MARGIN, 1, 1,
                                            hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            if (!gRenderWindow) return -1;
            layoutMain(hwnd);
            return 0;
        case WM_SIZE:
            layoutMain(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lp);
            info->ptMinTrackSize = {860, 580};
            return 0;
        }
        case WM_PAINT:
            paintChrome(hwnd);
            return 0;
        case WM_DRAWITEM:
            if (lp) { drawFlatButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lp)); return TRUE; }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_COMMAND:
            if (gDumping) return 0;
            try {
                switch (LOWORD(wp)) {
                    case ID_MODELS: showModelPicker(); break;
                    case ID_ROMS: showRoms(); break;
                    case ID_EXPORT_MODEL: exportSelected(); break;
                    case ID_DUMP_ALL: exportCompleteDump(); break;
                    case ID_IDLE: play(gIdle); break;
                    case ID_ACTION1: play(actionAt(0)); break;
                    case ID_ACTION2: play(actionAt(1)); break;
                }
            } catch (const std::exception& e) {
                MessageBoxA(hwnd, e.what(), "Smoke Detector", MB_OK | MB_ICONERROR);
                setStatus("Error durante la operacion");
            }
            return 0;
        case WM_CLOSE:
            if (!gDumping) DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            shutdownOpenGL();
            releaseUiResources();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

std::filesystem::path findRomDirectory(std::filesystem::path start) {
    std::error_code ec;
    start = std::filesystem::absolute(start, ec);
    if (ec) return {};
    for (;;) {
        const auto candidate = start / "roms";
        if (std::filesystem::is_directory(candidate, ec)) return candidate;
        ec.clear();
        const auto parent = start.parent_path();
        if (parent.empty() || parent == start) break;
        start = parent;
    }
    return {};
}

void initializeArchives() {
    gRomDirectory = findRomDirectory(std::filesystem::current_path());
    if (gRomDirectory.empty()) gRomDirectory = findRomDirectory(executableDirectory());
    if (gRomDirectory.empty()) throw std::runtime_error("No se encontro la carpeta roms del repositorio.");
    gExportRoot = gRomDirectory.parent_path() / "exports";

    gResourceArchive = std::make_unique<RomResourceArchive>();
    if (!gResourceArchive->open(gRomDirectory)) throw std::runtime_error("No se encontraron ROMs legibles en roms/.");

    gArchive = std::make_unique<RomModelArchive>();
    gArchive->open(gRomDirectory);
    gModels = gArchive->modelResources();

    if (!gModels.empty()) {
        auto pikachu = std::find_if(gModels.begin(), gModels.end(), [](const std::string& value) {
            return lower(value).find("pikat") != std::string::npos;
        });
        loadModel(pikachu != gModels.end() ? *pikachu : gModels.front());
    } else {
        setStatus("ROMs cargadas. No hay modelos BCH compatibles, pero el dump completo esta disponible.");
    }
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    try {
        ensureUiResources();
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
        wc.lpfnWndProc = WndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = gBrushBg;
        wc.lpszClassName = "SmokeDetectorWindowModern";
        if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            throw std::runtime_error("Could not register main window");
        }

        gWindow = CreateWindowExA(0, wc.lpszClassName, "Smoke Detector",
                                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 1080, 760,
                                  nullptr, nullptr, instance, nullptr);
        if (!gWindow || !gRenderWindow) throw std::runtime_error("Could not create main window");

        initOpenGL(gRenderWindow);
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
        releaseUiResources();
        return 1;
    }
}