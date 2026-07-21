cmake_minimum_required(VERSION 3.10)
project(DoomLauncher)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find SDL2
find_package(SDL2 REQUIRED)
find_package(SDL2_ttf REQUIRED)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src ${SDL2_INCLUDE_DIRS} ${SDL2_TTF_INCLUDE_DIRS})

find_library(SDL2_LIBRARY NAMES SDL2)
find_library(SDL2_TTF_LIBRARY NAMES SDL2_ttf)

# Main Launcher Executable
add_executable(doomlauncher
    src/main.cpp
    src/fire.cpp
    src/menu.cpp
    src/sky.cpp
    src/template_engine.cpp
    src/config_module.cpp
)

target_link_libraries(doomlauncher PUBLIC ${SDL2_LIBRARY} ${SDL2_TTF_LIBRARY})

# Config Engine Unit Test Executable
add_executable(test_config
    src/test_config.cpp
    src/template_engine.cpp
    src/config_module.cpp
)
