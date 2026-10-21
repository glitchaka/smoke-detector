cmake_minimum_required(VERSION 3.20)
project(smoke_detector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(smoke_rom
    src/rom/Lzh8.cpp
    src/rom/Lz11.cpp
    src/rom/RomInspector.cpp
    src/rom/RomLibrary.cpp
    src/rom/RomModelArchive.cpp
    src/rom/CiaBchModelSource.cpp
    src/rom/RumbleWorldArchive.cpp
    src/rom/ThreeDsCrypto.cpp
)

target_include_directories(smoke_rom PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_library(smoke_resource
    src/rom/CartridgeResourceSource.cpp
    src/rom/GenericRomResourceSource.cpp
    src/rom/RomResourceArchive.cpp
)

target_include_directories(smoke_resource PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(smoke_resource PUBLIC smoke_rom)

add_library(smoke_bch
    src/bch/BchScene.cpp
    src/bch/BchUvMetadata.cpp
)

target_include_directories(smoke_bch PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(smoke_bch PUBLIC smoke_rom)

add_library(smoke_export
    src/export/AssetExporter.cpp
    src/export/ResourcePackageExporter.cpp
)

target_include_directories(smoke_export PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(smoke_export PUBLIC smoke_rom smoke_resource smoke_bch)

add_executable(rom_probe tools/rom_probe.cpp)
target_link_libraries(rom_probe PRIVATE smoke_rom)

add_executable(rumble_probe tools/rumble_probe.cpp)
target_link_libraries(rumble_probe PRIVATE smoke_rom)

add_executable(resource_export tools/resource_export.cpp)
target_link_libraries(resource_export PRIVATE smoke_resource smoke_export)

add_executable(resource_unpack tools/resource_unpack.cpp)

add_executable(smoke_sfx_stub tools/resource_unpack.cpp)
target_compile_definitions(smoke_sfx_stub PRIVATE SMOKE_SFX_STUB=1)

if(WIN32)
    # Build a private Studio3 backend include. The new editor owns the window
    # and only reuses the proven ROM/model/OpenGL plumbing.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/demo/SmokeDetectorWin32Studio3.cpp" SMOKE_STUDIO_SOURCE)
    string(REPLACE
        "#include <vector>"
        "#include <vector>\n#include \"bch/BchUvMetadata.h\""
        SMOKE_STUDIO_SOURCE "${SMOKE_STUDIO_SOURCE}")
    string(REPLACE
        "void initializeArchives()"
        "void initializeStudioArchives()"
        SMOKE_STUDIO_SOURCE "${SMOKE_STUDIO_SOURCE}")
    string(REPLACE
        "initializeArchives();"
        "initializeStudioArchives();"
        SMOKE_STUDIO_SOURCE "${SMOKE_STUDIO_SOURCE}")
    string(REPLACE
        "void exportSelected()"
        "void exportStudioSelected()"
        SMOKE_STUDIO_SOURCE "${SMOKE_STUDIO_SOURCE}")
    string(REPLACE
        "exportSelected();"
        "exportStudioSelected();"
        SMOKE_STUDIO_SOURCE "${SMOKE_STUDIO_SOURCE}")
    string(REPLACE
        "loadModel(model);"
        "loadModel(model); promptemon::bch::applyMaterialUvMetadata(*gScene, *gArchive, model);"
        SMOKE_STUDIO_SOURCE "${SMOKE_STUDIO_SOURCE}")
    string(REPLACE
        "int transformType = 0; // 0 Maya, 1 SoftImage, 2 3ds Max"
        "int transformType = -1; // -1 Source, 0 Maya, 1 SoftImage, 2 3ds Max"
        SMOKE_STUDIO_SOURCE "${SMOKE_STUDIO_SOURCE}")
    string(REPLACE
        "const int transformType = fix ? fix->transformType : 0;"
        "const int transformType = (fix && fix->transformType >= 0) ? fix->transformType : (material ? material->uvTransformType : 0);"
        SMOKE_STUDIO_SOURCE "${SMOKE_STUDIO_SOURCE}")
    string(REPLACE
        "return type == 1 ? \"SoftImage\" : (type == 2 ? \"3ds Max\" : \"Maya\");"
        "return type < 0 ? \"Source\" : (type == 1 ? \"SoftImage\" : (type == 2 ? \"3ds Max\" : \"Maya\"));"
        SMOKE_STUDIO_SOURCE "${SMOKE_STUDIO_SOURCE}")
    string(REPLACE
        "fix.transformType = (fix.transformType + 1) % 3;"
        "fix.transformType = fix.transformType >= 2 ? -1 : fix.transformType + 1;"
        SMOKE_STUDIO_SOURCE "${SMOKE_STUDIO_SOURCE}")
    string(REPLACE
        "const int visible = std::max(1, (l.right.bottom - top - 12) / rowHeight);"
        "const int visible = std::max<int>(1, static_cast<int>((l.right.bottom - top - 12) / rowHeight));"
        SMOKE_STUDIO_SOURCE "${SMOKE_STUDIO_SOURCE}")
    string(REPLACE
        "int WINAPI WinMain("
        "int WINAPI SmokeDetectorStudio3LegacyWinMain("
        SMOKE_STUDIO_SOURCE "${SMOKE_STUDIO_SOURCE}")

    set(SMOKE_STUDIO_BASE "${CMAKE_CURRENT_BINARY_DIR}/SmokeDetectorStudioBase.generated.inc")
    file(WRITE "${SMOKE_STUDIO_BASE}" "${SMOKE_STUDIO_SOURCE}")

    # Inject the edited-asset exporter inside the editor's anonymous namespace
    # so it can bake mesh, rig, UV and image-layer changes into the OBJ package.
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/demo/SmokeDetectorWin32Editor.cpp" SMOKE_EDITOR_SOURCE)

    # WIN32_LEAN_AND_MEAN omits COM/OLE declarations required by GDI+ imaging.
    # Inject them before gdiplus.h so IStream/PROPID are defined on MSVC.
    string(REPLACE
        "#include <gdiplus.h>"
        "#include <objidl.h>\n#include <propidl.h>\n#include <gdiplus.h>"
        SMOKE_EDITOR_SOURCE "${SMOKE_EDITOR_SOURCE}")

    # PixelFormat32bppARGB is a GDI+ macro/constant, not a namespaced symbol.
    # Remove the using-declaration without depending on LF vs CRLF line endings.
    string(REPLACE
        "using Gdiplus::PixelFormat32bppARGB;"
        ""
        SMOKE_EDITOR_SOURCE "${SMOKE_EDITOR_SOURCE}")

    string(REPLACE
        "const int visible=std::max(1,(l.right.bottom-y-8)/EDIT_ROW_H);"
        "const int visible=std::max<int>(1,static_cast<int>((l.right.bottom-y-8)/EDIT_ROW_H));"
        SMOKE_EDITOR_SOURCE "${SMOKE_EDITOR_SOURCE}")
    string(REPLACE
        "void editorClick(HWND hwnd,int x,int y){"
        "#include \"SmokeDetectorWin32EditorExport.inc\"\n\nvoid editorClick(HWND hwnd,int x,int y){"
        SMOKE_EDITOR_SOURCE "${SMOKE_EDITOR_SOURCE}")
    string(REPLACE
        "exportStudioSelected();return;"
        "exportEditorSelected();return;"
        SMOKE_EDITOR_SOURCE "${SMOKE_EDITOR_SOURCE}")
    string(REPLACE
        "ShowWindow(gRenderWindow,gScene?SW_SHOW:SW_HIDE);"
        "ShowWindow(gRenderWindow,(gScene&&gPreview3D)?SW_SHOW:SW_HIDE);"
        SMOKE_EDITOR_SOURCE "${SMOKE_EDITOR_SOURCE}")
    string(REPLACE
        "releaseEditorUiResources();PostQuitMessage(0);"
        "releaseEditorUiResources();releaseStudioResources();PostQuitMessage(0);"
        SMOKE_EDITOR_SOURCE "${SMOKE_EDITOR_SOURCE}")

    set(SMOKE_EDITOR_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/SmokeDetectorWin32Editor.generated.cpp")
    file(WRITE "${SMOKE_EDITOR_GENERATED}" "${SMOKE_EDITOR_SOURCE}")

    add_executable(smoke_detector WIN32
        "${SMOKE_EDITOR_GENERATED}"
    )
    target_compile_definitions(smoke_detector PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_include_directories(smoke_detector PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/demo
        ${CMAKE_CURRENT_BINARY_DIR}
    )
    if(MSVC)
        target_compile_options(smoke_detector PRIVATE /utf-8)
    endif()
    target_link_libraries(smoke_detector PRIVATE
        smoke_rom
        smoke_resource
        smoke_bch
        smoke_export
        opengl32
        gdi32
        gdiplus
        user32
        shell32
        comdlg32
    )
endif()
