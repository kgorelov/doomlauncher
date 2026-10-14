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

    int totalItems = static_cast<int>(menuItems.size());
    int totalPages = (totalItems + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    if (totalPages < 1) totalPages = 1;
    int currentPage = selectedItem / ITEMS_PER_PAGE;

    if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
            case SDLK_UP:
                selectedItem = (selectedItem - 1 + totalItems) % totalItems;
                statusMessage.clear();
                break;
            case SDLK_DOWN:
                selectedItem = (selectedItem + 1) % totalItems;
                statusMessage.clear();
                break;
            case SDLK_PAGEUP:
                if (currentPage > 0) {
                    selectedItem = std::max(0, selectedItem - ITEMS_PER_PAGE);
                } else {
                    int targetPage = totalPages - 1;
                    int slot = selectedItem % ITEMS_PER_PAGE;
                    int targetItem = targetPage * ITEMS_PER_PAGE + slot;
                    if (targetItem >= totalItems) targetItem = totalItems - 1;
                    selectedItem = targetItem;
                }
                statusMessage.clear();
                break;
            case SDLK_PAGEDOWN:
                if (currentPage < totalPages - 1) {
                    int targetItem = selectedItem + ITEMS_PER_PAGE;
                    if (targetItem >= totalItems) targetItem = totalItems - 1;
                    selectedItem = targetItem;
                } else {
                    selectedItem = selectedItem % ITEMS_PER_PAGE;
                }
                statusMessage.clear();
                break;
            case SDLK_LEFT:
                if (selectedItem >= 0 && selectedItem < totalItems) {
                    if (menuItems[selectedItem].select_prev_engine()) {
                        statusMessage.clear();
                    }
                }
                break;
            case SDLK_RIGHT:
                if (selectedItem >= 0 && selectedItem < totalItems) {
                    if (menuItems[selectedItem].select_next_engine()) {
                        statusMessage.clear();
                    }
                }
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
    if (item.status == doomlauncher::GameStatus::MissingAssets || !item.assets_found) {
        statusMessage = "Cannot launch: " + (!item.status_details.empty() ? item.status_details : "missing asset");
        std::cout << statusMessage << std::endl;
        return;
    }

    if (item.status == doomlauncher::GameStatus::NoCompatibleEngine) {
        statusMessage = "Cannot launch: " + (!item.status_details.empty() ? item.status_details : "no compatible engine");
        std::cout << statusMessage << std::endl;
        return;
    }

    std::string cmd = item.get_command();
    if (cmd.empty()) {
        cmd = item.cmd;
    }

    if (cmd.empty()) {
        statusMessage = "No CMD defined for " + item.title;
        std::cout << statusMessage << std::endl;
        return;
    }

    statusMessage = "Launching: " + cmd;
    std::cout << statusMessage << std::endl;

    int res = std::system(cmd.c_str());
    (void)res;
}

void Menu::update() {
}

void Menu::render() {
    if (menuItems.empty()) return;

    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Color red = { 255, 0, 0, 255 };
    SDL_Color yellow = { 255, 255, 0, 255 };
    SDL_Color gray = { 130, 130, 130, 255 };
    SDL_Color darkRed = { 180, 50, 50, 255 };
    SDL_Color green = { 100, 255, 100, 255 };

    int totalItems = static_cast<int>(menuItems.size());
    int totalPages = (totalItems + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    if (totalPages < 1) totalPages = 1;
    int currentPage = selectedItem / ITEMS_PER_PAGE;
    int pageStartIndex = currentPage * ITEMS_PER_PAGE;
    int pageEndIndex = std::min(pageStartIndex + ITEMS_PER_PAGE, totalItems);

    int smallFontHeight = smallFont ? TTF_FontHeight(smallFont) : 24;
    int lineSpacing = smallFontHeight + 4;
    int osdStartY = screenHeight - (5 * lineSpacing + 12);
    int osdX = 30;

    int fontHeight = font ? TTF_FontHeight(font) : 40;
    int totalMenuHeight = ITEMS_PER_PAGE * fontHeight;
    int pageIndicatorHeight = smallFontHeight + 8;
    int maxAvailableForMenu = osdStartY - 20 - pageIndicatorHeight - 10;
    int itemSpacing = fontHeight;
    if (totalMenuHeight > maxAvailableForMenu && maxAvailableForMenu > 0) {
        itemSpacing = maxAvailableForMenu / ITEMS_PER_PAGE;
    }
    int effectiveMenuHeight = (ITEMS_PER_PAGE - 1) * itemSpacing + fontHeight;
    int startY = (osdStartY - pageIndicatorHeight - 20 - effectiveMenuHeight) / 2 + 10;
    if (startY < 15) startY = 15;

    for (int i = pageStartIndex; i < pageEndIndex; ++i) {
        const auto& item = menuItems[i];
        bool isSelected = (i == selectedItem);
        bool isPlayable = (item.status == doomlauncher::GameStatus::Ready && item.assets_found);
        SDL_Color color;
        if (!isPlayable) {
            color = isSelected ? darkRed : gray;
        } else {
            color = isSelected ? red : white;
        }

        int slotIndex = i - pageStartIndex;
        int y = startY + slotIndex * itemSpacing;
        std::string displayText = item.title;
        renderText(font, displayText, 0, y, color, true);
    }

    // Render page indicator under menu
    if (smallFont) {
        std::string pageIndicator = "[ Page " + std::to_string(currentPage + 1) + " of " + std::to_string(totalPages) + " ]";
        int pageIndicatorY = startY + (ITEMS_PER_PAGE - 1) * itemSpacing + fontHeight + 8;
        if (pageIndicatorY + smallFontHeight >= osdStartY) {
            pageIndicatorY = osdStartY - smallFontHeight - 4;
        }
        renderText(smallFont, pageIndicator, 0, pageIndicatorY, gray, true);
    }

    // Render OSD / HUD Block at bottom left
    if (smallFont && selectedItem >= 0 && selectedItem < totalItems) {
        const auto& item = menuItems[selectedItem];

        std::string line1;
        SDL_Color line1Color = yellow;

        std::string line2;
        SDL_Color line2Color = white;

        std::string line3;
        SDL_Color line3Color = gray;

        if (item.module_name == "quit") {
            line1 = "ACTION: Exit Doom Launcher";
            line1Color = yellow;
            line2 = "Press Enter or F10 to exit";
            line2Color = gray;
            line3 = "F1: Help | Enter: Exit | F10: Fast Quit";
        } else if (item.status == doomlauncher::GameStatus::MissingAssets || (!item.assets_found && !item.missing_assets.empty())) {
            line1 = "STATUS: Cannot launch - missing asset";
            if (!item.missing_assets.empty()) {
                line1 += " (" + item.missing_assets[0] + ")";
            } else if (!item.status_details.empty()) {
                line1 += " (" + item.status_details + ")";
            }
            line1Color = red;
            line2 = "CMD: [Disabled - missing assets]";
            line2Color = gray;
            line3 = "F1: Help | F2: Save | F3: Info | F4: Engine | F10: Quit";
        } else if (item.status == doomlauncher::GameStatus::NoCompatibleEngine) {
            line1 = "STATUS: Cannot launch - no compatible engine installed";
            if (!item.status_details.empty()) {
                line1 += " (" + item.status_details + ")";
            }
            line1Color = red;
            line2 = "CMD: [Disabled - no compatible engine]";
            line2Color = gray;
            line3 = "F1: Help | F2: Save | F3: Info | F4: Engine | F10: Quit";
        } else {
            const auto* eng = item.get_selected_engine();
            std::string engTitle = eng ? eng->title : "Unknown";
            line1 = "ENGINE: " + engTitle;
            if (item.compatible_engines.size() > 1) {
                line1 += " [" + std::to_string(item.compatible_engines.size()) + " compatible | F4: Change]";
            } else if (item.compatible_engines.size() == 1) {
                line1 += " [1 compatible]";
            }
            line1Color = yellow;

            std::string cmd = item.get_command();
            if (cmd.empty()) cmd = item.cmd;
            line2 = "CMD: " + cmd;
            line2Color = white;

            line3 = "F1: Help | F2: Save | F3: Info | F4: Engine | F10: Quit";
        }

        if (!statusMessage.empty()) {
            line3 = statusMessage;
            line3Color = (statusMessage.find("Saved") != std::string::npos || statusMessage.find("saved") != std::string::npos) ? green : yellow;
        }

        renderText(smallFont, line1, osdX, osdStartY, line1Color, false);
        renderText(smallFont, line2, osdX, osdStartY + lineSpacing, line2Color, false);
        renderText(smallFont, line3, osdX, osdStartY + 2 * lineSpacing, line3Color, false);
    }
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
        int maxW = screenWidth - finalX - 10;
        if (maxW > 0 && surface->w > maxW) {
            SDL_Rect srcRect = { 0, 0, maxW, surface->h };
            SDL_Rect renderQuad = { finalX, y, maxW, surface->h };
            SDL_RenderCopy(renderer, texture, &srcRect, &renderQuad);
        } else {
            SDL_Rect renderQuad = { finalX, y, surface->w, surface->h };
            SDL_RenderCopy(renderer, texture, nullptr, &renderQuad);
        }
        SDL_DestroyTexture(texture);
    }

    SDL_FreeSurface(surface);
}
