#include "fire.h"
#include <cstdlib>
#include <ctime>

Fire::Fire(SDL_Renderer* renderer, int screenWidth, int screenHeight)
    : renderer(renderer), width(screenWidth), height(screenHeight / 3) {
    fireBuffer.resize(width * height, 0);
    fireTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    initPalette();
    srand(time(nullptr));
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
    // Set the bottom row to full heat
    for (int x = 0; x < width; ++x) {
        fireBuffer[(height - 1) * width + x] = 255;
    }

    // Propagate fire upwards
    for (int y = 0; y < height - 1; ++y) {
        for (int x = 0; x < width; ++x) {
            int srcY = y + 1;
            int srcX = x;

            int p1 = fireBuffer[srcY * width + (srcX - 1 + width) % width];
            int p2 = fireBuffer[srcY * width + srcX];
            int p3 = fireBuffer[srcY * width + (srcX + 1) % width];
            int p4 = fireBuffer[(srcY + 1) % height * width + srcX];

            int newValue = (p1 + p2 + p3 + p4) / 4;
            if (newValue > 0) {
                newValue -= rand() % 2; // Cooling
            }

            fireBuffer[y * width + x] = (Uint8)std::max(0, newValue);
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
