#ifndef FIRE_H
#define FIRE_H

#include <SDL.h>
#include <vector>

class Fire {
public:
    Fire(SDL_Renderer* renderer, int screenWidth, int screenHeight);
    ~Fire();

    void update();
    void render();

private:
    SDL_Renderer* renderer;
    SDL_Texture* fireTexture;
    std::vector<Uint8> fireBuffer;
    std::vector<SDL_Color> palette;
    int width, height; // texture dimensions
    int fwidth, fheight; // flame buffer dimensions

    int hspread, vspread, residual;
    int ihspread, ivspread, iresidual;
    int variance, vartrend;
    bool bloom;


    void initPalette();
    void flameActive();
    void flameAdvance();
};

#endif // FIRE_H
