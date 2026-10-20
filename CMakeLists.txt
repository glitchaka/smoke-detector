cmake_minimum_required(VERSION 3.20)
project(promptemon_steidium LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(promptemon_rom
    src/rom/Lzh8.cpp
    src/rom/RomInspector.cpp
    src/rom/RomLibrary.cpp
    src/rom/RomModelArchive.cpp
    src/rom/CiaBchModelSource.cpp
    src/rom/RumbleWorldArchive.cpp
)

target_include_directories(promptemon_rom PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_library(promptemon_bch
    src/bch/BchScene.cpp
)

target_include_directories(promptemon_bch PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_executable(rom_probe
    tools/rom_probe.cpp
)

target_link_libraries(rom_probe PRIVATE promptemon_rom)

add_executable(rumble_probe
    tools/rumble_probe.cpp
)

target_link_libraries(rumble_probe PRIVATE promptemon_rom)

if(WIN32)
    add_executable(promptemon_demo WIN32
        src/demo/PromptemonDemoWin32.cpp
    )
    target_compile_definitions(promptemon_demo PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(promptemon_demo PRIVATE
        promptemon_rom
        promptemon_bch
        opengl32
        gdi32
        user32
        shell32
    )
endif()
