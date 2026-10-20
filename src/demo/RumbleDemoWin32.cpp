#include "bch/BchScene.h"
#include "rom/RumbleWorldArchive.h"

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
using promptemon::rom::RumbleWorldArchive;

constexpr int ID_IDLE = 1001;
constexpr int ID_ACTION1 = 1002;
constexpr int ID_ACTION2 = 1003;
constexpr int ID_MODELS = 1004;
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

std::unique_ptr<RumbleWorldArchive> gArchive;
std::unique_ptr<Scene> gScene;
std::vector<std::string> gModelResources;
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
    const float centeredU = uv.x - 0.5f;
    const float centeredV = uv.y - 0.5f;
    const float rotatedU = centeredU * cosine - centeredV * sine + 0.5f;
    const float rotatedV = centeredU * sine + centeredV * cosine + 0.5f;
    return {
        rotatedU * material->uvScaleU - material->uvTranslateU,
        rotatedV * material->uvScaleV - material->uvTranslateV
    };
}

void setStatus(const std::string& text) {
    if (gWindow) SetWindowTextA(gWindow, ("promptemon-steidium - " + text).c_str());
}

void destroyGlTextures() {
    if (!gGL) return;
    for (const auto& [name, id] : gGlTextures) {
        (void)name;
        if (id) glDeleteTextures(1, &id);
    }
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
    const auto bindPose = gScene->samplePose(nullptr, 0.0f);
    bool found = false;

    for (const auto& mesh : gScene->meshes()) {
        for (const auto& vertex : mesh.vertices) {
            const auto p = gScene->skin(vertex, bindPose);
            minimum.x = std::min(minimum.x, p.x);
            minimum.y = std::min(minimum.y, p.y);
            minimum.z = std::min(minimum.z, p.z);
            maximum.x = std::max(maximum.x, p.x);
            maximum.y = std::max(maximum.y, p.y);
            maximum.z = std::max(maximum.z, p.z);
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

const Animation* chooseAction(const char* preferred, const char* contains) {
    if (!gScene) return nullptr;
    if (preferred) {
        if (auto* animation = gScene->findAnimation(preferred)) return animation;
    }
    for (const auto& animation : gScene->animations()) {
        if (!animation.loop && animation.frames > 2.0f &&
            (!contains || animation.name.find(contains) != std::string::npos)) return &animation;
    }
    return nullptr;
}

void play(const Animation* animation) {
    if (!animation) return;
    gCurrent = animation;
    gFrame = 0.0f;
    setStatus(gSelectedModel + " | " + animation->name);
}

std::vector<std::uint8_t> animationForModel(const std::string& modelPath) {
    if (!gArchive) return {};
    std::vector<std::string> candidates;
    if (modelPath.rfind("pii/", 0) == 0) candidates.push_back("pii/anim.bch.cx");
    if (modelPath.rfind("mii/", 0) == 0) candidates.push_back("mii/mii_anim.bch.cx");
    const auto slash = modelPath.find_last_of('/');
    if (slash != std::string::npos) {
        const auto directory = modelPath.substr(0, slash);
        candidates.push_back(directory + "/anim.bch.cx");
        candidates.push_back(directory + "/animation.bch.cx");
    }
    for (const auto& candidate : candidates) {
        if (gArchive->has(candidate)) return gArchive->extractBch(candidate);
    }
    return {};
}

void loadModel(const std::string& modelPath) {
    if (!gArchive) throw std::runtime_error("ROM archive is not open");
    const auto model = gArchive->extractBch(modelPath);
    const auto animation = animationForModel(modelPath);
    auto next = std::make_unique<Scene>(Scene::parse(model, animation));
    if (next->meshes().empty()) throw std::runtime_error("Selected BCH has no visible 3D mesh: " + modelPath);

    gScene = std::move(next);
    gSelectedModel = modelPath;
    gFrame = 0.0f;
    gYaw = 0.0f;
    gPitch = 0.0f;
    gZoom = 1.0f;

    gIdle = gScene->findAnimation("Pii_anim_nomove_kamae");
    if (!gIdle) {
        for (const auto& animationClip : gScene->animations()) {
            if (animationClip.loop) {
                gIdle = &animationClip;
                break;
            }
        }
    }
    gCurrent = gIdle;
    updateModelBounds();
    if (gGL) uploadGlTextures();
    setStatus(modelPath + " | drag: rotate | wheel: zoom");
}

void setupProjection(int width, int height) {
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const double aspect = static_cast<double>(width) / static_cast<double>(height);
    const double top = 0.52;
    const double right = top * aspect;
    glFrustum(-right, right, -top, top, 0.9, 30.0);
    glMatrixMode(GL_MODELVIEW);
}

void drawScene() {
    if (!gScene || !gRenderWindow || !gGL) return;
    RECT rc{};
    GetClientRect(gRenderWindow, &rc);
    setupProjection(rc.right, rc.bottom);

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
        const Material* material = mesh.materialId < gScene->materials().size()
            ? &gScene->materials()[mesh.materialId]
            : nullptr;
        GLuint textureId = 0;
        if (texture) {
            const auto it = gGlTextures.find(texture->name);
            if (it != gGlTextures.end()) textureId = it->second;
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
            else {
                gCurrent = gIdle;
                gFrame = 0.0f;
                setStatus(gSelectedModel + " | idle");
            }
        }
    }
    drawScene();
}

void initOpenGL(HWND hwnd) {
    gDC = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
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
    if (gDC && gRenderWindow) {
        ReleaseDC(gRenderWindow, gDC);
        gDC = nullptr;
    }
}

void repopulatePicker() {
    if (!gPickerList) return;
    char searchBuffer[512]{};
    if (gPickerSearch) GetWindowTextA(gPickerSearch, searchBuffer, static_cast<int>(sizeof(searchBuffer)));
    const std::string query = lower(searchBuffer);

    SendMessageA(gPickerList, LB_RESETCONTENT, 0, 0);
    gFilteredModels.clear();
    for (const auto& path : gModelResources) {
        if (!query.empty() && lower(path).find(query) == std::string::npos) continue;
        if (SendMessageA(gPickerList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(path.c_str())) != LB_ERR) {
            gFilteredModels.push_back(path);
        }
    }
    if (!gFilteredModels.empty()) SendMessageA(gPickerList, LB_SETCURSEL, 0, 0);
    if (gPickerCount) {
        const std::string text = std::to_string(gFilteredModels.size()) + " modelos";
        SetWindowTextA(gPickerCount, text.c_str());
    }
}

bool loadPickerSelection() {
    if (!gPickerList) return false;
    const LRESULT selected = SendMessageA(gPickerList, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR || static_cast<std::size_t>(selected) >= gFilteredModels.size()) return false;
    try {
        loadModel(gFilteredModels[static_cast<std::size_t>(selected)]);
        return true;
    } catch (const std::exception& e) {
        MessageBoxA(gPicker, e.what(), "No se pudo cargar el modelo", MB_OK | MB_ICONERROR);
        return false;
    }
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
            gPickerList = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
                                          WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                                          12, 48, 640, 420, hwnd, reinterpret_cast<HMENU>(ID_PICKER_LIST), nullptr, nullptr);
            CreateWindowA("BUTTON", "Cargar", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                          458, 480, 92, 30, hwnd, reinterpret_cast<HMENU>(ID_PICKER_LOAD), nullptr, nullptr);
            CreateWindowA("BUTTON", "Cancelar", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          560, 480, 92, 30, hwnd, reinterpret_cast<HMENU>(ID_PICKER_CANCEL), nullptr, nullptr);
            repopulatePicker();
            SetFocus(gPickerSearch);
            return 0;
        case WM_SIZE: {
            const int width = LOWORD(lp);
            const int height = HIWORD(lp);
            if (gPickerSearch) MoveWindow(gPickerSearch, 74, 10, std::max(120, width - 180), 26, TRUE);
            if (gPickerCount) MoveWindow(gPickerCount, width - 98, 15, 86, 22, TRUE);
            if (gPickerList) MoveWindow(gPickerList, 12, 48, std::max(120, width - 24), std::max(80, height - 100), TRUE);
            if (auto load = GetDlgItem(hwnd, ID_PICKER_LOAD)) MoveWindow(load, width - 206, height - 42, 92, 30, TRUE);
            if (auto cancel = GetDlgItem(hwnd, ID_PICKER_CANCEL)) MoveWindow(cancel, width - 104, height - 42, 92, 30, TRUE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lp);
            info->ptMinTrackSize.x = 520;
            info->ptMinTrackSize.y = 380;
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == ID_PICKER_SEARCH && HIWORD(wp) == EN_CHANGE) {
                repopulatePicker();
                return 0;
            }
            if ((LOWORD(wp) == ID_PICKER_LIST && HIWORD(wp) == LBN_DBLCLK) || LOWORD(wp) == ID_PICKER_LOAD) {
                if (loadPickerSelection()) DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wp) == ID_PICKER_CANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            gPicker = nullptr;
            gPickerSearch = nullptr;
            gPickerList = nullptr;
            gPickerCount = nullptr;
            gFilteredModels.clear();
            if (gWindow) {
                EnableWindow(gWindow, TRUE);
                SetForegroundWindow(gWindow);
                SetFocus(gWindow);
            }
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

void showModelPicker() {
    if (!gWindow || gPicker) return;
    static bool registered = false;
    if (!registered) {
        WNDCLASSA wc{};
        wc.lpfnWndProc = PickerProc;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = "PromptemonModelPicker";
        if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            throw std::runtime_error("Could not register model picker window");
        }
        registered = true;
    }

    RECT owner{};
    GetWindowRect(gWindow, &owner);
    const int width = 700;
    const int height = 570;
    const int x = owner.left + ((owner.right - owner.left) - width) / 2;
    const int y = owner.top + ((owner.bottom - owner.top) - height) / 2;
    EnableWindow(gWindow, FALSE);
    gPicker = CreateWindowExA(WS_EX_DLGMODALFRAME, "PromptemonModelPicker",
                              "Modelos 3D de Pokemon Rumble World",
                              WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VISIBLE,
                              x, y, width, height, gWindow, nullptr, GetModuleHandleA(nullptr), nullptr);
    if (!gPicker) {
        EnableWindow(gWindow, TRUE);
        throw std::runtime_error("Could not create model picker");
    }
}

void layoutMainWindow(HWND hwnd) {
    if (!gRenderWindow) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int width = static_cast<int>(std::max<LONG>(1, rc.right - rc.left));
    const int height = static_cast<int>(std::max<LONG>(1, rc.bottom - rc.top));
    MoveWindow(gRenderWindow, 0, TOOLBAR_HEIGHT, width, std::max(1, height - TOOLBAR_HEIGHT), TRUE);
}

LRESULT CALLBACK RenderProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_LBUTTONDOWN:
            gDragging = true;
            gLastMouse = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            SetCapture(hwnd);
            return 0;
        case WM_LBUTTONUP:
            gDragging = false;
            ReleaseCapture();
            return 0;
        case WM_MOUSEMOVE:
            if (gDragging) {
                const POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                gYaw += (p.x - gLastMouse.x) * 0.55f;
                gPitch += (p.y - gLastMouse.y) * 0.35f;
                gPitch = std::clamp(gPitch, -70.0f, 70.0f);
                gLastMouse = p;
            }
            return 0;
        case WM_MOUSEWHEEL:
            gZoom *= GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1.1f : 0.9f;
            gZoom = std::clamp(gZoom, 0.45f, 2.25f);
            return 0;
        case WM_ERASEBKGND:
            return 1;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            CreateWindowA("BUTTON", "Modelos...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          12, 11, 105, 30, hwnd, reinterpret_cast<HMENU>(ID_MODELS), nullptr, nullptr);
            CreateWindowA("BUTTON", "Idle", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          125, 11, 80, 30, hwnd, reinterpret_cast<HMENU>(ID_IDLE), nullptr, nullptr);
            CreateWindowA("BUTTON", "Accion 1", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          213, 11, 90, 30, hwnd, reinterpret_cast<HMENU>(ID_ACTION1), nullptr, nullptr);
            CreateWindowA("BUTTON", "Accion 2", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                          311, 11, 90, 30, hwnd, reinterpret_cast<HMENU>(ID_ACTION2), nullptr, nullptr);
            gRenderWindow = CreateWindowExA(0, "PromptemonRenderSurface", "",
                                            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                            0, TOOLBAR_HEIGHT, 1, 1, hwnd, nullptr, GetModuleHandleA(nullptr), nullptr);
            if (!gRenderWindow) return -1;
            layoutMainWindow(hwnd);
            return 0;
        }
        case WM_SIZE:
            layoutMainWindow(hwnd);
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lp);
            info->ptMinTrackSize.x = 640;
            info->ptMinTrackSize.y = 480;
            HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi{sizeof(mi)};
            if (GetMonitorInfoA(monitor, &mi)) {
                const int maxWidth = mi.rcWork.right - mi.rcWork.left;
                const int maxHeight = mi.rcWork.bottom - mi.rcWork.top;
                info->ptMaxTrackSize.x = maxWidth;
                info->ptMaxTrackSize.y = maxHeight;
                info->ptMaxSize.x = maxWidth;
                info->ptMaxSize.y = maxHeight;
            }
            return 0;
        }
        case WM_COMMAND:
            try {
                if (LOWORD(wp) == ID_MODELS) showModelPicker();
                else if (LOWORD(wp) == ID_IDLE) play(gIdle);
                else if (LOWORD(wp) == ID_ACTION1) play(chooseAction("Pii_ROOT_attack_2__attack", "attack"));
                else if (LOWORD(wp) == ID_ACTION2) play(chooseAction("Pii_ROOT_attack_TUNODORIRU__attack", "attack_"));
            } catch (const std::exception& e) {
                MessageBoxA(hwnd, e.what(), "promptemon-steidium", MB_OK | MB_ICONERROR);
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            shutdownOpenGL();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

std::filesystem::path findCiaInRomDir(const std::filesystem::path& base) {
    const auto romDir = base / "roms";
    std::error_code ec;
    if (!std::filesystem::is_directory(romDir, ec)) return {};
    for (const auto& entry : std::filesystem::directory_iterator(romDir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        if (lower(entry.path().extension().string()) == ".cia") return entry.path();
    }
    return {};
}

std::filesystem::path findCiaFromAncestors(std::filesystem::path start) {
    std::error_code ec;
    start = std::filesystem::absolute(start, ec);
    if (ec) return {};
    for (;;) {
        if (auto cia = findCiaInRomDir(start); !cia.empty()) return cia;
        const auto parent = start.parent_path();
        if (parent.empty() || parent == start) break;
        start = parent;
    }
    return {};
}

std::filesystem::path executableDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path ciaFromCommandLine() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 2) {
        const std::filesystem::path explicitPath = argv[1];
        LocalFree(argv);
        return explicitPath;
    }
    if (argv) LocalFree(argv);
    if (auto cia = findCiaFromAncestors(std::filesystem::current_path()); !cia.empty()) return cia;
    if (auto cia = findCiaFromAncestors(executableDirectory()); !cia.empty()) return cia;
    throw std::runtime_error("No CIA found. Put Pokemon Rumble World in: promptemon-steidium\\roms\\");
}

void initializeArchive(const std::filesystem::path& cia) {
    gArchive = std::make_unique<RumbleWorldArchive>();
    if (!gArchive->open(cia)) throw std::runtime_error("Could not open CIA: " + cia.string());
    gModelResources = gArchive->modelResources();
    if (gModelResources.empty()) throw std::runtime_error("No BCH model resources found in CIA");
    const std::string pikachu = "pii/PIKATYUU_M.bch.cx";
    if (std::find(gModelResources.begin(), gModelResources.end(), pikachu) != gModelResources.end()) loadModel(pikachu);
    else loadModel(gModelResources.front());
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    try {
        const auto cia = ciaFromCommandLine();
        initializeArchive(cia);

        WNDCLASSA renderClass{};
        renderClass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        renderClass.lpfnWndProc = RenderProc;
        renderClass.hInstance = instance;
        renderClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        renderClass.lpszClassName = "PromptemonRenderSurface";
        if (!RegisterClassA(&renderClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            throw std::runtime_error("Could not register render surface");
        }

        WNDCLASSA wc{};
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = "PromptemonSteidiumDemo";
        if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            throw std::runtime_error("Could not register window class");
        }

        gWindow = CreateWindowExA(0, wc.lpszClassName, "promptemon-steidium",
                                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 960, 720,
                                  nullptr, nullptr, instance, nullptr);
        if (!gWindow || !gRenderWindow) throw std::runtime_error("Could not create window");

        initOpenGL(gRenderWindow);
        ShowWindow(gWindow, show);
        UpdateWindow(gWindow);
        setStatus(gSelectedModel + " | drag: rotate | wheel: zoom");
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
            if (running) tick();
            Sleep(1);
        }
        return 0;
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "promptemon-steidium demo error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
