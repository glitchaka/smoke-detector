enum class ViewportEditTool { Move, Rotate, Scale };

ViewportEditTool gViewportEditTool = ViewportEditTool::Move;
bool gViewportEditing = false;
bool gViewportOrbiting = false;
POINT gViewportLastMouse{};

bool viewportProjectPoint(promptemon::bch::Vec3 point, POINT& screen) {
    if (!gGL || !gRenderWindow) return false;
    if (wglGetCurrentContext() != gGL && !wglMakeCurrent(gDC, gGL)) return false;

    GLdouble model[16]{};
    GLdouble projection[16]{};
    GLint viewport[4]{};
    glGetDoublev(GL_MODELVIEW_MATRIX, model);
    glGetDoublev(GL_PROJECTION_MATRIX, projection);
    glGetIntegerv(GL_VIEWPORT, viewport);

    const GLdouble input[4] = {point.x, point.y, point.z, 1.0};
    GLdouble eye[4]{};
    GLdouble clip[4]{};
    for (int row = 0; row < 4; ++row) {
        eye[row] = model[row] * input[0] + model[4 + row] * input[1] +
                   model[8 + row] * input[2] + model[12 + row] * input[3];
    }
    for (int row = 0; row < 4; ++row) {
        clip[row] = projection[row] * eye[0] + projection[4 + row] * eye[1] +
                    projection[8 + row] * eye[2] + projection[12 + row] * eye[3];
    }
    if (std::fabs(clip[3]) < 1e-8) return false;
    const double ndcX = clip[0] / clip[3];
    const double ndcY = clip[1] / clip[3];
    const double ndcZ = clip[2] / clip[3];
    if (ndcZ < -1.2 || ndcZ > 1.2) return false;
    screen.x = static_cast<LONG>(viewport[0] + (ndcX + 1.0) * viewport[2] * 0.5);
    screen.y = static_cast<LONG>(viewport[3] - (ndcY + 1.0) * viewport[3] * 0.5);
    return true;
}

promptemon::bch::Vec3 viewportBonePoint(std::size_t index) {
    if (!gScene || index >= gScene->bones().size()) return {};
    const auto matrices = editorSkinMatrices();
    const auto& bone = gScene->bones()[index];
    promptemon::bch::Vec3 point{bone.bindGlobal.m[12], bone.bindGlobal.m[13], bone.bindGlobal.m[14]};
    if (index < matrices.size()) return eTransform(matrices[index], point);
    return editedBonePoint(index);
}

std::size_t viewportPickBone(int x, int y) {
    if (!gScene || gScene->bones().empty()) return static_cast<std::size_t>(-1);
    std::size_t best = static_cast<std::size_t>(-1);
    long long bestDistance = 20LL * 20LL;
    for (std::size_t i = 0; i < gScene->bones().size(); ++i) {
        POINT point{};
        if (!viewportProjectPoint(viewportBonePoint(i), point)) continue;
        const long long dx = static_cast<long long>(point.x) - x;
        const long long dy = static_cast<long long>(point.y) - y;
        const long long distance = dx * dx + dy * dy;
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

std::size_t viewportPickMesh(int x, int y) {
    if (!gScene || gScene->meshes().empty()) return static_cast<std::size_t>(-1);
    const auto matrices = editorSkinMatrices();
    const auto& edits = currentMeshEdits();
    std::size_t best = static_cast<std::size_t>(-1);
    long long bestDistance = 48LL * 48LL;
    for (std::size_t i = 0; i < gScene->meshes().size(); ++i) {
        if (i < edits.size() && !edits[i].visible) continue;
        auto center = meshCenter(gScene->meshes()[i], matrices);
        if (i < edits.size()) {
            center.x += edits[i].tx;
            center.y += edits[i].ty;
            center.z += edits[i].tz;
        }
        POINT point{};
        if (!viewportProjectPoint(center, point)) continue;
        const long long dx = static_cast<long long>(point.x) - x;
        const long long dy = static_cast<long long>(point.y) - y;
        const long long distance = dx * dx + dy * dy;
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

float viewportEditStep() {
    return 0.0065f / std::max(0.05f, gFitScale * std::max(0.35f, gZoom));
}

const char* viewportToolLabel() {
    switch (gViewportEditTool) {
        case ViewportEditTool::Move: return "Move";
        case ViewportEditTool::Rotate: return "Rotate";
        case ViewportEditTool::Scale: return "Scale";
    }
    return "Move";
}

void viewportApplyModelDelta(int dx, int dy) {
    auto& edits = currentMeshEdits();
    if (gEditorMesh >= edits.size()) return;
    auto& edit = edits[gEditorMesh];
    const bool zAxis = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const float step = viewportEditStep();
    if (gViewportEditTool == ViewportEditTool::Move) {
        if (zAxis) edit.tz -= dy * step;
        else { edit.tx += dx * step; edit.ty -= dy * step; }
    } else if (gViewportEditTool == ViewportEditTool::Rotate) {
        if (zAxis) edit.rz += dx * 0.010f;
        else { edit.ry += dx * 0.010f; edit.rx += dy * 0.010f; }
    } else {
        const float factor = std::clamp(1.0f - dy * 0.01f, 0.2f, 5.0f);
        edit.sx = std::clamp(edit.sx * factor, 0.05f, 20.0f);
        edit.sy = edit.sz = edit.sx;
    }
}

void viewportApplyRigDelta(int dx, int dy) {
    auto& edits = currentBoneEdits();
    if (gEditorBone >= edits.size()) return;
    auto& edit = edits[gEditorBone];
    const bool zAxis = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const float step = viewportEditStep();
    if (gViewportEditTool == ViewportEditTool::Rotate) {
        if (zAxis) edit.rz += dx * 0.010f;
        else { edit.ry += dx * 0.010f; edit.rx += dy * 0.010f; }
    } else {
        if (zAxis) edit.tz -= dy * step;
        else { edit.tx += dx * step; edit.ty -= dy * step; }
    }
}

void viewportApplyAnimationDelta(int dx, int dy) {
    if (!gScene || gScene->bones().empty()) return;
    gEditorBone = std::min(gEditorBone, gScene->bones().size() - 1);
    const bool zAxis = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const float step = viewportEditStep();
    if (gViewportEditTool == ViewportEditTool::Move) {
        if (zAxis) authorAdjustTranslation(2, -dy * step);
        else {
            if (dx) authorAdjustTranslation(0, dx * step);
            if (dy) authorAdjustTranslation(1, -dy * step);
        }
    } else if (gViewportEditTool == ViewportEditTool::Rotate) {
        if (zAxis) authorAdjustRotation(2, dx * 0.010f);
        else {
            if (dx) authorAdjustRotation(1, dx * 0.010f);
            if (dy) authorAdjustRotation(0, dy * 0.010f);
        }
    } else {
        const float delta = -dy * 0.005f;
        if (delta) {
            authorAdjustScale(0, delta);
            authorAdjustScale(1, delta);
            authorAdjustScale(2, delta);
        }
    }
}

void viewportApplyDelta(int dx, int dy) {
    if (gEditorMode == EditorMode::Model) viewportApplyModelDelta(dx, dy);
    else if (gEditorMode == EditorMode::Rig) viewportApplyRigDelta(dx, dy);
    else if (gEditorMode == EditorMode::Animate) viewportApplyAnimationDelta(dx, dy);
    if (gWindow) InvalidateRect(gWindow, nullptr, FALSE);
}

void viewportSetTool(ViewportEditTool tool) {
    gViewportEditTool = tool;
    setStatus(std::string("Tool: ") + viewportToolLabel() + " | left drag edits | Ctrl = Z axis | right drag orbits | wheel zooms");
}

LRESULT CALLBACK EditorRenderProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            if (gScene && (gEditorMode == EditorMode::Model || gEditorMode == EditorMode::Rig || gEditorMode == EditorMode::Animate)) {
                const int x = GET_X_LPARAM(lp);
                const int y = GET_Y_LPARAM(lp);
                if (gEditorMode == EditorMode::Model) {
                    const auto picked = viewportPickMesh(x, y);
                    if (picked != static_cast<std::size_t>(-1)) gEditorMesh = picked;
                } else {
                    const auto picked = viewportPickBone(x, y);
                    if (picked != static_cast<std::size_t>(-1)) gEditorBone = picked;
                    if (gEditorMode == EditorMode::Animate) gAnimationPaused = true;
                }
                gViewportEditing = true;
                gViewportLastMouse = {x, y};
                SetCapture(hwnd);
                viewportSetTool(gViewportEditTool);
                if (gWindow) InvalidateRect(gWindow, nullptr, FALSE);
                return 0;
            }
            return RenderProc(hwnd, msg, wp, lp);
        }
        case WM_LBUTTONUP:
            if (gViewportEditing) {
                gViewportEditing = false;
                ReleaseCapture();
                return 0;
            }
            return RenderProc(hwnd, msg, wp, lp);
        case WM_RBUTTONDOWN:
            SetFocus(hwnd);
            gViewportOrbiting = true;
            gViewportLastMouse = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            SetCapture(hwnd);
            return 0;
        case WM_RBUTTONUP:
            if (gViewportOrbiting) {
                gViewportOrbiting = false;
                ReleaseCapture();
                return 0;
            }
            break;
        case WM_MOUSEMOVE: {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (gViewportEditing) {
                viewportApplyDelta(point.x - gViewportLastMouse.x, point.y - gViewportLastMouse.y);
                gViewportLastMouse = point;
                return 0;
            }
            if (gViewportOrbiting) {
                gYaw += (point.x - gViewportLastMouse.x) * 0.55f;
                gPitch += (point.y - gViewportLastMouse.y) * 0.35f;
                gPitch = std::clamp(gPitch, -70.0f, 70.0f);
                gViewportLastMouse = point;
                return 0;
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == 'W') { viewportSetTool(ViewportEditTool::Move); return 0; }
            if (wp == 'E') { viewportSetTool(ViewportEditTool::Rotate); return 0; }
            if (wp == 'R') { viewportSetTool(ViewportEditTool::Scale); return 0; }
            if (wp == 'N' && gEditorMode == EditorMode::Animate) { authorCreateAnimation(); return 0; }
            if (wp == VK_SPACE && gEditorMode == EditorMode::Animate) { gAnimationPaused = !gAnimationPaused; return 0; }
            if ((wp == VK_LEFT || wp == VK_RIGHT) && gEditorMode == EditorMode::Animate && gCurrent) {
                gAnimationPaused = true;
                gFrame = std::clamp(gFrame + (wp == VK_LEFT ? -1.0f : 1.0f), 0.0f, std::max(1.0f, gCurrent->frames));
                if (gWindow) InvalidateRect(gWindow, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_MOUSEWHEEL:
            return RenderProc(hwnd, msg, wp, lp);
        case WM_ERASEBKGND:
            return 1;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}
