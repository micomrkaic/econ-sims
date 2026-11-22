/* rng.h — a small, seedable random number generator (xoshiro256**) with the
 * few draws the simulations need. Same seed, same stream, on every platform,
 * so a model test that passes here passes in the browser.
 *
 * Part of econ-sims. Copyright (C) 2026 Mico Mrkaic.
 * GPL-3.0-or-later; see LICENSE. */
#ifndef ECON_RNG_H
#define ECON_RNG_H
#include <stdint.h>
#include <math.h>

typedef struct { uint64_t s[4]; } Rng;

static inline uint64_t rng_rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

static inline uint64_t rng_next(Rng *r)
{
    uint64_t *s = r->s;
    uint64_t result = rng_rotl(s[1] * 5, 7) * 9, t = s[1] << 17;
    s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3];
    s[2] ^= t; s[3] = rng_rotl(s[3], 45);
    return result;
}

/* seed through splitmix64, so nearby seeds give unrelated streams */
static inline void rng_seed(Rng *r, uint64_t seed)
{
    for (int i = 0; i < 4; i++) {
        uint64_t z = (seed += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        r->s[i] = z ^ (z >> 31);
    }
}

/* uniform on [0, 1) */
static inline double rng_uniform(Rng *r) { return (double)(rng_next(r) >> 11) * 0x1.0p-53; }

/* uniform integer on 0 .. n-1 */
static inline int rng_below(Rng *r, int n) { return (int)(rng_uniform(r) * n); }

/* standard normal (Box-Muller; one draw per call, the pair's twin discarded) */
static inline double rng_normal(Rng *r)
{
    double u = 1.0 - rng_uniform(r), v = rng_uniform(r);
    return sqrt(-2.0 * log(u)) * cos(6.283185307179586 * v);
}

#endif
