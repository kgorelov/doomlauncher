#ifndef MENU_H
#define MENU_H

#include <SDL.h>
#include <SDL_ttf.h>
#include <vector>
#include <string>

class Menu {
public:
    Menu(SDL_Renderer* renderer, TTF_Font* font, int screenWidth, int screenHeight);
    ~Menu();

    void handleEvent(const SDL_Event& event);
    void update();
    void render();
    bool isQuitSelected() const;

private:
    SDL_Renderer* renderer;
    TTF_Font* font;
    int screenWidth;
    int screenHeight;
    std::vector<std::string> menuItems;
    int selectedItem;
    bool quitSelected;

    void renderText(const std::string& text, int x, int y, SDL_Color color, bool centered = false);
};

#endif // MENU_H
