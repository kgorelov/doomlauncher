#include "fire.h"
#include <algorithm>
#include <cstdlib>
#include <ctime>

Fire::Fire(SDL_Renderer* renderer, int screenWidth, int screenHeight)
    : renderer(renderer), width(screenWidth), height(screenHeight / 3) {
    fwidth = width / 2;
    fheight = height / 2;
    fireBuffer.resize(fwidth * fheight, 0);
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

    for (int x = 0; x < fwidth; ++x) {
        fireBuffer[(fheight - 1) * fwidth + x] = 255;
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
    for (int x = 0; x < fwidth; ++x) {
        int v1 = fireBuffer[(fheight - 1) * fwidth + x];
        v1 += (rand() % variance) - vartrend;
        if (v1 < 0) v1 = 0;
        fireBuffer[(fheight - 1) * fwidth + x] = v1 % 256;
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
    for (int y = fheight - 1; y > 0; --y) {
        for (int x = 0; x < fwidth; ++x) {
            int v1 = fireBuffer[y * fwidth + x];
            if (v1 > 0) {
                int v3, v2;
                Uint8* p_above = &fireBuffer[(y - 1) * fwidth + x];

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
                if (x < fwidth - 1) {
                    v2 = *(p_above + 1);
                    v2 += v3;
                    if (v2 > 255) v2 = 255;
                    *(p_above + 1) = v2;
                }

                // Cool down current cell
                if (y < fheight - 1) {
                    fireBuffer[y * fwidth + x] = (v1 * residual) >> 8;
                }
            }
        }
    }

    for (int x = 0; x < fwidth; ++x) {
        fireBuffer[x] = (fireBuffer[x] * residual) >> 8;
    }
}


void Fire::render() {
    void* pixels;
    int pitch;
    SDL_LockTexture(fireTexture, nullptr, &pixels, &pitch);

    Uint32* dst = (Uint32*)pixels;
    for (int y = 0; y < fheight - 1; ++y) {
        for (int x = 0; x < fwidth - 1; ++x) {
            int v1 = fireBuffer[y * fwidth + x];
            int v2 = fireBuffer[y * fwidth + x + 1];
            int v3 = fireBuffer[(y + 1) * fwidth + x];
            int v4 = fireBuffer[(y + 1) * fwidth + x + 1];

            SDL_Color c1 = palette[v1];
            SDL_Color c2 = palette[(v1 + v2) >> 1];
            SDL_Color c3 = palette[(v1 + v3) >> 1];
            SDL_Color c4 = palette[(v1 + v4) >> 1];

            Uint32 p1 = (c1.a << 24) | (c1.r << 16) | (c1.g << 8) | c1.b;
            Uint32 p2 = (c2.a << 24) | (c2.r << 16) | (c2.g << 8) | c2.b;
            Uint32 p3 = (c3.a << 24) | (c3.r << 16) | (c3.g << 8) | c3.b;
            Uint32 p4 = (c4.a << 24) | (c4.r << 16) | (c4.g << 8) | c4.b;

            dst[(y * 2) * width + (x * 2)] = p1;
            dst[(y * 2) * width + (x * 2) + 1] = p2;
            dst[(y * 2 + 1) * width + (x * 2)] = p3;
            dst[(y * 2 + 1) * width + (x * 2) + 1] = p4;
        }
    }

    SDL_UnlockTexture(fireTexture);

    SDL_Rect destRect = { 0, height * 2, width, height };
    SDL_RenderCopy(renderer, fireTexture, nullptr, &destRect);
}
