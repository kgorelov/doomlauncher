#include <SDL.h>
#include <SDL_ttf.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include "config_module.h"
#include "fire.h"
#include "font_data.h"
#include "menu.h"
#include "sky.h"

namespace fs = std::filesystem;

const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 600;

void print_help(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options] [config_path]\n\n"
              << "Options:\n"
              << "  -h, --help               Show this help message and exit\n"
              << "  -m, --module <path>      Specify a module config file or directory to load (can be repeated)\n";
}

int main(int argc, char* argv[]) {
    std::vector<std::string> modulePaths;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        } else if (arg == "-m" || arg == "--module") {
            if (i + 1 < argc) {
                modulePaths.push_back(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires a path argument.\n";
                print_help(argv[0]);
                return 1;
            }
        } else if (arg.rfind("--module=", 0) == 0) {
            modulePaths.push_back(arg.substr(9));
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "Error: Unknown option '" << arg << "'\n";
            print_help(argv[0]);
            return 1;
        } else {
            modulePaths.push_back(arg);
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "Could not initialize SDL: " << SDL_GetError() << std::endl;
        return 1;
    }

    if (TTF_Init() == -1) {
        std::cerr << "Could not initialize SDL_ttf: " << TTF_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Doom Launcher",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        SDL_WINDOW_FULLSCREEN_DESKTOP
    );

    if (window == nullptr) {
        std::cerr << "Could not create window: " << SDL_GetError() << std::endl;
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == nullptr) {
        std::cerr << "Could not create renderer: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    int screenWidth, screenHeight;
    SDL_GetRendererOutputSize(renderer, &screenWidth, &screenHeight);

    Fire fire(renderer, screenWidth, screenHeight);
    Sky sky(renderer, screenWidth, screenHeight);

    SDL_RWops* rw1 = SDL_RWFromConstMem(fonts_Doom2016Left_RpJDA_ttf, fonts_Doom2016Left_RpJDA_ttf_len);
    TTF_Font* font = TTF_OpenFontRW(rw1, 1, 84);

    SDL_RWops* rw2 = SDL_RWFromConstMem(fonts_Doom2016Left_RpJDA_ttf, fonts_Doom2016Left_RpJDA_ttf_len);
    TTF_Font* smallFont = TTF_OpenFontRW(rw2, 1, 24);

    if (font == nullptr || smallFont == nullptr) {
        std::cerr << "Failed to load font! SDL_ttf Error: " << TTF_GetError() << std::endl;
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    // Load config modules
    doomlauncher::ModuleRegistry registry;

    if (!modulePaths.empty()) {
        for (const auto& path : modulePaths) {
            if (fs::is_directory(path)) {
                std::cout << "Loading modules directory: " << path << std::endl;
                registry.load_directory(path);
            } else if (fs::exists(path)) {
                std::cout << "Loading config file: " << path << std::endl;
                registry.load_file(path);
            } else {
                std::cerr << "Error: Specified path does not exist: " << path << std::endl;
            }
        }
    } else {
        std::string configPath = "modules/main.toml";
        if (fs::exists(configPath)) {
            std::cout << "Loading config file: " << configPath << std::endl;
            registry.load_file(configPath);
        } else if (fs::exists("modules")) {
            std::cout << "Loading modules directory..." << std::endl;
            registry.load_directory("modules");
        } else {
            std::cerr << "Warning: No config modules found!" << std::endl;
        }
    }

    std::vector<doomlauncher::ResolvedMenuItem> resolvedItems = registry.resolve_menu_items();
    std::cout << "Resolved " << resolvedItems.size() << " menu item(s) from config." << std::endl;

    Menu menu(renderer, font, smallFont, screenWidth, screenHeight);
    menu.setMenuItems(resolvedItems);

    bool quit = false;
    SDL_Event event;

    while (!quit) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                quit = true;
            }
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    quit = true;
                }
            }
            menu.handleEvent(event);
        }

        if (menu.isQuitSelected()) {
            quit = true;
        }

        menu.update();

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        sky.update();
        sky.render();

        fire.update();
        fire.render();

        menu.render();

        SDL_RenderPresent(renderer);
    }

    if (font) TTF_CloseFont(font);
    if (smallFont) TTF_CloseFont(smallFont);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();

    return 0;
}
