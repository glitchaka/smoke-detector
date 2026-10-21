cmake_minimum_required(VERSION 3.20)
project(smoke_detector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep the readable/decrypted path unchanged. On Windows, generate the crypto
# translation unit with the standard retail NCCH KeyX table as a built-in
# fallback so ordinary cartridge images do not require an external key file.
set(SMOKE_THREEDS_CRYPTO_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/rom/ThreeDsCrypto.cpp")
if(WIN32)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/rom/ThreeDsCrypto.cpp" SMOKE_CRYPTO_SOURCE)
    string(REPLACE
        "    return std::nullopt;\n}\n\nstruct U128 {"
        "    KeyFile builtIn;\n    builtIn.path = \"Smoke Detector built-in retail NCCH table\";\n    Block key{};\n    if (parseHexBlock(\"82E9C9BEBFB8BDB875ECC0A07D474374\", key)) builtIn.keyX[0x18] = key;\n    if (parseHexBlock(\"45AD04953992C7C893724A9A7BCE6182\", key)) builtIn.keyX[0x1B] = key;\n    if (parseHexBlock(\"CEE7D8AB30C00DAE850EF5E382AC5AF3\", key)) builtIn.keyX[0x25] = key;\n    if (parseHexBlock(\"B98E95CECA3E4D171F76A94DE934C053\", key)) builtIn.keyX[0x2C] = key;\n    return builtIn;\n}\n\nstruct U128 {"
        SMOKE_CRYPTO_SOURCE "${SMOKE_CRYPTO_SOURCE}")
    set(SMOKE_CRYPTO_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/ThreeDsCrypto.generated.cpp")
    file(WRITE "${SMOKE_CRYPTO_GENERATED}" "${SMOKE_CRYPTO_SOURCE}")
    set(SMOKE_THREEDS_CRYPTO_SOURCE "${SMOKE_CRYPTO_GENERATED}")
endif()

add_library(smoke_rom
    src/rom/Lzh8.cpp
    src/rom/Lz11.cpp
    src/rom/RomInspector.cpp
    src/rom/RomLibrary.cpp
    src/rom/RomModelArchive.cpp
    src/rom/CiaBchModelSource.cpp
    src/rom/RumbleWorldArchive.cpp
    ${SMOKE_THREEDS_CRYPTO_SOURCE}
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
        "This image is encrypted. Smoke Detector does not bundle Nintendo keys. "
        "Smoke Detector could not open this encrypted 3DS image automatically. "
        SMOKE_STUDIO_SOURCE "${SMOKE_STUDIO_SOURCE}")
    string(REPLACE
        "Place your own aes_keys.txt beside the ROM, then reopen it. "
        "This image uses a 3DS crypto method that is not supported yet. "
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

    # Mode tabs are resource browsers too. Do not disable them just because a
    # 3D scene could not be parsed; Animal Crossing, for example, exposes many
    # BCRES/texture/animation files even before a model decoder succeeds.
    string(REPLACE
        "eButton(dc, l.modeButtons[static_cast<std::size_t>(i)], labels[i], active, i == 0 || gScene != nullptr);"
        "eButton(dc, l.modeButtons[static_cast<std::size_t>(i)], labels[i], active, !gDumping);"
        SMOKE_EDITOR_SOURCE "${SMOKE_EDITOR_SOURCE}")

    string(REPLACE
        "    eBox(dc,l.right,E_PANEL);\n    switch(gEditorMode){"
        "    eBox(dc,l.right,E_PANEL);\n    if(!gScene){paintBrowseInspector(dc,l);return;}\n    switch(gEditorMode){"
        SMOKE_EDITOR_SOURCE "${SMOKE_EDITOR_SOURCE}")

    string(REPLACE
        "void paintEditor(HWND hwnd){"
        "void paintResourceModeContent(HDC dc,const EditorLayout&l){\n    if(gScene)return;\n    RECT area=l.viewport;\n    eFill(dc,area,E_SURFACE);\n    RECT title{area.left+14,area.top+8,area.right-14,area.top+34};\n    std::string heading=std::string(editorModeName(gEditorMode))+\" resources\";\n    eText(dc,heading,title,gEditorSectionFont,E_TEXT);\n    if(gFiltered.empty()){\n        RECT empty{area.left+20,area.top+50,area.right-20,area.bottom-20};\n        eText(dc,\"No resources in this category\",empty,gEditorSmallFont,E_MUTED,DT_CENTER|DT_VCENTER|DT_SINGLELINE);\n        return;\n    }\n    const int rowH=34;\n    const int visible=std::max<int>(1,static_cast<int>((area.bottom-(area.top+42)-8)/rowH));\n    std::size_t selectedPos=0;\n    const auto it=std::find(gFiltered.begin(),gFiltered.end(),gSelectedItem);\n    if(it!=gFiltered.end())selectedPos=static_cast<std::size_t>(std::distance(gFiltered.begin(),it));\n    std::size_t start=selectedPos>static_cast<std::size_t>(visible/2)?selectedPos-static_cast<std::size_t>(visible/2):0;\n    const std::size_t maxStart=gFiltered.size()>static_cast<std::size_t>(visible)?gFiltered.size()-static_cast<std::size_t>(visible):0;\n    start=std::min(start,maxStart);\n    int y=area.top+42;\n    for(int i=0;i<visible;++i){\n        const std::size_t pos=start+static_cast<std::size_t>(i);\n        if(pos>=gFiltered.size())break;\n        const std::size_t idx=gFiltered[pos];\n        const auto&item=gItems[idx];\n        RECT row{area.left+10,y,area.right-10,y+rowH-2};\n        if(idx==gSelectedItem)eFill(dc,row,E_ACTIVE);\n        RECT badge{row.left+6,row.top,row.left+42,row.bottom};\n        eText(dc,badgeFor(item.category),badge,gEditorTinyFont,idx==gSelectedItem?E_TEXT:E_MUTED,DT_CENTER|DT_VCENTER|DT_SINGLELINE);\n        RECT name{badge.right+4,row.top,row.right-145,row.bottom};\n        eText(dc,item.displayName.empty()?item.id:item.displayName,name,gEditorSmallFont,E_TEXT);\n        RECT type{row.right-140,row.top,row.right-68,row.bottom};\n        eText(dc,filterName(item.category),type,gEditorTinyFont,E_MUTED,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);\n        RECT size{row.right-64,row.top,row.right-6,row.bottom};\n        eText(dc,item.size?humanBytes(item.size):\"\",size,gEditorTinyFont,E_MUTED,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);\n        eLine(dc,row.left,row.bottom,row.right,row.bottom,RGB(48,48,48));\n        y+=rowH;\n    }\n}\n\nvoid paintEditor(HWND hwnd){"
        SMOKE_EDITOR_SOURCE "${SMOKE_EDITOR_SOURCE}")

    string(REPLACE
        "eFill(dc,client,E_BG);const auto l=editorLayout(hwnd);paintEditorTop(dc,l);paintEditorLeft(dc,l);paintCenterHeader(dc,l);if(gEditorMode==EditorMode::Texture)paintUvCanvas(dc,l);if(gEditorMode==EditorMode::Animate)paintTimeline(dc,l);paintEditorRight(dc,l);paintEditorStatus(dc,l);"
        "eFill(dc,client,E_BG);const auto l=editorLayout(hwnd);paintEditorTop(dc,l);paintEditorLeft(dc,l);paintCenterHeader(dc,l);if(!gScene)paintResourceModeContent(dc,l);else{if(gEditorMode==EditorMode::Texture)paintUvCanvas(dc,l);if(gEditorMode==EditorMode::Animate)paintTimeline(dc,l);}paintEditorRight(dc,l);paintEditorStatus(dc,l);"
        SMOKE_EDITOR_SOURCE "${SMOKE_EDITOR_SOURCE}")

    string(REPLACE
        "for(int i=0;i<5;++i){if(eHit(l.modeButtons[static_cast<std::size_t>(i)],x,y)){if(i==0||gScene){gEditorMode=static_cast<EditorMode>(i);editorLayoutControls(hwnd);InvalidateRect(hwnd,nullptr,FALSE);}return;}}"
        "for(int i=0;i<5;++i){if(eHit(l.modeButtons[static_cast<std::size_t>(i)],x,y)){gEditorMode=static_cast<EditorMode>(i);gCategory=i==0?0:(i==3?3:(i==4?2:1));gTreeScroll=0;applyFilter();if(gSelectedItem<gItems.size()){if(i==1||i==2)activateEditorItem(gSelectedItem);else{gPreview3D=false;if(gRenderWindow)ShowWindow(gRenderWindow,SW_HIDE);setStatus(gItems[gSelectedItem].id);}}editorLayoutControls(hwnd);InvalidateRect(hwnd,nullptr,FALSE);return;}}"
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
