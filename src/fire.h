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
    Uint32 palette[256];
    SDL_Rect destRect;
    int width, height;   // internal texture dimensions
    int fwidth, fheight; // flame simulation buffer dimensions
    Uint32 rngState;
    bool bufferDirty;

    int hspread, vspread, residual;
    int ihspread, ivspread, iresidual;
    int variance, vartrend;
    bool bloom;

    Uint32 nextRand();
    void initPalette();
    void flameActive();
    void flameAdvance();
};

#endif // FIRE_H
