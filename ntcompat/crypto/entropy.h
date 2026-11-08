/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Random bytes for systems without RtlGenRandom (Windows XP and later).
 *
 * ntcompat_random() uses, in order:
 *   1. CryptoAPI's CryptGenRandom (NT 4.0 and Windows 2000);
 *   2. on NT 3.51, ntcompat's own generator: a SHA-256 pool seeded from
 *      RDSEED/RDRAND when the CPU has them, timing jitter of the CPU's
 *      timestamp counter, high-resolution timers and system state
 *      (performance counters, memory, process and thread times). Output
 *      blocks are hashed from the pool with a counter, and the pool is
 *      rekeyed after every request, so earlier output can't be recomputed
 *      from a later state.
 *
 * The NT 3.51 generator is the weakest link: without RDSEED/RDRAND its
 * unpredictability rests on timer jitter and system state, which is far
 * less than an OS random source offers. See ntcompat/README.md.
 */

#ifndef NTCOMPAT_ENTROPY_H
#define NTCOMPAT_ENTROPY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Which source served the last request (for diagnostics). */
#define NTCOMPAT_RANDOM_NONE      0
#define NTCOMPAT_RANDOM_CRYPTOAPI 1
#define NTCOMPAT_RANDOM_POOL      2  /* ntcompat's own generator */

/* Fills *buf* with *len* random bytes. Returns 1 on success, 0 on failure
 * (the pool never fails). Thread-safe. */
int ntcompat_random(void *buf, size_t len);

/* ntcompat's own generator only, regardless of CryptoAPI (for tests). */
int ntcompat_random_pool(void *buf, size_t len);

int ntcompat_random_last_source(void);

/* Nonzero if the CPU's RDSEED/RDRAND instructions seeded the pool. */
int ntcompat_random_pool_has_hw(void);

/* SHA-256, exposed for the tests. out must hold 32 bytes. */
void ntcompat_sha256(const void *data, size_t len, unsigned char out[32]);

#ifdef __cplusplus
}
#endif

#endif /* NTCOMPAT_ENTROPY_H */
