#pragma once
#include <cstdint>
// Fixed-seed PCG64 and NumPy's normal distribution preserve the approved noise.
class Noise {
    uint64_t hi = 0x052eb520926cc3a5ULL, lo = 0x624241c3b7fdb6f9ULL;
    uint64_t next();
    double uniform();

  public:
    double normal();
};
