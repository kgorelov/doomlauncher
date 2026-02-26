#include "fire.h"
#include <algorithm>
#include <cstdlib>
#include <ctime>

Fire::Fire(SDL_Renderer* renderer, int screenWidth, int screenHeight)
    : renderer(renderer), width(screenWidth), height(screenHeight / 3) {
    fireBuffer.resize(width * height, 0);
    fireTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    initPalette();
    srand(time(nullptr));

    hspread = 30;
    vspread = 97;
    residual = 99;
    variance = 50;
    vartrend = 20;
    bloom = true;

    ihspread = hspread;
    ivspread = vspread;
    iresidual = residual;

    for (int x = 0; x < width; ++x) {
        fireBuffer[(height - 1) * width + x] = 255;
    }
}

Fire::~Fire() {
    if (fireTexture) {
        SDL_DestroyTexture(fireTexture);
    }
}

void Fire::initPalette() {
    palette.resize(256);
    for (int i = 0; i < 256; ++i) {
        if (i < 64) {
            palette[i] = { (Uint8)(i * 4), 0, 0, 255 };
        } else if (i < 128) {
            palette[i] = { 255, (Uint8)((i - 64) * 4), 0, 255 };
        } else if (i < 192) {
            palette[i] = { 255, 255, (Uint8)((i - 128) * 4), 255 };
        } else {
            palette[i] = { 255, 255, 255, 255 };
        }
    }
}

void Fire::update() {
    flameActive();
    flameAdvance();
}

void Fire::flameActive() {
    for (int x = 0; x < width; ++x) {
        int v1 = fireBuffer[(height - 1) * width + x];
        v1 += (rand() % variance) - vartrend;
        if (v1 < 0) v1 = 0;
        if (v1 > 255) v1 = 255;
        fireBuffer[(height - 1) * width + x] = v1;
    }

    if (bloom) {
        int v1 = (rand() % 100);
        if (v1 == 10)
            residual += (rand() % 10);
        else if (v1 == 20)
            hspread += (rand() % 15);
        else if (v1 == 30)
            vspread += (rand() % 20);
    }

    residual = ((iresidual * 10) + (residual * 90)) / 100;
    hspread = ((ihspread * 10) + (hspread * 90)) / 100;
    vspread = ((ivspread * 10) + (vspread * 90)) / 100;
}

void Fire::flameAdvance() {
    for (int y = height - 1; y > 0; --y) {
        for (int x = 0; x < width; ++x) {
            int v1 = fireBuffer[y * width + x];
            if (v1 > 0) {
                int v3, v2;
                Uint8* p_above = &fireBuffer[(y - 1) * width + x];

                // Vertical spread
                v3 = (v1 * vspread) >> 8;
                v2 = *p_above;
                v2 += v3;
                if (v2 > 255) v2 = 255;
                *p_above = v2;

                // Horizontal spread
                v3 = (v1 * hspread) >> 8;
                if (x > 0) {
                    v2 = *(p_above - 1);
                    v2 += v3;
                    if (v2 > 255) v2 = 255;
                    *(p_above - 1) = v2;
                }
                if (x < width - 1) {
                    v2 = *(p_above + 1);
                    v2 += v3;
                    if (v2 > 255) v2 = 255;
                    *(p_above + 1) = v2;
                }

                // Cool down current cell
                if (y < height - 1) {
                    fireBuffer[y * width + x] = (v1 * residual) >> 8;
                }
            }
        }
    }
}


void Fire::render() {
    void* pixels;
    int pitch;
    SDL_LockTexture(fireTexture, nullptr, &pixels, &pitch);

    Uint32* dst = (Uint32*)pixels;
    for (int i = 0; i < width * height; ++i) {
        SDL_Color color = palette[fireBuffer[i]];
        dst[i] = (color.a << 24) | (color.r << 16) | (color.g << 8) | color.b;
    }

    SDL_UnlockTexture(fireTexture);

    SDL_Rect destRect = { 0, height * 2, width, height };
    SDL_RenderCopy(renderer, fireTexture, nullptr, &destRect);
}
