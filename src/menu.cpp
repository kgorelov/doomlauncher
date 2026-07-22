#include "menu.h"
#include <cstdlib>
#include <iostream>

Menu::Menu(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* smallFont, int screenWidth, int screenHeight)
    : renderer(renderer), font(font), smallFont(smallFont), screenWidth(screenWidth), screenHeight(screenHeight), selectedItem(0), quitSelected(false) {
}

Menu::~Menu() {
}

void Menu::setMenuItems(const std::vector<doomlauncher::ResolvedMenuItem>& items) {
    menuItems = items;

    doomlauncher::ResolvedMenuItem quit_item;
    quit_item.title = "Quit";
    quit_item.module_name = "quit";
    menuItems.push_back(quit_item);

    selectedItem = 0;
}

void Menu::handleEvent(const SDL_Event& event) {
    if (menuItems.empty()) return;

    if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
            case SDLK_UP:
                selectedItem = (selectedItem - 1 + static_cast<int>(menuItems.size())) % static_cast<int>(menuItems.size());
                statusMessage.clear();
                break;
            case SDLK_DOWN:
                selectedItem = (selectedItem + 1) % static_cast<int>(menuItems.size());
                statusMessage.clear();
                break;
            case SDLK_RETURN:
                if (menuItems[selectedItem].module_name == "quit") {
                    quitSelected = true;
                } else {
                    launchSelected();
                }
                break;
        }
    }
}

void Menu::launchSelected() {
    if (selectedItem < 0 || selectedItem >= static_cast<int>(menuItems.size())) return;

    const auto& item = menuItems[selectedItem];
    if (item.cmd.empty()) {
        statusMessage = "No CMD defined for " + item.title;
        std::cout << statusMessage << std::endl;
        return;
    }

    statusMessage = "Launching: " + item.cmd;
    std::cout << statusMessage << std::endl;

    int res = std::system(item.cmd.c_str());
    (void)res;
}

void Menu::update() {
}

void Menu::render() {
    if (menuItems.empty()) return;

    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Color red = { 255, 0, 0, 255 };
    SDL_Color yellow = { 255, 255, 0, 255 };

    int fontHeight = TTF_FontHeight(font);
    int totalMenuHeight = static_cast<int>(menuItems.size()) * fontHeight;
    int startY = (screenHeight - totalMenuHeight) / 2;

    for (size_t i = 0; i < menuItems.size(); ++i) {
        SDL_Color color = (static_cast<int>(i) == selectedItem) ? red : white;
        int y = startY + static_cast<int>(i) * fontHeight;
        renderText(font, menuItems[i].title, 0, y, color, true);
    }

#if 0
    // Render command preview / status message at bottom
    if (selectedItem >= 0 && selectedItem < static_cast<int>(menuItems.size())) {
        const auto& item = menuItems[selectedItem];
        std::string preview = item.module_name == "quit" ? "Exit Doom Launcher" : "CMD: " + item.cmd;
        if (!statusMessage.empty()) {
            preview = statusMessage;
        }

        if (smallFont) {
            renderText(smallFont, preview, 0, screenHeight - 50, yellow, true);
        }
    }
#endif
}

bool Menu::isQuitSelected() const {
    return quitSelected;
}

void Menu::renderText(TTF_Font* f, const std::string& text, int x, int y, SDL_Color color, bool centered) {
    if (!f || text.empty()) return;

    SDL_Surface* surface = TTF_RenderText_Solid(f, text.c_str(), color);
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
