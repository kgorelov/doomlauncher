#include <SDL.h>
#include <SDL_ttf.h>
#include <iostream>
#include <vector>
#include <string>

const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 600;

void renderText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color, int screenWidth, bool centered = false) {
    SDL_Surface* surface = TTF_RenderText_Solid(font, text.c_str(), color);
    if (surface == nullptr) {
        std::cerr << "Unable to render text surface! SDL_ttf Error: " << TTF_GetError() << std::endl;
        return;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture == nullptr) {
        std::cerr << "Unable to create texture from rendered text! SDL Error: " << SDL_GetError() << std::endl;
    } else {
        int textWidth = surface->w;
        int finalX = centered ? (screenWidth - textWidth) / 2 : x;
        SDL_Rect renderQuad = { finalX, y, surface->w, surface->h };
        SDL_RenderCopy(renderer, texture, nullptr, &renderQuad);
        SDL_DestroyTexture(texture);
    }

    SDL_FreeSurface(surface);
}

int main(int argc, char* argv[]) {
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

    // TTF_Font* font = TTF_OpenFont("fonts/MetalMania-Regular.ttf", 84);
    TTF_Font* font = TTF_OpenFont("fonts/Doom2016Left-RpJDA.ttf", 96);
    if (font == nullptr) {
        std::cerr << "Failed to load font! SDL_ttf Error: " << TTF_GetError() << std::endl;
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    std::vector<std::string> menuItems = { "DooM", "DooM 2", "DooM TnT", "New Game", "Load Game", "Options", "Quit" };
    int selectedItem = 0;

    bool quit = false;
    SDL_Event event;

    while (!quit) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                quit = true;
            }
            if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                    case SDLK_UP:
                        selectedItem = (selectedItem - 1 + menuItems.size()) % menuItems.size();
                        break;
                    case SDLK_DOWN:
                        selectedItem = (selectedItem + 1) % menuItems.size();
                        break;
                    case SDLK_RETURN:
                        // Handle selection
                        if (menuItems[selectedItem] == "Quit") {
                            quit = true;
                        }
                        break;
                    case SDLK_ESCAPE:
                        quit = true;
                        break;
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        SDL_Color white = { 255, 255, 255, 255 };
        SDL_Color red = { 255, 0, 0, 255 };

        int fontHeight = TTF_FontHeight(font);
        int totalMenuHeight = menuItems.size() * fontHeight;
        int startY = (screenHeight - totalMenuHeight) / 2;

        for (int i = 0; i < menuItems.size(); ++i) {
            SDL_Color color = (i == selectedItem) ? red : white;
            int y = startY + i * fontHeight;
            renderText(renderer, font, menuItems[i], 0, y, color, screenWidth, true);
        }

        SDL_RenderPresent(renderer);
    }

    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();

    return 0;
}
