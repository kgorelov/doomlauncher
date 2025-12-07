cmake_minimum_required(VERSION 3.10)
project(DoomLauncher)

# Find SDL2
find_package(SDL2 REQUIRED)
find_package(SDL2_ttf REQUIRED)

# Add source files
add_executable(doomlauncher src/main.cpp src/fire.cpp src/menu.cpp)

target_include_directories(doomlauncher PUBLIC ${SDL2_INCLUDE_DIRS} ${SDL2_TTF_INCLUDE_DIRS})
find_library(SDL2_LIBRARY NAMES SDL2)
find_library(SDL2_TTF_LIBRARY NAMES SDL2_ttf)

target_link_libraries(doomlauncher PUBLIC ${SDL2_LIBRARY} ${SDL2_TTF_LIBRARY})
