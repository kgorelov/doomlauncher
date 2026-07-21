#ifndef MENU_H
#define MENU_H

#include "config_module.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <vector>

class Menu {
public:
    Menu(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* smallFont, int screenWidth, int screenHeight);
    ~Menu();

    void setMenuItems(const std::vector<doomlauncher::ResolvedMenuItem>& items);
    void handleEvent(const SDL_Event& event);
    void update();
    void render();
    bool isQuitSelected() const;

private:
    SDL_Renderer* renderer;
    TTF_Font* font;
    TTF_Font* smallFont;
    int screenWidth;
    int screenHeight;
    std::vector<doomlauncher::ResolvedMenuItem> menuItems;
    int selectedItem;
    bool quitSelected;
    std::string statusMessage;

    void renderText(TTF_Font* f, const std::string& text, int x, int y, SDL_Color color, bool centered = false);
    void launchSelected();
};

#endif // MENU_H
