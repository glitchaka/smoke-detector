#include "bch/BchScene.h"
#include "export/AssetExporter.h"
#include "rom/RomModelArchive.h"

#define WIN32_LEAN_AND_MEAN
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
using smoke_detector::exporter::AssetExporter;

constexpr int ID_MODELS = 1001;
constexpr int ID_ROMS = 1002;
constexpr int ID_EXPORT = 1003;
constexpr int ID_IDLE = 1004;
constexpr int ID_ACTION1 = 1005;
constexpr int ID_ACTION2 = 1006;
constexpr int ID_PICKER_SEARCH = 2001;
constexpr int ID_PICKER_LIST = 2002;
constexpr int ID_PICKER_LOAD = 2003;
constexpr int ID_PICKER_CANCEL = 2004;
constexpr int ID_PICKER_COUNT = 2005;
constexpr int TOOLBAR_HEIGHT = 52;

HWND gWindow = nullptr;
HWND gRenderWindow = nullptr;
HDC gDC = nullptr;
HGLRC gGL = nullptr;

std::unique_ptr<RomModelArchive> gArchive;
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

HWND gPicker = nullptr;
HWND gPickerSearch = nullptr;
HWND gPickerList = nullptr;
HWND gPickerCount = nullptr;
std::vector<std::string> gFilteredModels;

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
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

void setStatus(const std::string& text) {
    if (gWindow) SetWindowTextA(gWindow, ("smoke-detector - " + text).c_str());
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
    setStatus(virtualPath + " | drag: rotate | wheel: zoom");
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
    if (!gScene || !gRenderWindow || !gGL) return;
    RECT rc{};
    GetClientRect(gRenderWindow, &rc);
    setupProjection(rc.right - rc.left, rc.bottom - rc.top);

    glClearColor(0.88f, 0.90f, 0.93f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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
        case WM_CREATE:
            gPicker = hwnd;
            CreateWindowA("STATIC", "Buscar:", WS_CHILD | WS_VISIBLE, 12, 15, 62, 22, hwnd, nullptr, nullptr, nullptr);
            gPickerSearch = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                            74, 10, 480, 26, hwnd, reinterpret_cast<HMENU>(ID_PICKER_SEARCH), nullptr, nullptr);
            gPickerCount = CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE | SS_RIGHT,
                                         560, 15, 92, 22, hwnd, reinterpret_cast<HMENU>(ID_PICKER_COUNT), nullptr, nullptr);
            gPickerList = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                                          12, 48, 640, 420, hwnd, reinterpret_cast<HMENU>(ID_PICKER_LIST), nullptr, nullptr);
            CreateWindowA("BUTTON", "Cargar", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 458, 480, 92, 30, hwnd, reinterpret_cast<HMENU>(ID_PICKER_LOAD), nullptr, nullptr);
            CreateWindowA("BUTTON", "Cancelar", WS_CHILD | WS_VISIBLE, 560, 480, 92, 30, hwnd, reinterpret_cast<HMENU>(ID_PICKER_CANCEL), nullptr, nullptr);
            repopulatePicker();
            SetFocus(gPickerSearch);
            return 0;
        case WM_SIZE: {
            const int width = LOWORD(lp), height = HIWORD(lp);
            if (gPickerSearch) MoveWindow(gPickerSearch, 74, 10, std::max(120, width - 180), 26, TRUE);
            if (gPickerCount) MoveWindow(gPickerCount, width - 98, 15, 86, 22, TRUE);
            if (gPickerList) MoveWindow(gPickerList, 12, 48, std::max(120, width - 24), std::max(80, height - 100), TRUE);
            if (auto h = GetDlgItem(hwnd, ID_PICKER_LOAD)) MoveWindow(h, width - 206, height - 42, 92, 30, TRUE);
            if (auto h = GetDlgItem(hwnd, ID_PICKER_CANCEL)) MoveWindow(h, width - 104, height - 42, 92, 30, TRUE);
            return 0;
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
    static bool registered = false;
    if (!registered) {
        WNDCLASSA wc{}; wc.lpfnWndProc = PickerProc; wc.hInstance = GetModuleHandleA(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = "SmokeDetectorModelPicker";
        if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Could not register model picker");
        registered = true;
    }
    RECT owner{}; GetWindowRect(gWindow, &owner);
    const int width = 700, height = 570;
    EnableWindow(gWindow, FALSE);
    gPicker = CreateWindowExA(WS_EX_DLGMODALFRAME, "SmokeDetectorModelPicker", "Modelos 3D encontrados",
                              WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VISIBLE,
                              owner.left + ((owner.right - owner.left) - width) / 2,
                              owner.top + ((owner.bottom - owner.top) - height) / 2,
                              width, height, gWindow, nullptr, GetModuleHandleA(nullptr), nullptr);
    if (!gPicker) { EnableWindow(gWindow, TRUE); throw std::runtime_error("Could not create model picker"); }
}

void showRoms() {
    if (!gArchive) return;
    std::string text;
    for (const auto& entry : gArchive->library().entries()) {
        text += entry.info.path.filename().string();
        text += "\r\n  ";
        text += RomInspector::formatName(entry.info.format);
        if (!entry.info.detectedGame.empty()) text += " | " + entry.info.detectedGame;
        text += "\r\n  ";
        text += entry.hasModelProvider ? ("3D: " + entry.modelProvider) : entry.modelError;
        text += "\r\n\r\n";
    }
    if (text.empty()) text = "No se encontraron ROMs.";
    MessageBoxA(gWindow, text.c_str(), "ROMs detectadas", MB_OK | MB_ICONINFORMATION);
}

void exportSelected() {
    if (!gArchive || gSelectedModel.empty()) return;
    const auto result = AssetExporter::exportModel(*gArchive, gSelectedModel, gExportRoot);
    std::string message = "Exportado en:\r\n" + result.directory.string() +
        "\r\n\r\nAnimaciones: " + std::to_string(result.animationClips) +
        "\r\nTexturas: " + std::to_string(result.textures) +
        "\r\nAudio: " + std::to_string(result.audioResources);
    MessageBoxA(gWindow, message.c_str(), "Exportacion completa", MB_OK | MB_ICONINFORMATION);
    ShellExecuteA(gWindow, "open", result.directory.string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void layoutMain(HWND hwnd) {
    if (!gRenderWindow) return;
    RECT rc{}; GetClientRect(hwnd, &rc);
    const int width = std::max(1L, rc.right - rc.left);
    const int height = std::max(1L, rc.bottom - rc.top);
    MoveWindow(gRenderWindow, 0, TOOLBAR_HEIGHT, width, std::max(1, height - TOOLBAR_HEIGHT), TRUE);
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
            CreateWindowA("BUTTON", "Modelos...", WS_CHILD | WS_VISIBLE, 10, 10, 100, 30, hwnd, reinterpret_cast<HMENU>(ID_MODELS), nullptr, nullptr);
            CreateWindowA("BUTTON", "ROMs...", WS_CHILD | WS_VISIBLE, 116, 10, 82, 30, hwnd, reinterpret_cast<HMENU>(ID_ROMS), nullptr, nullptr);
            CreateWindowA("BUTTON", "Exportar", WS_CHILD | WS_VISIBLE, 204, 10, 92, 30, hwnd, reinterpret_cast<HMENU>(ID_EXPORT), nullptr, nullptr);
            CreateWindowA("BUTTON", "Idle", WS_CHILD | WS_VISIBLE, 310, 10, 70, 30, hwnd, reinterpret_cast<HMENU>(ID_IDLE), nullptr, nullptr);
            CreateWindowA("BUTTON", "Accion 1", WS_CHILD | WS_VISIBLE, 386, 10, 82, 30, hwnd, reinterpret_cast<HMENU>(ID_ACTION1), nullptr, nullptr);
            CreateWindowA("BUTTON", "Accion 2", WS_CHILD | WS_VISIBLE, 474, 10, 82, 30, hwnd, reinterpret_cast<HMENU>(ID_ACTION2), nullptr, nullptr);
            gRenderWindow = CreateWindowExA(0, "SmokeDetectorRender", "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                            0, TOOLBAR_HEIGHT, 1, 1, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            if (!gRenderWindow) return -1;
            layoutMain(hwnd); return 0;
        case WM_SIZE: layoutMain(hwnd); return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lp);
            info->ptMinTrackSize = {640, 480};
            MONITORINFO mi{sizeof(mi)};
            if (GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
                info->ptMaxTrackSize.x = mi.rcWork.right - mi.rcWork.left;
                info->ptMaxTrackSize.y = mi.rcWork.bottom - mi.rcWork.top;
                info->ptMaxSize = info->ptMaxTrackSize;
            }
            return 0;
        }
        case WM_COMMAND:
            try {
                switch (LOWORD(wp)) {
                    case ID_MODELS: showModelPicker(); break;
                    case ID_ROMS: showRoms(); break;
                    case ID_EXPORT: exportSelected(); break;
                    case ID_IDLE: play(gIdle); break;
                    case ID_ACTION1: play(actionAt(0)); break;
                    case ID_ACTION2: play(actionAt(1)); break;
                }
            } catch (const std::exception& e) { MessageBoxA(hwnd, e.what(), "smoke-detector", MB_OK | MB_ICONERROR); }
            return 0;
        case WM_ERASEBKGND: return 1;
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
        case WM_DESTROY: shutdownOpenGL(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

std::filesystem::path executableDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) return {};
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
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

void initializeArchive() {
    gRomDirectory = findRomDirectory(std::filesystem::current_path());
    if (gRomDirectory.empty()) gRomDirectory = findRomDirectory(executableDirectory());
    if (gRomDirectory.empty()) throw std::runtime_error("No se encontro la carpeta roms del repositorio.");
    gExportRoot = gRomDirectory.parent_path() / "exports";

    gArchive = std::make_unique<RomModelArchive>();
    if (!gArchive->open(gRomDirectory)) throw std::runtime_error("No se encontraron ROMs legibles en roms/.");
    gModels = gArchive->modelResources();
    if (gModels.empty()) throw std::runtime_error("Se detectaron ROMs, pero ninguna contiene modelos 3D BCH compatibles.");

    auto pikachu = std::find_if(gModels.begin(), gModels.end(), [](const std::string& value) {
        return lower(value).find("pikat") != std::string::npos;
    });
    loadModel(pikachu != gModels.end() ? *pikachu : gModels.front());
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    try {
        initializeArchive();

        WNDCLASSA renderClass{};
        renderClass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        renderClass.lpfnWndProc = RenderProc; renderClass.hInstance = instance;
        renderClass.hCursor = LoadCursor(nullptr, IDC_ARROW); renderClass.lpszClassName = "SmokeDetectorRender";
        if (!RegisterClassA(&renderClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Could not register render surface");

        WNDCLASSA wc{};
        wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = WndProc; wc.hInstance = instance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = "SmokeDetectorWindow";
        if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("Could not register main window");

        gWindow = CreateWindowExA(0, wc.lpszClassName, "smoke-detector", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 960, 720, nullptr, nullptr, instance, nullptr);
        if (!gWindow || !gRenderWindow) throw std::runtime_error("Could not create main window");
        initOpenGL(gRenderWindow);
        ShowWindow(gWindow, show); UpdateWindow(gWindow);
        setStatus(gSelectedModel);
        gLastTick = std::chrono::steady_clock::now();

        MSG msg{};
        bool running = true;
        while (running) {
            while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) { running = false; break; }
                TranslateMessage(&msg); DispatchMessageA(&msg);
            }
            if (running) tick();
            Sleep(1);
        }
        return 0;
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "smoke-detector error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
