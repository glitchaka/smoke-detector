#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <GL/gl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "SmokeDetectorStudioBase.generated.inc"

#pragma comment(lib, "gdiplus.lib")

namespace {

using Gdiplus::Bitmap;
using Gdiplus::Color;
using Gdiplus::Graphics;
using Gdiplus::ImageLockModeRead;
using Gdiplus::ImageLockModeWrite;
using Gdiplus::Pen;
using Gdiplus::PixelFormat32bppARGB;
using Gdiplus::Rect;
using Gdiplus::RectF;

constexpr int EDIT_SEARCH_ID = 12001;
constexpr int EDIT_TOP_H = 44;
constexpr int EDIT_MODE_H = 34;
constexpr int EDIT_STATUS_H = 22;
constexpr int EDIT_LEFT_W = 286;
constexpr int EDIT_RIGHT_W = 324;
constexpr int EDIT_GAP = 6;
constexpr int EDIT_ROW_H = 25;
constexpr int EDIT_MIN_W = 1180;
constexpr int EDIT_MIN_H = 720;

// Neutral greys deliberately follow the supplied reference. There is no blue
// accent in the editor: state is communicated by value/contrast and a thin
// light edge instead of saturated selection cards.
constexpr COLORREF E_BG = RGB(31, 31, 31);
constexpr COLORREF E_PANEL = RGB(39, 39, 39);
constexpr COLORREF E_PANEL_ALT = RGB(44, 44, 44);
constexpr COLORREF E_SURFACE = RGB(35, 35, 35);
constexpr COLORREF E_FIELD = RGB(50, 50, 50);
constexpr COLORREF E_ACTIVE = RGB(60, 60, 60);
constexpr COLORREF E_BORDER = RGB(66, 66, 66);
constexpr COLORREF E_BORDER_ACTIVE = RGB(116, 116, 116);
constexpr COLORREF E_TEXT = RGB(230, 230, 230);
constexpr COLORREF E_MUTED = RGB(158, 158, 158);
constexpr COLORREF E_DIM = RGB(104, 104, 104);
constexpr COLORREF E_WIRE = RGB(236, 236, 236);

constexpr float PI_F = 3.14159265358979323846f;

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

enum class EditorMode {
    Browse = 0,
    Model,
    Rig,
    Texture,
    Animate
};

struct EditorLayout {
    RECT client{};
    RECT top{};
    RECT modes{};
    RECT left{};
    RECT search{};
    RECT tree{};
    RECT center{};
    RECT viewport{};
    RECT uvCanvas{};
    RECT timeline{};
    RECT right{};
    RECT status{};
    RECT open{};
    RECT exportOne{};
    RECT dump{};
    std::array<RECT, 5> modeButtons{};
};

struct MeshEdit {
    bool visible = true;
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    float rx = 0.0f, ry = 0.0f, rz = 0.0f;
    float sx = 1.0f, sy = 1.0f, sz = 1.0f;
};

struct BoneEdit {
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    float rx = 0.0f, ry = 0.0f, rz = 0.0f;
};

struct ImageLayerState {
    std::wstring path;
    bool visible = true;
    float offsetU = 0.0f;
    float offsetV = 0.0f;
    float scale = 1.0f;
    float rotation = 0.0f;
    float cropX = 0.0f;
    float cropY = 0.0f;
    float cropW = 1.0f;
    float cropH = 1.0f;
};

HWND gEditorSearch = nullptr;
HBRUSH gEditorEditBrush = nullptr;
HFONT gEditorTitleFont = nullptr;
HFONT gEditorSectionFont = nullptr;
HFONT gEditorSmallFont = nullptr;
HFONT gEditorTinyFont = nullptr;
EditorMode gEditorMode = EditorMode::Browse;
std::size_t gEditorMesh = 0;
std::size_t gEditorBone = 0;
std::size_t gEditorMaterial = 0;
std::size_t gEditorMeshScroll = 0;
std::size_t gEditorBoneScroll = 0;
bool gAnimationPaused = false;
std::unordered_map<std::string, std::vector<MeshEdit>> gMeshEdits;
std::unordered_map<std::string, std::vector<BoneEdit>> gBoneEdits;
std::unordered_map<std::string, std::vector<ImageLayerState>> gImageLayers;
std::unique_ptr<Bitmap> gLayerBitmap;
std::wstring gLayerBitmapPath;
ULONG_PTR gGdiToken = 0;

int ew(const RECT& r) { return static_cast<int>(r.right - r.left); }
int eh(const RECT& r) { return static_cast<int>(r.bottom - r.top); }
bool eHit(const RECT& r, int x, int y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }

EditorLayout editorLayout(HWND hwnd) {
    EditorLayout l{};
    GetClientRect(hwnd, &l.client);
    const int w = std::max(1, ew(l.client));
    const int h = std::max(1, eh(l.client));
    l.top = {0, 0, w, EDIT_TOP_H};
    l.modes = {0, EDIT_TOP_H, w, EDIT_TOP_H + EDIT_MODE_H};
    l.status = {0, h - EDIT_STATUS_H, w, h};
    const int top = l.modes.bottom + EDIT_GAP;
    const int bottom = l.status.top - EDIT_GAP;
    l.left = {EDIT_GAP, top, EDIT_GAP + EDIT_LEFT_W, bottom};
    l.right = {w - EDIT_GAP - EDIT_RIGHT_W, top, w - EDIT_GAP, bottom};
    l.center = {l.left.right + EDIT_GAP, top, l.right.left - EDIT_GAP, bottom};
    l.search = {l.left.left + 8, l.left.top + 38, l.left.right - 8, l.left.top + 68};
    l.tree = {l.left.left + 5, l.left.top + 76, l.left.right - 5, l.left.bottom - 5};

    l.dump = {w - 102, 7, w - 8, 37};
    l.exportOne = {w - 202, 7, w - 108, 37};
    l.open = {w - 302, 7, w - 208, 37};

    const int modeLeft = l.left.right + EDIT_GAP;
    const int modeWidth = 86;
    for (int i = 0; i < 5; ++i) {
        l.modeButtons[static_cast<std::size_t>(i)] = {
            modeLeft + i * modeWidth, l.modes.top + 4,
            modeLeft + (i + 1) * modeWidth - 2, l.modes.bottom - 4
        };
    }

    l.viewport = {l.center.left + 1, l.center.top + 29, l.center.right - 1, l.center.bottom - 1};
    l.uvCanvas = {};
    l.timeline = {};
    if (gEditorMode == EditorMode::Texture) {
        const int split = l.center.left + std::max(260, ew(l.center) * 47 / 100);
        l.uvCanvas = {l.center.left + 1, l.center.top + 29, split - 3, l.center.bottom - 1};
        l.viewport = {split + 3, l.center.top + 29, l.center.right - 1, l.center.bottom - 1};
    } else if (gEditorMode == EditorMode::Animate) {
        const int timelineH = 106;
        l.timeline = {l.center.left + 1, l.center.bottom - timelineH, l.center.right - 1, l.center.bottom - 1};
        l.viewport = {l.center.left + 1, l.center.top + 29, l.center.right - 1, l.timeline.top - 4};
    }
    return l;
}

void editorUiResources() {
    ensureStudioResources();
    if (!gEditorEditBrush) gEditorEditBrush = CreateSolidBrush(E_FIELD);
    if (!gEditorTitleFont) gEditorTitleFont = createUiFont(16, FW_SEMIBOLD);
    if (!gEditorSectionFont) gEditorSectionFont = createUiFont(12, FW_SEMIBOLD);
    if (!gEditorSmallFont) gEditorSmallFont = createUiFont(11, FW_NORMAL);
    if (!gEditorTinyFont) gEditorTinyFont = createUiFont(10, FW_NORMAL);
}

void releaseEditorUiResources() {
    if (gEditorEditBrush) DeleteObject(gEditorEditBrush);
    if (gEditorTitleFont) DeleteObject(gEditorTitleFont);
    if (gEditorSectionFont) DeleteObject(gEditorSectionFont);
    if (gEditorSmallFont) DeleteObject(gEditorSmallFont);
    if (gEditorTinyFont) DeleteObject(gEditorTinyFont);
    gEditorEditBrush = nullptr;
    gEditorTitleFont = gEditorSectionFont = gEditorSmallFont = gEditorTinyFont = nullptr;
}

void eFill(HDC dc, const RECT& r, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}

void eLine(HDC dc, int x1, int y1, int x2, int y2, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, old);
    DeleteObject(pen);
}

void eBox(HDC dc, const RECT& r, COLORREF fill, COLORREF border = E_BORDER) {
    eFill(dc, r, fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, r.left, r.top, r.right, r.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void eText(HDC dc, const std::string& value, RECT r, HFONT font, COLORREF color,
           UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    SelectObject(dc, font);
    DrawTextA(dc, value.c_str(), -1, &r, flags);
}

void eButton(HDC dc, const RECT& r, const char* label, bool active = false, bool enabled = true) {
    const COLORREF fill = !enabled ? E_PANEL : (active ? E_ACTIVE : E_FIELD);
    const COLORREF border = active ? E_BORDER_ACTIVE : E_BORDER;
    eBox(dc, r, fill, border);
    if (active) {
        RECT mark{r.left, r.top, r.left + 2, r.bottom};
        eFill(dc, mark, RGB(190, 190, 190));
    }
    eText(dc, label, r, gEditorSmallFont, enabled ? E_TEXT : E_DIM,
          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

const char* editorModeName(EditorMode mode) {
    switch (mode) {
        case EditorMode::Browse: return "Browse";
        case EditorMode::Model: return "Model";
        case EditorMode::Rig: return "Rig";
        case EditorMode::Texture: return "Texture";
        case EditorMode::Animate: return "Animate";
    }
    return "Browse";
}

std::vector<MeshEdit>& currentMeshEdits() {
    static std::vector<MeshEdit> empty;
    if (!gScene || gSelectedModel.empty()) return empty;
    auto& edits = gMeshEdits[gSelectedModel];
    if (edits.size() != gScene->meshes().size()) edits.assign(gScene->meshes().size(), MeshEdit{});
    return edits;
}

std::vector<BoneEdit>& currentBoneEdits() {
    static std::vector<BoneEdit> empty;
    if (!gScene || gSelectedModel.empty()) return empty;
    auto& edits = gBoneEdits[gSelectedModel];
    if (edits.size() != gScene->bones().size()) edits.assign(gScene->bones().size(), BoneEdit{});
    return edits;
}

std::vector<ImageLayerState>& currentImageLayers() {
    static std::vector<ImageLayerState> empty;
    if (!gScene || gSelectedModel.empty()) return empty;
    auto& layers = gImageLayers[gSelectedModel];
    if (layers.size() != gScene->materials().size()) layers.resize(gScene->materials().size());
    return layers;
}

ImageLayerState* currentLayer() {
    auto& layers = currentImageLayers();
    if (layers.empty()) return nullptr;
    gEditorMaterial = std::min(gEditorMaterial, layers.size() - 1);
    return &layers[gEditorMaterial];
}

const promptemon::bch::Texture* currentTexture() {
    if (!gScene || gScene->materials().empty()) return nullptr;
    const auto materialIndex = std::min(gEditorMaterial, gScene->materials().size() - 1);
    return gScene->textureForMaterial(static_cast<std::uint16_t>(materialIndex));
}

void ensureEditorState() {
    if (!gScene) return;
    currentMeshEdits();
    currentBoneEdits();
    currentImageLayers();
    if (!gScene->meshes().empty()) gEditorMesh = std::min(gEditorMesh, gScene->meshes().size() - 1); else gEditorMesh = 0;
    if (!gScene->bones().empty()) gEditorBone = std::min(gEditorBone, gScene->bones().size() - 1); else gEditorBone = 0;
    if (!gScene->materials().empty()) gEditorMaterial = std::min(gEditorMaterial, gScene->materials().size() - 1); else gEditorMaterial = 0;
}

promptemon::bch::Mat4 eIdentity() {
    promptemon::bch::Mat4 m{};
    m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1.0f;
    return m;
}

promptemon::bch::Mat4 eMul(const promptemon::bch::Mat4& a, const promptemon::bch::Mat4& b) {
    promptemon::bch::Mat4 r{};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) r.m[c * 4 + row] += a.m[k * 4 + row] * b.m[c * 4 + k];
        }
    }
    return r;
}

promptemon::bch::Mat4 eTranslate(float x, float y, float z) {
    auto m = eIdentity();
    m.m[12] = x; m.m[13] = y; m.m[14] = z;
    return m;
}

promptemon::bch::Mat4 eRotate(float rx, float ry, float rz) {
    const float cx = std::cos(rx), sx = std::sin(rx);
    const float cy = std::cos(ry), sy = std::sin(ry);
    const float cz = std::cos(rz), sz = std::sin(rz);
    promptemon::bch::Mat4 mx = eIdentity(), my = eIdentity(), mz = eIdentity();
    mx.m[5] = cx; mx.m[9] = -sx; mx.m[6] = sx; mx.m[10] = cx;
    my.m[0] = cy; my.m[8] = sy; my.m[2] = -sy; my.m[10] = cy;
    mz.m[0] = cz; mz.m[4] = -sz; mz.m[1] = sz; mz.m[5] = cz;
    return eMul(mz, eMul(my, mx));
}

promptemon::bch::Vec3 eTransform(const promptemon::bch::Mat4& m, promptemon::bch::Vec3 p) {
    return {
        m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
        m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
        m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14]
    };
}

promptemon::bch::Mat4 boneDeltaMatrix(std::size_t boneIndex) {
    const auto& edits = currentBoneEdits();
    if (!gScene || boneIndex >= gScene->bones().size() || boneIndex >= edits.size()) return eIdentity();
    const auto& bone = gScene->bones()[boneIndex];
    const auto& edit = edits[boneIndex];
    const float px = bone.bindGlobal.m[12], py = bone.bindGlobal.m[13], pz = bone.bindGlobal.m[14];
    return eMul(eTranslate(px + edit.tx, py + edit.ty, pz + edit.tz),
                eMul(eRotate(edit.rx, edit.ry, edit.rz), eTranslate(-px, -py, -pz)));
}

promptemon::bch::Mat4 accumulatedBoneDelta(std::size_t boneIndex) {
    if (!gScene || boneIndex >= gScene->bones().size()) return eIdentity();
    std::array<std::size_t, 128> chain{};
    std::size_t count = 0;
    int current = static_cast<int>(boneIndex);
    while (current >= 0 && static_cast<std::size_t>(current) < gScene->bones().size() && count < chain.size()) {
        chain[count++] = static_cast<std::size_t>(current);
        current = gScene->bones()[static_cast<std::size_t>(current)].parent;
    }
    auto delta = eIdentity();
    while (count > 0) delta = eMul(boneDeltaMatrix(chain[--count]), delta);
    return delta;
}

std::vector<promptemon::bch::Mat4> editorSkinMatrices() {
    if (!gScene) return {};
    auto matrices = gScene->samplePose(gCurrent, gFrame);
    const auto& edits = currentBoneEdits();
    if (edits.empty()) return matrices;
    for (std::size_t i = 0; i < matrices.size(); ++i) matrices[i] = eMul(accumulatedBoneDelta(i), matrices[i]);
    return matrices;
}

promptemon::bch::Vec3 meshCenter(const promptemon::bch::Mesh& mesh,
                                 const std::vector<promptemon::bch::Mat4>& matrices) {
    promptemon::bch::Vec3 center{};
    if (mesh.vertices.empty()) return center;
    for (const auto& v : mesh.vertices) {
        const auto p = gScene->skin(v, matrices);
        center.x += p.x; center.y += p.y; center.z += p.z;
    }
    const float inv = 1.0f / static_cast<float>(mesh.vertices.size());
    center.x *= inv; center.y *= inv; center.z *= inv;
    return center;
}

promptemon::bch::Vec3 applyMeshEdit(promptemon::bch::Vec3 p, const MeshEdit& edit,
                                    promptemon::bch::Vec3 pivot) {
    p.x -= pivot.x; p.y -= pivot.y; p.z -= pivot.z;
    p.x *= edit.sx; p.y *= edit.sy; p.z *= edit.sz;
    const auto rotation = eRotate(edit.rx, edit.ry, edit.rz);
    p = eTransform(rotation, p);
    p.x += pivot.x + edit.tx; p.y += pivot.y + edit.ty; p.z += pivot.z + edit.tz;
    return p;
}

promptemon::bch::Vec3 editedBonePoint(std::size_t boneIndex) {
    const auto& bone = gScene->bones()[boneIndex];
    promptemon::bch::Vec3 p{bone.bindGlobal.m[12], bone.bindGlobal.m[13], bone.bindGlobal.m[14]};
    return eTransform(accumulatedBoneDelta(boneIndex), p);
}

void drawEditorScene() {
    if (!gRenderWindow || !gGL) return;
    RECT rc{};
    GetClientRect(gRenderWindow, &rc);
    setupProjection(std::max(1L, rc.right - rc.left), std::max(1L, rc.bottom - rc.top));
    glClearColor(0.118f, 0.118f, 0.118f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!gScene) { SwapBuffers(gDC); return; }

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -3.25f / std::max(0.35f, gZoom));
    glRotatef(gPitch, 1, 0, 0);
    glRotatef(gYaw, 0, 1, 0);
    glScalef(gFitScale, gFitScale, gFitScale);
    glTranslatef(-gModelCenter.x, -gModelCenter.y, -gModelCenter.z);

    const auto matrices = editorSkinMatrices();
    const auto& meshEdits = currentMeshEdits();
    for (std::size_t mi = 0; mi < gScene->meshes().size(); ++mi) {
        if (mi < meshEdits.size() && !meshEdits[mi].visible) continue;
        const auto& mesh = gScene->meshes()[mi];
        const auto* texture = gScene->textureForMaterial(mesh.materialId);
        const Material* material = mesh.materialId < gScene->materials().size() ? &gScene->materials()[mesh.materialId] : nullptr;
        GLuint tex = 0;
        if (texture) {
            const auto it = gGlTextures.find(texture->name);
            if (it != gGlTextures.end()) tex = it->second;
        }
        if (tex) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex); glColor4f(1,1,1,1); }
        else { glDisable(GL_TEXTURE_2D); glColor4f(.76f,.76f,.76f,1); }
        const UvFix* fix = uvFixForMaterial(mesh.materialId);
        const auto pivot = meshCenter(mesh, matrices);
        const MeshEdit edit = mi < meshEdits.size() ? meshEdits[mi] : MeshEdit{};
        glBegin(GL_TRIANGLES);
        for (const auto& vertex : mesh.vertices) {
            auto p = gScene->skin(vertex, matrices);
            p = applyMeshEdit(p, edit, pivot);
            if (tex) {
                const auto uv = adjustedUv(material, vertex.uv, fix);
                glTexCoord2f(uv.x, 1.0f - uv.y);
            }
            glVertex3f(p.x, p.y, p.z);
        }
        glEnd();
    }

    if (gEditorMode == EditorMode::Rig && !gScene->bones().empty()) {
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_DEPTH_TEST);
        glLineWidth(2.0f);
        glBegin(GL_LINES);
        for (std::size_t i = 0; i < gScene->bones().size(); ++i) {
            const int parent = gScene->bones()[i].parent;
            if (parent < 0 || static_cast<std::size_t>(parent) >= gScene->bones().size()) continue;
            const auto a = editedBonePoint(static_cast<std::size_t>(parent));
            const auto b = editedBonePoint(i);
            if (i == gEditorBone || static_cast<std::size_t>(parent) == gEditorBone) glColor3f(.95f,.95f,.95f);
            else glColor3f(.58f,.58f,.58f);
            glVertex3f(a.x,a.y,a.z); glVertex3f(b.x,b.y,b.z);
        }
        glEnd();
        glPointSize(6.0f);
        glBegin(GL_POINTS);
        for (std::size_t i = 0; i < gScene->bones().size(); ++i) {
            const auto p = editedBonePoint(i);
            if (i == gEditorBone) glColor3f(1,1,1); else glColor3f(.55f,.55f,.55f);
            glVertex3f(p.x,p.y,p.z);
        }
        glEnd();
        glEnable(GL_DEPTH_TEST);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    SwapBuffers(gDC);
}

void editorTick() {
    const auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - gLastTick).count();
    gLastTick = now;
    dt = std::clamp(dt, 0.0f, 0.1f);
    if (gCurrent && !gAnimationPaused) {
        gFrame += dt * 30.0f;
        if (gCurrent->frames > 0 && gFrame > gCurrent->frames) {
            if (gCurrent->loop) gFrame = std::fmod(gFrame, gCurrent->frames);
            else { gFrame = gCurrent->frames; gAnimationPaused = true; }
        }
    }
    drawEditorScene();
}

std::wstring chooseImage(HWND owner) {
    wchar_t buffer[32768]{};
    static const wchar_t filter[] =
        L"Images (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0All files (*.*)\0*.*\0\0";
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = static_cast<DWORD>(std::size(buffer));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = L"Load image layer";
    return GetOpenFileNameW(&dialog) ? std::wstring(buffer) : std::wstring{};
}

int encoderClsid(const WCHAR* mime, CLSID* clsid) {
    UINT count = 0, bytes = 0;
    Gdiplus::GetImageEncodersSize(&count, &bytes);
    if (!bytes) return -1;
    std::vector<BYTE> data(bytes);
    auto* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(data.data());
    Gdiplus::GetImageEncoders(count, bytes, info);
    for (UINT i = 0; i < count; ++i) {
        if (wcscmp(info[i].MimeType, mime) == 0) { *clsid = info[i].Clsid; return static_cast<int>(i); }
    }
    return -1;
}

std::unique_ptr<Bitmap> textureBitmap(const promptemon::bch::Texture& texture) {
    if (texture.width <= 0 || texture.height <= 0 ||
        texture.rgba.size() < static_cast<std::size_t>(texture.width) * texture.height * 4) return {};
    auto bitmap = std::make_unique<Bitmap>(texture.width, texture.height, PixelFormat32bppARGB);
    Gdiplus::BitmapData data{};
    Rect lockRect(0, 0, texture.width, texture.height);
    if (bitmap->LockBits(&lockRect, ImageLockModeWrite, PixelFormat32bppARGB, &data) != Gdiplus::Ok) return {};
    for (int y = 0; y < texture.height; ++y) {
        auto* dst = static_cast<BYTE*>(data.Scan0) + y * data.Stride;
        for (int x = 0; x < texture.width; ++x) {
            const std::size_t src = (static_cast<std::size_t>(y) * texture.width + x) * 4;
            dst[x * 4 + 0] = texture.rgba[src + 2];
            dst[x * 4 + 1] = texture.rgba[src + 1];
            dst[x * 4 + 2] = texture.rgba[src + 0];
            dst[x * 4 + 3] = texture.rgba[src + 3];
        }
    }
    bitmap->UnlockBits(&data);
    return bitmap;
}

void ensureLayerBitmap() {
    ImageLayerState* layer = currentLayer();
    const std::wstring path = layer ? layer->path : std::wstring{};
    if (path == gLayerBitmapPath && (path.empty() || gLayerBitmap)) return;
    gLayerBitmap.reset();
    gLayerBitmapPath = path;
    if (!path.empty()) {
        auto image = std::make_unique<Bitmap>(path.c_str());
        if (image->GetLastStatus() == Gdiplus::Ok) gLayerBitmap = std::move(image);
    }
}

void drawImageLayer(Graphics& graphics, int targetW, int targetH, const ImageLayerState& layer) {
    ensureLayerBitmap();
    if (!layer.visible || !gLayerBitmap || targetW <= 0 || targetH <= 0) return;
    const float iw = static_cast<float>(gLayerBitmap->GetWidth());
    const float ih = static_cast<float>(gLayerBitmap->GetHeight());
    if (iw <= 0 || ih <= 0) return;
    const float sx = std::clamp(layer.cropX, 0.0f, 0.95f) * iw;
    const float sy = std::clamp(layer.cropY, 0.0f, 0.95f) * ih;
    const float sw = std::max(1.0f, std::min(layer.cropW, 1.0f - layer.cropX) * iw);
    const float sh = std::max(1.0f, std::min(layer.cropH, 1.0f - layer.cropY) * ih);
    const float fit = std::min(static_cast<float>(targetW) / sw, static_cast<float>(targetH) / sh);
    const float dw = sw * fit * std::max(0.01f, layer.scale);
    const float dh = sh * fit * std::max(0.01f, layer.scale);
    const float cx = (0.5f + layer.offsetU) * targetW;
    const float cy = (0.5f + layer.offsetV) * targetH;
    const auto state = graphics.Save();
    graphics.TranslateTransform(cx, cy);
    graphics.RotateTransform(layer.rotation * 180.0f / PI_F);
    graphics.DrawImage(gLayerBitmap.get(), RectF(-dw * 0.5f, -dh * 0.5f, dw, dh),
                       sx, sy, sw, sh, Gdiplus::UnitPixel);
    graphics.Restore(state);
}

std::unique_ptr<Bitmap> compositeTextureBitmap() {
    const auto* texture = currentTexture();
    if (!texture) return {};
    auto base = textureBitmap(*texture);
    if (!base) return {};
    ImageLayerState* layer = currentLayer();
    if (layer && !layer->path.empty() && layer->visible) {
        Graphics graphics(base.get());
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        drawImageLayer(graphics, texture->width, texture->height, *layer);
    }
    return base;
}

std::vector<std::uint8_t> bitmapToRgba(Bitmap& bitmap) {
    const int w = static_cast<int>(bitmap.GetWidth());
    const int h = static_cast<int>(bitmap.GetHeight());
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4);
    Gdiplus::BitmapData data{};
    Rect lockRect(0,0,w,h);
    if (bitmap.LockBits(&lockRect, ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) return {};
    for (int y = 0; y < h; ++y) {
        const auto* src = static_cast<const BYTE*>(data.Scan0) + y * data.Stride;
        for (int x = 0; x < w; ++x) {
            const std::size_t dst = (static_cast<std::size_t>(y) * w + x) * 4;
            rgba[dst + 0] = src[x * 4 + 2];
            rgba[dst + 1] = src[x * 4 + 1];
            rgba[dst + 2] = src[x * 4 + 0];
            rgba[dst + 3] = src[x * 4 + 3];
        }
    }
    bitmap.UnlockBits(&data);
    return rgba;
}

void refreshEditedTexture() {
    if (!gScene || !gGL) return;
    const auto* texture = currentTexture();
    if (!texture) return;
    auto composite = compositeTextureBitmap();
    if (!composite) return;
    auto rgba = bitmapToRgba(*composite);
    if (rgba.empty()) return;
    const auto it = gGlTextures.find(texture->name);
    if (it == gGlTextures.end()) return;
    glBindTexture(GL_TEXTURE_2D, it->second);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture->width, texture->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void restoreTextureUpload() {
    const auto* texture = currentTexture();
    if (!texture || !gGL) return;
    const auto it = gGlTextures.find(texture->name);
    if (it == gGlTextures.end()) return;
    glBindTexture(GL_TEXTURE_2D, it->second);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture->width, texture->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture->rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

std::filesystem::path editorAssetDirectory() {
    std::filesystem::path root = gExportRoot / "edited";
    std::string name = gScene && !gScene->modelName().empty() ? gScene->modelName() : "model";
    for (char& c : name) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) c = '_';
    return root / name;
}

bool savePng(Bitmap& bitmap, const std::filesystem::path& path) {
    CLSID encoder{};
    if (encoderClsid(L"image/png", &encoder) < 0) return false;
    std::filesystem::create_directories(path.parent_path());
    return bitmap.Save(path.wstring().c_str(), &encoder, nullptr) == Gdiplus::Ok;
}

void exportEditedTexture() {
    auto bitmap = compositeTextureBitmap();
    const auto* texture = currentTexture();
    if (!bitmap || !texture) return;
    std::string name = texture->name.empty() ? "texture" : texture->name;
    for (char& c : name) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) c = '_';
    const auto out = editorAssetDirectory() / (name + ".png");
    if (!savePng(*bitmap, out)) throw std::runtime_error("Could not export PNG texture");
    setStatus("Texture exported: " + out.string());
    ShellExecuteW(gWindow, L"open", out.parent_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void exportUvTemplate() {
    const auto* texture = currentTexture();
    if (!texture || !gScene || gScene->materials().empty()) return;
    const int w = std::max(8, texture->width);
    const int h = std::max(8, texture->height);
    Bitmap bitmap(w, h, PixelFormat32bppARGB);
    Graphics graphics(&bitmap);
    graphics.Clear(Color(0, 0, 0, 0));
    Pen shadow(Color(210, 0, 0, 0), 3.0f);
    Pen wire(Color(255, 240, 240, 240), 1.0f);
    const std::size_t materialIndex = std::min(gEditorMaterial, gScene->materials().size() - 1);
    const Material* material = &gScene->materials()[materialIndex];
    const UvFix* fix = uvFixForMaterial(materialIndex);
    for (const auto& mesh : gScene->meshes()) {
        if (mesh.materialId != materialIndex) continue;
        for (std::size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
            Gdiplus::PointF pts[4];
            for (int k = 0; k < 3; ++k) {
                const auto uv = adjustedUv(material, mesh.vertices[i + static_cast<std::size_t>(k)].uv, fix);
                pts[k] = Gdiplus::PointF(uv.x * w, (1.0f - uv.y) * h);
            }
            pts[3] = pts[0];
            graphics.DrawLines(&shadow, pts, 4);
            graphics.DrawLines(&wire, pts, 4);
        }
    }
    const auto out = editorAssetDirectory() / "uv-template.png";
    if (!savePng(bitmap, out)) throw std::runtime_error("Could not export UV template");
    setStatus("UV template exported: " + out.string());
    ShellExecuteW(gWindow, L"open", out.parent_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void eSection(HDC dc, const RECT& panel, int& y, const char* title) {
    RECT row{panel.left + 8, y, panel.right - 8, y + 26};
    eFill(dc, row, E_PANEL_ALT);
    RECT arrow{row.left + 6, row.top, row.left + 20, row.bottom};
    eText(dc, "v", arrow, gEditorTinyFont, E_MUTED, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT label{arrow.right + 2, row.top, row.right - 6, row.bottom};
    eText(dc, title, label, gEditorSectionFont, E_TEXT);
    y += 30;
}

void eProp(HDC dc, const RECT& panel, int& y, const std::string& key, const std::string& value) {
    RECT a{panel.left + 14, y, panel.left + 112, y + 23};
    RECT b{a.right, y, panel.right - 14, y + 23};
    eText(dc, key, a, gEditorTinyFont, E_MUTED);
    eText(dc, value, b, gEditorTinyFont, E_TEXT);
    y += 24;
}

RECT rightRow(const RECT& panel, int y) { return {panel.left + 12, y, panel.right - 12, y + 24}; }
RECT minusBox(const RECT& panel, int y) { return {panel.right - 102, y, panel.right - 78, y + 23}; }
RECT valueBox(const RECT& panel, int y) { return {panel.right - 76, y, panel.right - 38, y + 23}; }
RECT plusBox(const RECT& panel, int y) { return {panel.right - 36, y, panel.right - 12, y + 23}; }

void eAdjust(HDC dc, const RECT& panel, int y, const char* label, float value, bool degrees = false) {
    RECT name{panel.left + 14, y, panel.right - 108, y + 23};
    eText(dc, label, name, gEditorTinyFont, E_MUTED);
    const auto minus = minusBox(panel, y), val = valueBox(panel, y), plus = plusBox(panel, y);
    eBox(dc, minus, E_FIELD); eBox(dc, val, E_PANEL_ALT); eBox(dc, plus, E_FIELD);
    eText(dc, "-", minus, gEditorTinyFont, E_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    std::ostringstream out;
    if (degrees) out << std::fixed << std::setprecision(0) << value * 180.0f / PI_F;
    else out << std::fixed << std::setprecision(2) << value;
    eText(dc, out.str(), val, gEditorTinyFont, E_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    eText(dc, "+", plus, gEditorTinyFont, E_TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void paintEditorTop(HDC dc, const EditorLayout& l) {
    eFill(dc, l.top, E_PANEL);
    RECT title{12, 0, 280, EDIT_TOP_H};
    eText(dc, "Smoke Detector", title, gEditorTitleFont, E_TEXT);
    eButton(dc, l.open, "Open ROM", false, !gDumping);
    eButton(dc, l.exportOne, "Export", false, !gDumping && gSelectedItem < gItems.size());
    eButton(dc, l.dump, "Dump all", false, !gDumping && !gItems.empty());
    eLine(dc, 0, l.top.bottom - 1, l.top.right, l.top.bottom - 1, E_BORDER);

    eFill(dc, l.modes, E_SURFACE);
    static const char* labels[] = {"Browse", "Model", "Rig", "Texture", "Animate"};
    for (int i = 0; i < 5; ++i) {
        const bool active = static_cast<int>(gEditorMode) == i;
        eButton(dc, l.modeButtons[static_cast<std::size_t>(i)], labels[i], active, i == 0 || gScene != nullptr);
    }
    eLine(dc, 0, l.modes.bottom - 1, l.modes.right, l.modes.bottom - 1, E_BORDER);
}

void paintEditorLeft(HDC dc, const EditorLayout& l) {
    eBox(dc, l.left, E_PANEL);
    RECT heading{l.left.left + 10, l.left.top + 5, l.left.right - 10, l.left.top + 33};
    eText(dc, "Resources", heading, gEditorSectionFont, E_TEXT);
    RECT count{l.left.right - 62, heading.top, l.left.right - 10, heading.bottom};
    eText(dc, std::to_string(gFiltered.size()), count, gEditorTinyFont, E_MUTED,
          DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    eBox(dc, l.search, E_FIELD);

    HRGN clip = CreateRectRgn(l.tree.left, l.tree.top, l.tree.right, l.tree.bottom);
    SelectClipRgn(dc, clip);
    const int visible = std::max(1, eh(l.tree) / EDIT_ROW_H);
    const std::size_t maxScroll = gTreeRows.size() > static_cast<std::size_t>(visible) ? gTreeRows.size() - visible : 0;
    gTreeScroll = std::min(gTreeScroll, maxScroll);
    for (int i = 0; i <= visible; ++i) {
        const std::size_t rowIndex = gTreeScroll + static_cast<std::size_t>(i);
        if (rowIndex >= gTreeRows.size()) break;
        const auto& row = gTreeRows[rowIndex];
        RECT rr{l.tree.left, l.tree.top + i * EDIT_ROW_H, l.tree.right, l.tree.top + (i + 1) * EDIT_ROW_H};
        const bool selected = !row.folder && row.itemIndex == gSelectedItem;
        if (selected) eFill(dc, rr, E_ACTIVE);
        const int indent = std::min(70, row.depth * 12);
        RECT icon{rr.left + 5 + indent, rr.top, rr.left + 25 + indent, rr.bottom};
        const std::string glyph = row.folder
            ? (gCollapsedFolders.count(row.key) ? ">" : "v")
            : badgeFor(gItems[row.itemIndex].category);
        eText(dc, glyph, icon, gEditorTinyFont, row.folder ? E_MUTED : E_TEXT,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT label{icon.right + 1, rr.top, rr.right - 5, rr.bottom};
        eText(dc, row.label, label, gEditorSmallFont, selected ? E_TEXT : (row.folder ? RGB(190,190,190) : E_TEXT));
        eLine(dc, rr.left, rr.bottom - 1, rr.right, rr.bottom - 1, RGB(45,45,45));
    }
    SelectClipRgn(dc, nullptr);
    DeleteObject(clip);
}

RECT fitInside(const RECT& outer, int w, int h) {
    if (w <= 0 || h <= 0 || ew(outer) <= 0 || eh(outer) <= 0) return outer;
    const double scale = std::min(static_cast<double>(ew(outer)) / w, static_cast<double>(eh(outer)) / h);
    const int dw = std::max(1, static_cast<int>(w * scale));
    const int dh = std::max(1, static_cast<int>(h * scale));
    const int x = outer.left + (ew(outer) - dw) / 2;
    const int y = outer.top + (eh(outer) - dh) / 2;
    return {x,y,x+dw,y+dh};
}

void drawBitmapToHdc(HDC dc, Bitmap& bitmap, const RECT& target) {
    Graphics graphics(dc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    graphics.DrawImage(&bitmap, Rect(target.left, target.top, ew(target), eh(target)),
                       0, 0, static_cast<INT>(bitmap.GetWidth()), static_cast<INT>(bitmap.GetHeight()), Gdiplus::UnitPixel);
}

void paintUvCanvas(HDC dc, const EditorLayout& l) {
    if (IsRectEmpty(&l.uvCanvas)) return;
    eBox(dc, l.uvCanvas, E_SURFACE);
    RECT heading{l.uvCanvas.left + 8, l.uvCanvas.top + 5, l.uvCanvas.right - 8, l.uvCanvas.top + 28};
    eText(dc, "Texture / UV", heading, gEditorSectionFont, E_TEXT);
    RECT canvas{l.uvCanvas.left + 8, l.uvCanvas.top + 32, l.uvCanvas.right - 8, l.uvCanvas.bottom - 8};
    eFill(dc, canvas, RGB(27,27,27));
    const auto* texture = currentTexture();
    if (!texture) {
        eText(dc, "No texture on this material", canvas, gEditorSmallFont, E_MUTED,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return;
    }
    auto composite = compositeTextureBitmap();
    if (!composite) return;
    const RECT imageRect = fitInside(canvas, texture->width, texture->height);
    drawBitmapToHdc(dc, *composite, imageRect);

    if (!gScene || gScene->materials().empty()) return;
    const std::size_t materialIndex = std::min(gEditorMaterial, gScene->materials().size() - 1);
    const Material* material = &gScene->materials()[materialIndex];
    const UvFix* fix = uvFixForMaterial(materialIndex);
    HPEN shadow = CreatePen(PS_SOLID, 3, RGB(0,0,0));
    HPEN wire = CreatePen(PS_SOLID, 1, E_WIRE);
    for (const auto& mesh : gScene->meshes()) {
        if (mesh.materialId != materialIndex) continue;
        for (std::size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
            POINT pts[4]{};
            for (int k = 0; k < 3; ++k) {
                const auto uv = adjustedUv(material, mesh.vertices[i + static_cast<std::size_t>(k)].uv, fix);
                pts[k].x = imageRect.left + static_cast<LONG>(uv.x * ew(imageRect));
                pts[k].y = imageRect.top + static_cast<LONG>((1.0f - uv.y) * eh(imageRect));
            }
            pts[3] = pts[0];
            HGDIOBJ old = SelectObject(dc, shadow); Polyline(dc, pts, 4); SelectObject(dc, wire); Polyline(dc, pts, 4); SelectObject(dc, old);
        }
    }
    DeleteObject(shadow); DeleteObject(wire);
}

void paintCenterHeader(HDC dc, const EditorLayout& l) {
    eBox(dc, l.center, E_PANEL);
    RECT heading{l.center.left + 10, l.center.top, l.center.right - 10, l.center.top + 28};
    std::string name = gScene ? gScene->modelName() : "Preview";
    if (name.empty()) name = "Preview";
    eText(dc, name, heading, gEditorSectionFont, E_TEXT);
    RECT mode{l.center.right - 130, l.center.top, l.center.right - 10, l.center.top + 28};
    eText(dc, editorModeName(gEditorMode), mode, gEditorTinyFont, E_MUTED,
          DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

void paintTimeline(HDC dc, const EditorLayout& l) {
    if (IsRectEmpty(&l.timeline)) return;
    eBox(dc, l.timeline, E_PANEL_ALT);
    RECT title{l.timeline.left + 10, l.timeline.top + 4, l.timeline.left + 100, l.timeline.top + 26};
    eText(dc, "Timeline", title, gEditorSectionFont, E_TEXT);
    const float frames = gCurrent ? std::max(1.0f, gCurrent->frames) : 1.0f;
    RECT track{l.timeline.left + 12, l.timeline.top + 39, l.timeline.right - 12, l.timeline.top + 68};
    eFill(dc, track, E_SURFACE);
    for (int i = 0; i <= 10; ++i) {
        const int x = track.left + ew(track) * i / 10;
        eLine(dc, x, track.top, x, track.bottom, i % 5 == 0 ? E_MUTED : E_BORDER);
    }
    const int cursor = track.left + static_cast<int>(std::clamp(gFrame / frames, 0.0f, 1.0f) * ew(track));
    eLine(dc, cursor, track.top - 5, cursor, track.bottom + 5, RGB(220,220,220));
    RECT value{track.left, track.bottom + 6, track.right, track.bottom + 27};
    std::ostringstream label;
    label << std::fixed << std::setprecision(1) << gFrame << " / " << frames << " frames";
    eText(dc, label.str(), value, gEditorTinyFont, E_MUTED);
}

void paintBrowseInspector(HDC dc, const EditorLayout& l) {
    int y = l.right.top + 10;
    if (gSelectedItem < gItems.size()) {
        const auto& item = gItems[gSelectedItem];
        eSection(dc, l.right, y, "Resource");
        eProp(dc, l.right, y, "Name", item.displayName.empty() ? item.id : item.displayName);
        eProp(dc, l.right, y, "Type", filterName(item.category));
        eProp(dc, l.right, y, "Size", item.size ? humanBytes(item.size) : "unknown");
    }
    if (gScene) {
        y += 5;
        eSection(dc, l.right, y, "Geometry");
        eProp(dc, l.right, y, "Meshes", std::to_string(gScene->meshes().size()));
        eProp(dc, l.right, y, "Bones", std::to_string(gScene->bones().size()));
        eProp(dc, l.right, y, "Materials", std::to_string(gScene->materials().size()));
        eProp(dc, l.right, y, "Textures", std::to_string(gScene->textures().size()));
        eProp(dc, l.right, y, "Animations", std::to_string(gScene->animations().size()));
    }
}

void paintMeshInspector(HDC dc, const EditorLayout& l) {
    if (!gScene) return;
    ensureEditorState();
    int y = l.right.top + 10;
    eSection(dc, l.right, y, "Meshes");
    const int listRows = std::min<int>(8, std::max<int>(1, static_cast<int>(gScene->meshes().size())));
    const std::size_t maxScroll = gScene->meshes().size() > static_cast<std::size_t>(listRows) ? gScene->meshes().size() - listRows : 0;
    gEditorMeshScroll = std::min(gEditorMeshScroll, maxScroll);
    for (int i = 0; i < listRows; ++i) {
        const std::size_t idx = gEditorMeshScroll + static_cast<std::size_t>(i);
        if (idx >= gScene->meshes().size()) break;
        RECT row{l.right.left + 10, y, l.right.right - 10, y + 24};
        if (idx == gEditorMesh) eFill(dc, row, E_ACTIVE);
        RECT vis{row.left + 4, row.top, row.left + 26, row.bottom};
        eText(dc, currentMeshEdits()[idx].visible ? "o" : "-", vis, gEditorTinyFont, E_MUTED,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        RECT name{vis.right + 2, row.top, row.right - 4, row.bottom};
        eText(dc, gScene->meshes()[idx].name.empty() ? "mesh " + std::to_string(idx) : gScene->meshes()[idx].name,
              name, gEditorSmallFont, E_TEXT);
        y += 25;
    }
    y += 5;
    eSection(dc, l.right, y, "Transform");
    if (gEditorMesh < currentMeshEdits().size()) {
        const auto& e = currentMeshEdits()[gEditorMesh];
        eAdjust(dc,l.right,y,"Position X",e.tx); y+=25;
        eAdjust(dc,l.right,y,"Position Y",e.ty); y+=25;
        eAdjust(dc,l.right,y,"Position Z",e.tz); y+=25;
        eAdjust(dc,l.right,y,"Rotation X",e.rx,true); y+=25;
        eAdjust(dc,l.right,y,"Rotation Y",e.ry,true); y+=25;
        eAdjust(dc,l.right,y,"Rotation Z",e.rz,true); y+=25;
        eAdjust(dc,l.right,y,"Scale",e.sx); y+=28;
        RECT reset{l.right.left+12,y,l.right.right-12,y+25}; eButton(dc,reset,"Reset mesh");
    }
}

void paintRigInspector(HDC dc, const EditorLayout& l) {
    if (!gScene) return;
    ensureEditorState();
    int y = l.right.top + 10;
    eSection(dc, l.right, y, "Skeleton");
    const int listRows = std::min<int>(9, std::max<int>(1, static_cast<int>(gScene->bones().size())));
    const std::size_t maxScroll = gScene->bones().size() > static_cast<std::size_t>(listRows) ? gScene->bones().size() - listRows : 0;
    gEditorBoneScroll = std::min(gEditorBoneScroll, maxScroll);
    for (int i = 0; i < listRows; ++i) {
        const std::size_t idx = gEditorBoneScroll + static_cast<std::size_t>(i);
        if (idx >= gScene->bones().size()) break;
        RECT row{l.right.left + 10, y, l.right.right - 10, y + 24};
        if (idx == gEditorBone) eFill(dc,row,E_ACTIVE);
        RECT icon{row.left+4,row.top,row.left+28,row.bottom}; eText(dc,"J",icon,gEditorTinyFont,E_MUTED,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        RECT name{icon.right,row.top,row.right-4,row.bottom};
        eText(dc,gScene->bones()[idx].name.empty()?"joint "+std::to_string(idx):gScene->bones()[idx].name,name,gEditorSmallFont,E_TEXT);
        y += 25;
    }
    y += 5;
    eSection(dc,l.right,y,"Pose joint");
    if (gEditorBone < currentBoneEdits().size()) {
        const auto& e=currentBoneEdits()[gEditorBone];
        eAdjust(dc,l.right,y,"Position X",e.tx); y+=25;
        eAdjust(dc,l.right,y,"Position Y",e.ty); y+=25;
        eAdjust(dc,l.right,y,"Position Z",e.tz); y+=25;
        eAdjust(dc,l.right,y,"Rotation X",e.rx,true); y+=25;
        eAdjust(dc,l.right,y,"Rotation Y",e.ry,true); y+=25;
        eAdjust(dc,l.right,y,"Rotation Z",e.rz,true); y+=28;
        RECT reset{l.right.left+12,y,l.right.right-12,y+25}; eButton(dc,reset,"Reset joint");
    }
}

void paintTextureInspector(HDC dc, const EditorLayout& l) {
    if (!gScene || gScene->materials().empty()) return;
    ensureEditorState();
    int y=l.right.top+10;
    eSection(dc,l.right,y,"Material");
    RECT prev{l.right.left+12,y,l.right.left+38,y+25};
    RECT next{l.right.right-38,y,l.right.right-12,y+25};
    RECT name{prev.right+4,y,next.left-4,y+25};
    eButton(dc,prev,"<"); eButton(dc,next,">"); eBox(dc,name,E_PANEL_ALT);
    const auto& material=gScene->materials()[gEditorMaterial];
    eText(dc,material.name.empty()?"material "+std::to_string(gEditorMaterial):material.name,name,gEditorSmallFont,E_TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    y+=31;
    eProp(dc,l.right,y,"Texture",material.textureName.empty()?"none":material.textureName);
    y+=4;
    eSection(dc,l.right,y,"UV mapping");
    auto& fixes=currentUvFixes();
    if (gEditorMaterial<fixes.size()) {
        auto& f=fixes[gEditorMaterial];
        RECT transform=rightRow(l.right,y); eBox(dc,transform,E_FIELD); eText(dc,"Transform: "+std::string(transformName(f.transformType)),transform,gEditorTinyFont,E_TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE); y+=28;
        eAdjust(dc,l.right,y,"Offset U",f.offsetU); y+=25;
        eAdjust(dc,l.right,y,"Offset V",f.offsetV); y+=25;
        eAdjust(dc,l.right,y,"Scale U",f.scaleU); y+=25;
        eAdjust(dc,l.right,y,"Scale V",f.scaleV); y+=25;
        eAdjust(dc,l.right,y,"Rotation",f.rotation,true); y+=29;
    }
    eSection(dc,l.right,y,"Image layer");
    RECT load{l.right.left+12,y,l.right.left+104,y+25};
    RECT templ{load.right+4,y,load.right+96,y+25};
    RECT save{templ.right+4,y,l.right.right-12,y+25};
    eButton(dc,load,"Load image"); eButton(dc,templ,"UV template"); eButton(dc,save,"Export PNG"); y+=31;
    if (auto* layer=currentLayer()) {
        eAdjust(dc,l.right,y,"Layer X",layer->offsetU); y+=25;
        eAdjust(dc,l.right,y,"Layer Y",layer->offsetV); y+=25;
        eAdjust(dc,l.right,y,"Layer scale",layer->scale); y+=25;
        eAdjust(dc,l.right,y,"Layer rotate",layer->rotation,true); y+=25;
        eAdjust(dc,l.right,y,"Crop X",layer->cropX); y+=25;
        eAdjust(dc,l.right,y,"Crop Y",layer->cropY); y+=25;
        eAdjust(dc,l.right,y,"Crop W",layer->cropW); y+=25;
        eAdjust(dc,l.right,y,"Crop H",layer->cropH); y+=28;
        RECT clear{l.right.left+12,y,l.right.right-12,y+25}; eButton(dc,clear,"Remove image layer",false,!layer->path.empty());
    }
}

void paintAnimateInspector(HDC dc, const EditorLayout& l) {
    if (!gScene) return;
    int y=l.right.top+10;
    eSection(dc,l.right,y,"Playback");
    RECT playButton{l.right.left+12,y,l.right.right-12,y+27}; eButton(dc,playButton,gAnimationPaused?"Play":"Pause",gCurrent!=nullptr,gCurrent!=nullptr); y+=33;
    eProp(dc,l.right,y,"Clip",gCurrent?gCurrent->name:"Base pose");
    eProp(dc,l.right,y,"Frame",std::to_string(static_cast<int>(gFrame)));
    y+=4;
    eSection(dc,l.right,y,"Animations");
    const int visible=std::max(1,(l.right.bottom-y-8)/EDIT_ROW_H);
    const std::size_t total=gScene->animations().size()+1;
    const std::size_t maxScroll=total>static_cast<std::size_t>(visible)?total-visible:0;
    gAnimationScroll=std::min(gAnimationScroll,maxScroll);
    for(int i=0;i<visible;++i){
        const std::size_t idx=gAnimationScroll+static_cast<std::size_t>(i); if(idx>=total)break;
        RECT row{l.right.left+10,y+i*EDIT_ROW_H,l.right.right-10,y+(i+1)*EDIT_ROW_H};
        bool active=false;std::string label;
        if(idx==0){label="Base pose";active=gCurrent==nullptr;}else{const auto&a=gScene->animations()[idx-1];label=a.name;active=gCurrent==&a;}
        if(active)eFill(dc,row,E_ACTIVE); eText(dc,label,row,gEditorSmallFont,active?E_TEXT:RGB(198,198,198));
    }
}

void paintEditorRight(HDC dc, const EditorLayout& l) {
    eBox(dc,l.right,E_PANEL);
    switch(gEditorMode){
        case EditorMode::Model: paintMeshInspector(dc,l); break;
        case EditorMode::Rig: paintRigInspector(dc,l); break;
        case EditorMode::Texture: paintTextureInspector(dc,l); break;
        case EditorMode::Animate: paintAnimateInspector(dc,l); break;
        default: paintBrowseInspector(dc,l); break;
    }
}

void paintEditorStatus(HDC dc,const EditorLayout&l){
    eFill(dc,l.status,E_PANEL);RECT textRect{8,l.status.top,l.status.right-8,l.status.bottom};eText(dc,gStatus,textRect,gEditorTinyFont,E_MUTED);
    if(gProgressTotal){const double ratio=std::min(1.0,static_cast<double>(gProgressCurrent)/static_cast<double>(std::max<std::size_t>(1,gProgressTotal)));RECT progress{0,l.status.bottom-2,static_cast<LONG>(ew(l.status)*ratio),l.status.bottom};eFill(dc,progress,RGB(176,176,176));}
}

void paintEditor(HWND hwnd){
    PAINTSTRUCT ps{};HDC target=BeginPaint(hwnd,&ps);RECT client{};GetClientRect(hwnd,&client);
    HDC dc=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,std::max(1,ew(client)),std::max(1,eh(client)));HGDIOBJ old=SelectObject(dc,bitmap);
    eFill(dc,client,E_BG);const auto l=editorLayout(hwnd);paintEditorTop(dc,l);paintEditorLeft(dc,l);paintCenterHeader(dc,l);if(gEditorMode==EditorMode::Texture)paintUvCanvas(dc,l);if(gEditorMode==EditorMode::Animate)paintTimeline(dc,l);paintEditorRight(dc,l);paintEditorStatus(dc,l);
    BitBlt(target,0,0,ew(client),eh(client),dc,0,0,SRCCOPY);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);EndPaint(hwnd,&ps);
}

void editorLayoutControls(HWND hwnd){
    const auto l=editorLayout(hwnd);
    gSearch=gEditorSearch;
    if(gEditorSearch)MoveWindow(gEditorSearch,l.search.left+6,l.search.top+2,std::max(10,ew(l.search)-12),eh(l.search)-4,TRUE);
    if(gRenderWindow){MoveWindow(gRenderWindow,l.viewport.left,l.viewport.top,std::max(1,ew(l.viewport)),std::max(1,eh(l.viewport)),TRUE);ShowWindow(gRenderWindow,gScene?SW_SHOW:SW_HIDE);}
}

float editStep(float base){return (GetKeyState(VK_SHIFT)&0x8000)?base*10.0f:base;}

bool clickAdjust(const RECT& panel,int y,int x,int mouseY,float&value,float step,float minValue=-100000.0f,float maxValue=100000.0f){
    if(eHit(minusBox(panel,y),x,mouseY)){value=std::clamp(value-editStep(step),minValue,maxValue);return true;}
    if(eHit(plusBox(panel,y),x,mouseY)){value=std::clamp(value+editStep(step),minValue,maxValue);return true;}
    return false;
}

void handleMeshInspectorClick(const EditorLayout&l,int x,int y){
    if(!gScene)return;ensureEditorState();int cy=l.right.top+10+30;const int rows=std::min<int>(8,std::max<int>(1,static_cast<int>(gScene->meshes().size())));
    for(int i=0;i<rows;++i){const std::size_t idx=gEditorMeshScroll+static_cast<std::size_t>(i);if(idx>=gScene->meshes().size())break;RECT row{l.right.left+10,cy,l.right.right-10,cy+24};if(eHit(row,x,y)){if(x<row.left+30)currentMeshEdits()[idx].visible=!currentMeshEdits()[idx].visible;else gEditorMesh=idx;InvalidateRect(gWindow,&l.right,FALSE);return;}cy+=25;}
    cy+=5+30;if(gEditorMesh>=currentMeshEdits().size())return;auto&e=currentMeshEdits()[gEditorMesh];
    if(clickAdjust(l.right,cy,x,y,e.tx,.02f)||clickAdjust(l.right,cy+25,x,y,e.ty,.02f)||clickAdjust(l.right,cy+50,x,y,e.tz,.02f)||clickAdjust(l.right,cy+75,x,y,e.rx,PI_F/90.0f)||clickAdjust(l.right,cy+100,x,y,e.ry,PI_F/90.0f)||clickAdjust(l.right,cy+125,x,y,e.rz,PI_F/90.0f)||clickAdjust(l.right,cy+150,x,y,e.sx,.05f,.05f,20.0f)){e.sy=e.sz=e.sx;InvalidateRect(gWindow,&l.right,FALSE);return;}
    RECT reset{l.right.left+12,cy+178,l.right.right-12,cy+203};if(eHit(reset,x,y)){e=MeshEdit{};InvalidateRect(gWindow,&l.right,FALSE);}
}

void handleRigInspectorClick(const EditorLayout&l,int x,int y){
    if(!gScene)return;ensureEditorState();int cy=l.right.top+10+30;const int rows=std::min<int>(9,std::max<int>(1,static_cast<int>(gScene->bones().size())));
    for(int i=0;i<rows;++i){const std::size_t idx=gEditorBoneScroll+static_cast<std::size_t>(i);if(idx>=gScene->bones().size())break;RECT row{l.right.left+10,cy,l.right.right-10,cy+24};if(eHit(row,x,y)){gEditorBone=idx;InvalidateRect(gWindow,&l.right,FALSE);return;}cy+=25;}
    cy+=5+30;if(gEditorBone>=currentBoneEdits().size())return;auto&e=currentBoneEdits()[gEditorBone];
    if(clickAdjust(l.right,cy,x,y,e.tx,.02f)||clickAdjust(l.right,cy+25,x,y,e.ty,.02f)||clickAdjust(l.right,cy+50,x,y,e.tz,.02f)||clickAdjust(l.right,cy+75,x,y,e.rx,PI_F/90.0f)||clickAdjust(l.right,cy+100,x,y,e.ry,PI_F/90.0f)||clickAdjust(l.right,cy+125,x,y,e.rz,PI_F/90.0f)){InvalidateRect(gWindow,&l.right,FALSE);return;}
    RECT reset{l.right.left+12,cy+153,l.right.right-12,cy+178};if(eHit(reset,x,y)){e=BoneEdit{};InvalidateRect(gWindow,&l.right,FALSE);}
}

void handleTextureInspectorClick(const EditorLayout&l,int x,int y){
    if(!gScene||gScene->materials().empty())return;ensureEditorState();int cy=l.right.top+10+30;
    RECT prev{l.right.left+12,cy,l.right.left+38,cy+25};RECT next{l.right.right-38,cy,l.right.right-12,cy+25};
    if(eHit(prev,x,y)||eHit(next,x,y)){restoreTextureUpload();if(eHit(prev,x,y))gEditorMaterial=gEditorMaterial?gEditorMaterial-1:gScene->materials().size()-1;else gEditorMaterial=(gEditorMaterial+1)%gScene->materials().size();gLayerBitmap.reset();gLayerBitmapPath.clear();refreshEditedTexture();InvalidateRect(gWindow,nullptr,FALSE);return;}
    cy+=31+24+4+30;auto&fixes=currentUvFixes();if(gEditorMaterial<fixes.size()){auto&f=fixes[gEditorMaterial];RECT transform=rightRow(l.right,cy);if(eHit(transform,x,y)){f.transformType=f.transformType>=2?-1:f.transformType+1;InvalidateRect(gWindow,nullptr,FALSE);return;}cy+=28;if(clickAdjust(l.right,cy,x,y,f.offsetU,.01f)||clickAdjust(l.right,cy+25,x,y,f.offsetV,.01f)||clickAdjust(l.right,cy+50,x,y,f.scaleU,.02f,.02f,20.0f)||clickAdjust(l.right,cy+75,x,y,f.scaleV,.02f,.02f,20.0f)||clickAdjust(l.right,cy+100,x,y,f.rotation,PI_F/180.0f)){InvalidateRect(gWindow,nullptr,FALSE);return;}cy+=129;}
    cy+=30;RECT load{l.right.left+12,cy,l.right.left+104,cy+25};RECT templ{load.right+4,cy,load.right+96,cy+25};RECT save{templ.right+4,cy,l.right.right-12,cy+25};
    if(eHit(load,x,y)){const auto path=chooseImage(gWindow);if(!path.empty()){auto*layer=currentLayer();layer->path=path;layer->visible=true;gLayerBitmap.reset();gLayerBitmapPath.clear();refreshEditedTexture();InvalidateRect(gWindow,nullptr,FALSE);}return;}
    if(eHit(templ,x,y)){exportUvTemplate();return;}if(eHit(save,x,y)){exportEditedTexture();return;}cy+=31;auto*layer=currentLayer();if(!layer)return;
    bool changed=clickAdjust(l.right,cy,x,y,layer->offsetU,.02f,-2,2)||clickAdjust(l.right,cy+25,x,y,layer->offsetV,.02f,-2,2)||clickAdjust(l.right,cy+50,x,y,layer->scale,.05f,.01f,20)||clickAdjust(l.right,cy+75,x,y,layer->rotation,PI_F/180.0f)||clickAdjust(l.right,cy+100,x,y,layer->cropX,.02f,0,.95f)||clickAdjust(l.right,cy+125,x,y,layer->cropY,.02f,0,.95f)||clickAdjust(l.right,cy+150,x,y,layer->cropW,.02f,.05f,1.0f)||clickAdjust(l.right,cy+175,x,y,layer->cropH,.02f,.05f,1.0f);
    if(changed){layer->cropW=std::min(layer->cropW,1.0f-layer->cropX);layer->cropH=std::min(layer->cropH,1.0f-layer->cropY);refreshEditedTexture();InvalidateRect(gWindow,nullptr,FALSE);return;}
    RECT clear{l.right.left+12,cy+203,l.right.right-12,cy+228};if(eHit(clear,x,y)&&!layer->path.empty()){layer->path.clear();gLayerBitmap.reset();gLayerBitmapPath.clear();restoreTextureUpload();InvalidateRect(gWindow,nullptr,FALSE);}
}

void handleAnimateInspectorClick(const EditorLayout&l,int x,int y){
    if(!gScene)return;int cy=l.right.top+10+30;RECT playButton{l.right.left+12,cy,l.right.right-12,cy+27};if(eHit(playButton,x,y)&&gCurrent){gAnimationPaused=!gAnimationPaused;InvalidateRect(gWindow,&l.right,FALSE);return;}cy+=33+48+4+30;if(y<cy)return;const std::size_t idx=gAnimationScroll+static_cast<std::size_t>((y-cy)/EDIT_ROW_H);if(idx==0){gCurrent=nullptr;gFrame=0;gAnimationPaused=true;}else if(idx-1<gScene->animations().size()){gCurrent=&gScene->animations()[idx-1];gFrame=0;gAnimationPaused=false;}InvalidateRect(gWindow,nullptr,FALSE);
}

void activateEditorItem(std::size_t item){activateItem(item);ensureEditorState();gEditorMesh=gEditorBone=gEditorMaterial=0;gLayerBitmap.reset();gLayerBitmapPath.clear();editorLayoutControls(gWindow);InvalidateRect(gWindow,nullptr,FALSE);}

void editorClick(HWND hwnd,int x,int y){
    const auto l=editorLayout(hwnd);
    if(eHit(l.open,x,y)){const auto path=chooseRom(hwnd);if(!path.empty()){loadRomInput(path);ensureEditorState();editorLayoutControls(hwnd);InvalidateRect(hwnd,nullptr,FALSE);}return;}
    if(eHit(l.exportOne,x,y)&&gSelectedItem<gItems.size()){exportStudioSelected();return;}
    if(eHit(l.dump,x,y)&&!gItems.empty()){dumpAll();return;}
    for(int i=0;i<5;++i){if(eHit(l.modeButtons[static_cast<std::size_t>(i)],x,y)){if(i==0||gScene){gEditorMode=static_cast<EditorMode>(i);editorLayoutControls(hwnd);InvalidateRect(hwnd,nullptr,FALSE);}return;}}
    if(eHit(l.tree,x,y)){const std::size_t row=gTreeScroll+static_cast<std::size_t>((y-l.tree.top)/EDIT_ROW_H);if(row<gTreeRows.size()){const auto item=gTreeRows[row];if(item.folder){if(gCollapsedFolders.count(item.key))gCollapsedFolders.erase(item.key);else gCollapsedFolders.insert(item.key);rebuildTree();InvalidateRect(hwnd,&l.left,FALSE);}else activateEditorItem(item.itemIndex);}return;}
    if(gEditorMode==EditorMode::Model&&eHit(l.right,x,y)){handleMeshInspectorClick(l,x,y);return;}
    if(gEditorMode==EditorMode::Rig&&eHit(l.right,x,y)){handleRigInspectorClick(l,x,y);return;}
    if(gEditorMode==EditorMode::Texture&&eHit(l.right,x,y)){handleTextureInspectorClick(l,x,y);return;}
    if(gEditorMode==EditorMode::Animate){if(eHit(l.right,x,y)){handleAnimateInspectorClick(l,x,y);return;}if(eHit(l.timeline,x,y)&&gCurrent){RECT track{l.timeline.left+12,l.timeline.top+39,l.timeline.right-12,l.timeline.top+68};if(eHit(track,x,y)){const float t=std::clamp(static_cast<float>(x-track.left)/std::max(1,ew(track)),0.0f,1.0f);gFrame=t*std::max(1.0f,gCurrent->frames);gAnimationPaused=true;InvalidateRect(hwnd,nullptr,FALSE);return;}}}
}

LRESULT CALLBACK EditorProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
        case WM_CREATE:
            editorUiResources();applyDarkTitleBar(hwnd);gWindow=hwnd;gEditorSearch=CreateWindowExA(0,"EDIT","",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,10,10,hwnd,controlId(EDIT_SEARCH_ID),GetModuleHandleA(nullptr),nullptr);gSearch=gEditorSearch;if(gEditorSearch){SendMessage(gEditorSearch,WM_SETFONT,reinterpret_cast<WPARAM>(gEditorSmallFont),TRUE);SendMessageA(gEditorSearch,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(7,7));}gRenderWindow=CreateWindowExA(0,"SmokeDetectorRenderModern","",WS_CHILD|WS_CLIPSIBLINGS,0,0,1,1,hwnd,nullptr,GetModuleHandleA(nullptr),nullptr);if(!gRenderWindow)return-1;DragAcceptFiles(hwnd,TRUE);applyFilter();editorLayoutControls(hwnd);return 0;
        case WM_SIZE:editorLayoutControls(hwnd);InvalidateRect(hwnd,nullptr,FALSE);return 0;
        case WM_GETMINMAXINFO:reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize={EDIT_MIN_W,EDIT_MIN_H};return 0;
        case WM_PAINT:paintEditor(hwnd);return 0;
        case WM_ERASEBKGND:return 1;
        case WM_CTLCOLOREDIT:{HDC dc=reinterpret_cast<HDC>(wp);SetBkColor(dc,E_FIELD);SetTextColor(dc,E_TEXT);return reinterpret_cast<LRESULT>(gEditorEditBrush);}
        case WM_COMMAND:if(LOWORD(wp)==EDIT_SEARCH_ID&&HIWORD(wp)==EN_CHANGE){gTreeScroll=0;applyFilter();return 0;}break;
        case WM_LBUTTONDOWN:if(!gDumping){try{editorClick(hwnd,GET_X_LPARAM(lp),GET_Y_LPARAM(lp));}catch(const std::exception&e){MessageBoxA(hwnd,e.what(),"Smoke Detector",MB_OK|MB_ICONERROR);}}return 0;
        case WM_MOUSEWHEEL:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(hwnd,&p);const auto l=editorLayout(hwnd);const int delta=GET_WHEEL_DELTA_WPARAM(wp);if(eHit(l.tree,p.x,p.y)){const int visible=std::max(1,eh(l.tree)/EDIT_ROW_H);const std::size_t maxScroll=gTreeRows.size()>static_cast<std::size_t>(visible)?gTreeRows.size()-visible:0;if(delta<0)gTreeScroll=std::min(maxScroll,gTreeScroll+3);else gTreeScroll=gTreeScroll>3?gTreeScroll-3:0;InvalidateRect(hwnd,&l.left,FALSE);return 0;}if(gEditorMode==EditorMode::Model&&eHit(l.right,p.x,p.y)){if(delta<0)gEditorMeshScroll+=2;else gEditorMeshScroll=gEditorMeshScroll>2?gEditorMeshScroll-2:0;InvalidateRect(hwnd,&l.right,FALSE);return 0;}if(gEditorMode==EditorMode::Rig&&eHit(l.right,p.x,p.y)){if(delta<0)gEditorBoneScroll+=2;else gEditorBoneScroll=gEditorBoneScroll>2?gEditorBoneScroll-2:0;InvalidateRect(hwnd,&l.right,FALSE);return 0;}if(gEditorMode==EditorMode::Animate&&eHit(l.right,p.x,p.y)){if(delta<0)gAnimationScroll+=2;else gAnimationScroll=gAnimationScroll>2?gAnimationScroll-2:0;InvalidateRect(hwnd,&l.right,FALSE);return 0;}break;}
        case WM_DROPFILES:{const HDROP drop=reinterpret_cast<HDROP>(wp);wchar_t path[32768]{};if(DragQueryFileW(drop,0,path,static_cast<UINT>(std::size(path)))>0){loadRomInput(std::filesystem::path(path));ensureEditorState();editorLayoutControls(hwnd);}DragFinish(drop);InvalidateRect(hwnd,nullptr,FALSE);return 0;}
        case WM_CLOSE:if(!gDumping)DestroyWindow(hwnd);return 0;
        case WM_DESTROY:shutdownOpenGL();releaseEditorUiResources();PostQuitMessage(0);return 0;
    }
    return DefWindowProcA(hwnd,msg,wp,lp);
}

} // namespace

int WINAPI WinMain(HINSTANCE instance,HINSTANCE,LPSTR,int show){
    try{
        Gdiplus::GdiplusStartupInput gdiplusInput;Gdiplus::GdiplusStartup(&gGdiToken,&gdiplusInput,nullptr);
        editorUiResources();initializeStudioArchives();
        WNDCLASSA renderClass{};renderClass.style=CS_OWNDC|CS_HREDRAW|CS_VREDRAW;renderClass.lpfnWndProc=RenderProc;renderClass.hInstance=instance;renderClass.hCursor=LoadCursor(nullptr,IDC_ARROW);renderClass.hbrBackground=gBrushBg;renderClass.lpszClassName="SmokeDetectorRenderModern";if(!RegisterClassA(&renderClass)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Could not register render surface");
        WNDCLASSA wc{};wc.style=CS_HREDRAW|CS_VREDRAW;wc.lpfnWndProc=EditorProc;wc.hInstance=instance;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=gBrushBg;wc.lpszClassName="SmokeDetectorEditor";if(!RegisterClassA(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Could not register editor window");
        gWindow=CreateWindowExA(0,wc.lpszClassName,"Smoke Detector",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,1460,880,nullptr,nullptr,instance,nullptr);if(!gWindow||!gRenderWindow)throw std::runtime_error("Could not create Smoke Detector editor");
        initOpenGL(gRenderWindow);if(gScene){uploadGlTextures();ensureEditorState();}ShowWindow(gRenderWindow,gScene?SW_SHOW:SW_HIDE);editorLayoutControls(gWindow);ShowWindow(gWindow,show);UpdateWindow(gWindow);gLastTick=std::chrono::steady_clock::now();
        MSG msg{};bool running=true;while(running){while(PeekMessageA(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT){running=false;break;}TranslateMessage(&msg);DispatchMessageA(&msg);}if(running)editorTick();Sleep(1);}if(gGdiToken)Gdiplus::GdiplusShutdown(gGdiToken);return 0;
    }catch(const std::exception&e){MessageBoxA(nullptr,e.what(),"Smoke Detector error",MB_OK|MB_ICONERROR);releaseEditorUiResources();if(gGdiToken)Gdiplus::GdiplusShutdown(gGdiToken);return 1;}
}
