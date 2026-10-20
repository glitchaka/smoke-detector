cmake_minimum_required(VERSION 3.20)
project(smoke_detector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(smoke_rom
    src/rom/Lzh8.cpp
    src/rom/RomInspector.cpp
    src/rom/RomLibrary.cpp
    src/rom/RomModelArchive.cpp
    src/rom/CiaBchModelSource.cpp
    src/rom/RumbleWorldArchive.cpp
)

target_include_directories(smoke_rom PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_library(smoke_bch
    src/bch/BchScene.cpp
)

target_include_directories(smoke_bch PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_library(smoke_export
    src/export/AssetExporter.cpp
)

target_include_directories(smoke_export PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(smoke_export PUBLIC smoke_rom smoke_bch)

add_executable(rom_probe tools/rom_probe.cpp)
target_link_libraries(rom_probe PRIVATE smoke_rom)

add_executable(rumble_probe tools/rumble_probe.cpp)
target_link_libraries(rumble_probe PRIVATE smoke_rom)

if(WIN32)
    add_executable(smoke_detector WIN32
        src/demo/SmokeDetectorWin32.cpp
    )
    target_compile_definitions(smoke_detector PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(smoke_detector PRIVATE
        smoke_rom
        smoke_bch
        smoke_export
        opengl32
        gdi32
        user32
        shell32
    )
endif()
