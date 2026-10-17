#ifndef PERLINNOISE_H
#define PERLINNOISE_H

#include <vector>
#include <numeric>
#include <random>
#include <algorithm>
#include <cmath>

class PerlinNoise {
public:
    PerlinNoise() : PerlinNoise(std::random_device{}()) {
    }

    PerlinNoise(unsigned int seed) {
        std::iota(p, p + 256, 0);
        std::default_random_engine engine(seed);
        std::shuffle(p, p + 256, engine);
        std::copy(p, p + 256, p + 256);
    }

    double noise(double x, double y) const {
        double fx = std::floor(x);
        double fy = std::floor(y);
        int X = static_cast<int>(fx) & 255;
        int Y = static_cast<int>(fy) & 255;
        x -= fx;
        y -= fy;
        double u = fade(x);
        double v = fade(y);
        int A = p[X] + Y;
        int AA = p[A];
        int AB = p[A + 1];
        int B = p[X + 1] + Y;
        int BA = p[B];
        int BB = p[B + 1];
        return lerp(v, lerp(u, grad(AA, x, y),
                               grad(BA, x - 1, y)),
                       lerp(u, grad(AB, x, y - 1),
                               grad(BB, x - 1, y - 1)));
    }

private:
    int p[512];

    static double fade(double t) {
        return t * t * t * (t * (t * 6 - 15) + 10);
    }

    static double lerp(double t, double a, double b) {
        return a + t * (b - a);
    }

    static double grad(int hash, double x, double y) {
        int h = hash & 15;
        double u = h < 8 ? x : y;
        double v = h < 4 ? y : h == 12 || h == 14 ? x : 0;
        return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
    }
};

#endif // PERLINNOISE_H
