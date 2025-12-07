#ifndef SKY_H
#define SKY_H

#include <SDL.h>
#include <vector>

class Sky {
public:
    Sky(SDL_Renderer* renderer, int screenWidth, int screenHeight);
    ~Sky();

    void update();
    void render();

private:
    SDL_Renderer* renderer;
    int width;
    int height;
    SDL_Texture* skyTexture;
    float scrollOffset;
};

#endif // SKY_H
