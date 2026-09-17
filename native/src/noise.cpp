// PCG arithmetic and normal distribution adapted from NumPy 2.4.4.
// See third_party/NUMPY-LICENSE.txt and PCG-LICENSE.md.
#include "noise.h"
#include "../third_party/ziggurat_constants.h"
#include <cmath>
uint64_t Noise::next() {
    const uint64_t m = 4865540595714422341ULL;
    uint64_t a = lo & 0xffffffff, b = lo >> 32, c = m & 0xffffffff, d = m >> 32;
    uint64_t t = b * c + ((a * c) >> 32), w = t & 0xffffffff, h = t >> 32;
    w += a * d;
    uint64_t productHigh = b * d + h + (w >> 32), productLow = lo * m;
    productHigh += hi * m + lo * 2549297995355413924ULL;
    lo = productLow + 0x1406a8451555a563ULL;
    hi = productHigh + 0x6b4b19cd6cd8e6e0ULL + (lo < productLow);
    uint64_t value = hi ^ lo;
    unsigned rot = unsigned(hi >> 58);
    return (value >> rot) | (value << ((64 - rot) & 63));
}
double Noise::uniform() {
    return double(next() >> 11) * (1.0 / 9007199254740992.0);
}
double Noise::normal() {
    for (;;) {
        uint64_t r = next();
        int idx = int(r & 255);
        r >>= 8;
        int sign = int(r & 1);
        uint64_t rabs = (r >> 1) & 0x000fffffffffffffULL;
        double x = rabs * wi_double[idx];
        if (sign)
            x = -x;
        if (rabs < ki_double[idx])
            return x;
        if (idx == 0) {
            for (;;) {
                double xx = -ziggurat_nor_inv_r * std::log1p(-uniform()),
                       yy = -std::log1p(-uniform());
                if (yy + yy > xx * xx)
                    return ((rabs >> 8) & 1) ? -(ziggurat_nor_r + xx) : ziggurat_nor_r + xx;
            }
        } else if ((fi_double[idx - 1] - fi_double[idx]) * uniform() + fi_double[idx] <
                   std::exp(-.5 * x * x))
            return x;
    }
}
