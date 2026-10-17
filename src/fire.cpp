#include "fire.h"
#include <algorithm>
#include <ctime>

namespace {
constexpr int kMaxFireTexWidth = 800;
constexpr int kMaxFireTexHeight = 200;
} // namespace

Fire::Fire(SDL_Renderer* renderer, int screenWidth, int screenHeight)
    : renderer(renderer),
      destRect{ 0, (screenHeight / 3) * 2, screenWidth, screenHeight / 3 },
      width(std::max(4, std::min(screenWidth, kMaxFireTexWidth))),
      height(std::max(4, std::min(screenHeight / 3, kMaxFireTexHeight))),
      fwidth(width / 2),
      fheight(height / 2),
      rngState(static_cast<Uint32>(std::time(nullptr)) ^ 0x9E3779B9u),
      bufferDirty(true) {
    if (rngState == 0) {
        rngState = 1;
    }

    fireBuffer.resize(fwidth * fheight, 0);
    fireTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    initPalette();

    hspread = 30;
    vspread = 97;
    residual = 99;
    variance = 50;
    vartrend = 20;
    bloom = true;

    ihspread = hspread;
    ivspread = vspread;
    iresidual = residual;

    Uint8* bottomRow = fireBuffer.data() + (fheight - 1) * fwidth;
    for (int x = 0; x < fwidth; ++x) {
        bottomRow[x] = 255;
    }
}

Fire::~Fire() {
    if (fireTexture) {
        SDL_DestroyTexture(fireTexture);
    }
}

Uint32 Fire::nextRand() {
    Uint32 x = rngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rngState = x;
    return x;
}

void Fire::initPalette() {
    for (int i = 0; i < 256; ++i) {
        Uint8 r, g, b;
        if (i < 64) {
            r = static_cast<Uint8>(i * 4);
            g = 0;
            b = 0;
        } else if (i < 128) {
            r = 255;
            g = static_cast<Uint8>((i - 64) * 4);
            b = 0;
        } else if (i < 192) {
            r = 255;
            g = 255;
            b = static_cast<Uint8>((i - 128) * 4);
        } else {
            r = 255;
            g = 255;
            b = 255;
        }
        palette[i] = (255u << 24) | (static_cast<Uint32>(r) << 16) | (static_cast<Uint32>(g) << 8) | static_cast<Uint32>(b);
    }
}

void Fire::update() {
    flameActive();
    flameAdvance();
    bufferDirty = true;
}

void Fire::flameActive() {
    Uint8* bottomRow = fireBuffer.data() + (fheight - 1) * fwidth;
    for (int x = 0; x < fwidth; ++x) {
        int v1 = bottomRow[x];
        v1 += static_cast<int>(nextRand() % static_cast<Uint32>(variance)) - vartrend;
        if (v1 < 0) v1 = 0;
        bottomRow[x] = static_cast<Uint8>(v1 & 255);
    }

    if (bloom) {
        Uint32 v1 = nextRand() % 100u;
        if (v1 == 10u)
            residual += static_cast<int>(nextRand() % 10u);
        else if (v1 == 20u)
            hspread += static_cast<int>(nextRand() % 15u);
        else if (v1 == 30u)
            vspread += static_cast<int>(nextRand() % 20u);
    }

    residual = ((iresidual * 10) + (residual * 90)) / 100;
    hspread = ((ihspread * 10) + (hspread * 90)) / 100;
    vspread = ((ivspread * 10) + (vspread * 90)) / 100;
}

void Fire::flameAdvance() {
    Uint8* buf = fireBuffer.data();
    for (int y = fheight - 1; y > 0; --y) {
        Uint8* row = buf + y * fwidth;
        Uint8* rowAbove = buf + (y - 1) * fwidth;
        bool coolCurrentRow = (y < fheight - 1);

        for (int x = 0; x < fwidth; ++x) {
            int v1 = row[x];
            if (v1 > 0) {
                Uint8* p_above = rowAbove + x;

                // Vertical spread
                int v3 = (v1 * vspread) >> 8;
                int v2 = *p_above + v3;
                *p_above = static_cast<Uint8>(v2 > 255 ? 255 : v2);

                // Horizontal spread
                v3 = (v1 * hspread) >> 8;
                if (x > 0) {
                    v2 = *(p_above - 1) + v3;
                    *(p_above - 1) = static_cast<Uint8>(v2 > 255 ? 255 : v2);
                }
                if (x < fwidth - 1) {
                    v2 = *(p_above + 1) + v3;
                    *(p_above + 1) = static_cast<Uint8>(v2 > 255 ? 255 : v2);
                }

                // Cool down current cell
                if (coolCurrentRow) {
                    row[x] = static_cast<Uint8>((v1 * residual) >> 8);
                }
            }
        }
    }

    for (int x = 0; x < fwidth; ++x) {
        buf[x] = static_cast<Uint8>((buf[x] * residual) >> 8);
    }
}

void Fire::render() {
    if (!fireTexture) return;

    if (bufferDirty) {
        void* pixels = nullptr;
        int pitch = 0;
        if (SDL_LockTexture(fireTexture, nullptr, &pixels, &pitch) == 0 && pixels) {
            const Uint8* buf = fireBuffer.data();
            Uint8* dstBytes = static_cast<Uint8*>(pixels);

            for (int y = 0; y < fheight; ++y) {
                const Uint8* srcRow0 = buf + y * fwidth;
                const Uint8* srcRow1 = (y + 1 < fheight) ? (srcRow0 + fwidth) : srcRow0;

                Uint32* dstRow0 = reinterpret_cast<Uint32*>(dstBytes + (y * 2) * pitch);
                Uint32* dstRow1 = reinterpret_cast<Uint32*>(dstBytes + (y * 2 + 1) * pitch);

                for (int x = 0; x < fwidth - 1; ++x) {
                    int v1 = srcRow0[x];
                    int v2 = srcRow0[x + 1];
                    int v3 = srcRow1[x];
                    int v4 = srcRow1[x + 1];

                    dstRow0[x * 2] = palette[v1];
                    dstRow0[x * 2 + 1] = palette[(v1 + v2) >> 1];
                    dstRow1[x * 2] = palette[(v1 + v3) >> 1];
                    dstRow1[x * 2 + 1] = palette[(v1 + v4) >> 1];
                }

                int lastX = fwidth - 1;
                int v1 = srcRow0[lastX];
                int v3 = srcRow1[lastX];
                Uint32 pTop = palette[v1];
                Uint32 pBot = palette[(v1 + v3) >> 1];
                dstRow0[lastX * 2] = pTop;
                dstRow0[lastX * 2 + 1] = pTop;
                dstRow1[lastX * 2] = pBot;
                dstRow1[lastX * 2 + 1] = pBot;
            }

            SDL_UnlockTexture(fireTexture);
            bufferDirty = false;
        }
    }

    SDL_RenderCopy(renderer, fireTexture, nullptr, &destRect);
}
