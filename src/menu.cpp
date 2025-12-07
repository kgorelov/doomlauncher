#include "menu.h"
#include <iostream>

Menu::Menu(SDL_Renderer* renderer, TTF_Font* font, int screenWidth, int screenHeight)
    : renderer(renderer), font(font), screenWidth(screenWidth), screenHeight(screenHeight), selectedItem(0), quitSelected(false) {
    menuItems = { "DooM", "DooM 2", "DooM TNT", "Plutonia", "Plutonia 2", "Sigil", "Options", "Quit" };
}

Menu::~Menu() {
}

void Menu::handleEvent(const SDL_Event& event) {
    if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
            case SDLK_UP:
                selectedItem = (selectedItem - 1 + menuItems.size()) % menuItems.size();
                break;
            case SDLK_DOWN:
                selectedItem = (selectedItem + 1) % menuItems.size();
                break;
            case SDLK_RETURN:
                if (menuItems[selectedItem] == "Quit") {
                    quitSelected = true;
                }
                break;
        }
    }
}

void Menu::update() {
    // Nothing to update for now
}

void Menu::render() {
    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Color red = { 255, 0, 0, 255 };

    int fontHeight = TTF_FontHeight(font);
    int totalMenuHeight = menuItems.size() * fontHeight;
    int startY = (screenHeight - totalMenuHeight) / 2;

    for (int i = 0; i < menuItems.size(); ++i) {
        SDL_Color color = (i == selectedItem) ? red : white;
        int y = startY + i * fontHeight;
        renderText(menuItems[i], 0, y, color, true);
    }
}

bool Menu::isQuitSelected() const {
    return quitSelected;
}

void Menu::renderText(const std::string& text, int x, int y, SDL_Color color, bool centered) {
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
