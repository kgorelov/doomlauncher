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
    int width;
    int height;
    std::vector<Uint8> fireBuffer;
    SDL_Texture* fireTexture;
    std::vector<SDL_Color> palette;

    int hspread;
    int vspread;
    int residual;
    int variance;
    int vartrend;
    bool bloom;

    int ihspread;
    int ivspread;
    int iresidual;


    void initPalette();
    void flameActive();
    void flameAdvance();
};

#endif // FIRE_H
