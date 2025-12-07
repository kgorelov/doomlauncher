#include "sky.h"
#include "PerlinNoise.h"
#include <cstdlib>
#include <ctime>

Sky::Sky(SDL_Renderer* renderer, int screenWidth, int screenHeight)
    : renderer(renderer), width(screenWidth), height(screenHeight / 3), scrollOffset(0.0f) {
    skyTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (skyTexture == nullptr) {
        // Handle error
        return;
    }

    PerlinNoise pn;
    std::vector<Uint32> pixels(width * height);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width / 2; ++x) {
            double frequency = 0.01;
            double amplitude = 1.0;
            double noiseValue = 0.0;
            for (int i = 0; i < 4; ++i) {
                noiseValue += pn.noise(x * frequency, y * frequency) * amplitude;
                frequency *= 2;
                amplitude *= 0.5;
            }

            // Map the noise value to a grayscale color
            Uint8 gray = static_cast<Uint8>((noiseValue + 1.0) / 2.0 * 128 + 64);

            // Apply gradient
            float gradient = 1.0f - (float)y / height;
            gray *= gradient * gradient;

            pixels[y * width + x] = (0xFF << 24) | (gray << 16) | (gray << 8) | gray;
            pixels[y * width + (width - 1 - x)] = (0xFF << 24) | (gray << 16) | (gray << 8) | gray;
        }
    }

    SDL_UpdateTexture(skyTexture, nullptr, pixels.data(), width * sizeof(Uint32));
}

Sky::~Sky() {
    if (skyTexture) {
        SDL_DestroyTexture(skyTexture);
    }
}

void Sky::update() {
    scrollOffset += 0.5f;
    if (scrollOffset > width) {
        scrollOffset = 0.0f;
    }
}

void Sky::render() {
    int iScrollOffset = static_cast<int>(scrollOffset);

    SDL_Rect src1 = { iScrollOffset, 0, width - iScrollOffset, height };
    SDL_Rect dst1 = { 0, 0, width - iScrollOffset, height };
    SDL_RenderCopy(renderer, skyTexture, &src1, &dst1);

    SDL_Rect src2 = { 0, 0, iScrollOffset, height };
    SDL_Rect dst2 = { width - iScrollOffset, 0, iScrollOffset, height };
    SDL_RenderCopy(renderer, skyTexture, &src2, &dst2);
}
