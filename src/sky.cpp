#include "sky.h"
#include "PerlinNoise.h"
#include <algorithm>

namespace {
constexpr int kMaxSkyTexWidth = 800;
constexpr int kMaxSkyTexHeight = 200;
} // namespace

Sky::Sky(SDL_Renderer* renderer, int screenWidth, int screenHeight)
    : renderer(renderer),
      width(screenWidth),
      height(screenHeight / 3),
      texWidth(std::max(2, std::min(screenWidth, kMaxSkyTexWidth) & ~1)),
      texHeight(std::max(1, std::min(screenHeight / 3, kMaxSkyTexHeight))),
      skyTexture(nullptr),
      scrollOffset(0.0f) {
    skyTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, texWidth, texHeight);
    if (skyTexture == nullptr) {
        return;
    }

    PerlinNoise pn;
    std::vector<Uint32> pixels(texWidth * texHeight);
    int halfWidth = texWidth / 2;
    float invHeight = 1.0f / static_cast<float>(texHeight);

    for (int y = 0; y < texHeight; ++y) {
        float gradient = 1.0f - static_cast<float>(y) * invHeight;
        float gradSq = gradient * gradient;
        Uint32* row = pixels.data() + y * texWidth;

        for (int x = 0; x < halfWidth; ++x) {
            double frequency = 0.01;
            double amplitude = 1.0;
            double noiseValue = 0.0;
            for (int i = 0; i < 4; ++i) {
                noiseValue += pn.noise(x * frequency, y * frequency) * amplitude;
                frequency *= 2.0;
                amplitude *= 0.5;
            }

            // Map the noise value to a grayscale color and apply vertical gradient
            Uint8 gray = static_cast<Uint8>((noiseValue + 1.0) * 64.0 + 64.0);
            gray = static_cast<Uint8>(gray * gradSq);

            Uint32 pixel = (0xFFu << 24) | (static_cast<Uint32>(gray) << 16) | (static_cast<Uint32>(gray) << 8) | static_cast<Uint32>(gray);
            row[x] = pixel;
            row[texWidth - 1 - x] = pixel;
        }
    }

    SDL_UpdateTexture(skyTexture, nullptr, pixels.data(), texWidth * sizeof(Uint32));
}

Sky::~Sky() {
    if (skyTexture) {
        SDL_DestroyTexture(skyTexture);
    }
}

void Sky::update() {
    scrollOffset += 0.5f;
    if (scrollOffset >= static_cast<float>(texWidth)) {
        scrollOffset = 0.0f;
    }
}

void Sky::render() {
    if (!skyTexture) return;

    int iScrollOffset = static_cast<int>(scrollOffset) % texWidth;
    int firstPartTexW = texWidth - iScrollOffset;
    int firstPartScreenW = (width * firstPartTexW) / texWidth;

    SDL_Rect src1 = { iScrollOffset, 0, firstPartTexW, texHeight };
    SDL_Rect dst1 = { 0, 0, firstPartScreenW, height };
    SDL_RenderCopy(renderer, skyTexture, &src1, &dst1);

    if (iScrollOffset > 0) {
        SDL_Rect src2 = { 0, 0, iScrollOffset, texHeight };
        SDL_Rect dst2 = { firstPartScreenW, 0, width - firstPartScreenW, height };
        SDL_RenderCopy(renderer, skyTexture, &src2, &dst2);
    } else {
        // Keep 2 RenderCopy calls or single full copy; let's issue an empty-safe copy or single copy
        SDL_Rect src2 = { 0, 0, 1, texHeight };
        SDL_Rect dst2 = { width, 0, 0, height };
        SDL_RenderCopy(renderer, skyTexture, &src2, &dst2);
    }
}
