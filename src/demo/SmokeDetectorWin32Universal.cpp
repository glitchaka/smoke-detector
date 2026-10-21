#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <filesystem>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

// Keep the modern UI implementation intact. This translation unit reuses it
// and replaces only the application entry point/window dispatch so ROM input is
// no longer tied to the repository's roms/ directory or to Pokemon content.
#define WinMain SmokeDetectorModernLegacyWinMain
#include "SmokeDetectorWin32Modern.cpp"
#undef WinMain

namespace {

constexpr int ID_OPEN_ROM = 1010;

void clearLoadedModel() {
    if (gGL) destroyGlTextures();
    gScene.reset();
    gSelectedModel.clear();
    gIdle = nullptr;
    gCurrent = nullptr;
    gFrame = 0.0f;
    gYaw = 0.0f;
    gPitch = 0.0f;
    gZoom = 1.0f;
    gModelCenter = {};
    gFitScale = 1.0f;

    if (gGL && gDC && gRenderWindow) {
        wglMakeCurrent(gDC, gGL);
        glClearColor(0.075f, 0.085f, 0.105f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        SwapBuffers(gDC);
    }
}

void updateCapabilityButtons() {
    if (!gWindow) return;
    const BOOL hasModels = gModels.empty() ? FALSE : TRUE;
    const BOOL hasScene = gScene ? TRUE : FALSE;
    if (auto h = GetDlgItem(gWindow, ID_MODELS)) EnableWindow(h, hasModels);
    if (auto h = GetDlgItem(gWindow, ID_EXPORT_MODEL)) EnableWindow(h, hasModels);
    if (auto h = GetDlgItem(gWindow, ID_IDLE)) EnableWindow(h, hasScene);
    if (auto h = GetDlgItem(gWindow, ID_ACTION1)) EnableWindow(h, hasScene);
    if (auto h = GetDlgItem(gWindow, ID_ACTION2)) EnableWindow(h, hasScene);
}

std::string loadedRomStatus(const std::filesystem::path& input) {
    if (!gResourceArchive) return "ROM cargada";
    const auto summaries = gResourceArchive->sourceSummaries();
    if (summaries.empty()) return "ROM cargada";

    const auto& first = summaries.front();
    std::ostringstream text;
    if (summaries.size() == 1) {
        const std::string title = first.rom.title.empty()
            ? first.rom.path.filename().string()
            : first.rom.title;
        text << title << " | " << RomInspector::formatName(first.rom.format)
             << " | " << first.resourceCount << " recursos";
        if (gModels.empty()) text << " | sin modelo 3D compatible";
    } else {
        text << summaries.size() << " ROMs cargadas | " << gModels.size() << " modelos 3D";
    }
    return text.str();
}

bool loadRomInput(const std::filesystem::path& input, bool reportErrors = true) {
    try {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(input, ec) && !std::filesystem::is_directory(input, ec)) {
            throw std::runtime_error("La ruta seleccionada no existe o no es accesible.");
        }

        auto resourceArchive = std::make_unique<RomResourceArchive>();
        if (!resourceArchive->open(input)) {
            throw std::runtime_error("No se pudo abrir la ROM seleccionada.");
        }

        auto modelArchive = std::make_unique<RomModelArchive>();
        modelArchive->open(input);
        auto models = modelArchive->modelResources();

        clearLoadedModel();
        gRomDirectory = input;
        gResourceArchive = std::move(resourceArchive);
        gArchive = std::move(modelArchive);
        gModels = std::move(models);

        if (gExportRoot.empty()) {
            const auto base = std::filesystem::is_directory(input, ec)
                ? input.parent_path()
                : input.parent_path();
            gExportRoot = base / "smoke-detector-exports";
        }

        if (!gModels.empty()) {
            // Do not assume Pokemon. Load the first actual 3D model exposed by
            // the format provider, whatever game supplied it.
            loadModel(gModels.front());
        } else {
            setStatus(loadedRomStatus(input));
        }

        if (gWindow) {
            const std::string caption = "Smoke Detector - " + input.filename().string();
            SetWindowTextA(gWindow, caption.c_str());
        }
        updateCapabilityButtons();
        return true;
    } catch (const std::exception& e) {
        if (reportErrors && gWindow) {
            MessageBoxA(gWindow, e.what(), "No se pudo abrir la ROM", MB_OK | MB_ICONERROR);
        }
        return false;
    }
}

std::filesystem::path chooseRomFile(HWND owner) {
    wchar_t buffer[32768]{};
    static const wchar_t filter[] =
        L"ROMs compatibles (*.gb;*.gbc;*.gba;*.nds;*.cia;*.3ds;*.cci;*.cxi)\0"
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

void initializeUniversalArchives() {
    const auto startupRom = commandLineRom();

    auto defaultRoms = findRomDirectory(std::filesystem::current_path());
    if (defaultRoms.empty()) defaultRoms = findRomDirectory(executableDirectory());
    if (!defaultRoms.empty()) gExportRoot = defaultRoms.parent_path() / "exports";
    else gExportRoot = executableDirectory() / "exports";

    if (!startupRom.empty()) {
        if (!loadRomInput(startupRom, false)) {
            throw std::runtime_error("No se pudo cargar la ROM indicada en la linea de comandos.");
        }
        return;
    }

    if (!defaultRoms.empty() && loadRomInput(defaultRoms, false)) return;

    // An empty installation is valid now: the user can click Abrir ROM or drag
    // a file into the window instead of having to prepare roms/ in advance.
    gArchive = std::make_unique<RomModelArchive>();
    gResourceArchive = std::make_unique<RomResourceArchive>();
    gModels.clear();
    setStatus("Abre o arrastra una ROM para comenzar.");
}

LRESULT CALLBACK UniversalWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CREATE) {
        const LRESULT result = WndProc(hwnd, msg, wp, lp);
        if (result == -1) return result;
        createCommandButton(hwnd, ID_OPEN_ROM, "Abrir ROM", 780, 116);
        DragAcceptFiles(hwnd, TRUE);
        updateCapabilityButtons();
        return result;
    }

    if (msg == WM_COMMAND && LOWORD(wp) == ID_OPEN_ROM) {
        if (gDumping) return 0;
        const auto selected = chooseRomFile(hwnd);
        if (!selected.empty()) loadRomInput(selected);
        return 0;
    }

    if (msg == WM_DROPFILES) {
        if (gDumping) return 0;
        const auto drop = reinterpret_cast<HDROP>(wp);
        wchar_t path[32768]{};
        if (DragQueryFileW(drop, 0, path, static_cast<UINT>(std::size(path))) > 0) {
            loadRomInput(std::filesystem::path(path));
        }
        DragFinish(drop);
        return 0;
    }

    if (msg == WM_GETMINMAXINFO) {
        const LRESULT result = WndProc(hwnd, msg, wp, lp);
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        if (info) info->ptMinTrackSize.x = std::max<LONG>(info->ptMinTrackSize.x, 930);
        return result;
    }

    return WndProc(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    try {
        ensureUiResources();
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
        wc.lpfnWndProc = UniversalWndProc;
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
        if (gScene) uploadGlTextures();
        updateCapabilityButtons();
        if (!gRomDirectory.empty() && std::filesystem::is_regular_file(gRomDirectory)) {
            const std::string caption = "Smoke Detector - " + gRomDirectory.filename().string();
            SetWindowTextA(gWindow, caption.c_str());
        }
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
