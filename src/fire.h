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

    void initPalette();
};

#endif // FIRE_H
