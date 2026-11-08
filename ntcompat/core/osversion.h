/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * The Windows version ntcompat decides for: the real one, or the one named
 * by NTCOMPAT_EMULATE (nt351, nt4, w2k, xp) so fallback paths can be tested
 * on newer Windows. Only APIs present on Windows NT 3.1 are used.
 */

#ifndef NTCOMPAT_OSVERSION_H
#define NTCOMPAT_OSVERSION_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ntcompat_osversion {
    unsigned major;
    unsigned minor;
    int emulated;  /* nonzero when NTCOMPAT_EMULATE chose the version */
} ntcompat_osversion;

/* Computed on first use; safe to call from any thread and from CRT start-up. */
const ntcompat_osversion *ntcompat_os(void);

/* Nonzero if the effective version is at least major.minor. */
int ntcompat_os_at_least(unsigned major, unsigned minor);

#ifdef __cplusplus
}
#endif

#endif /* NTCOMPAT_OSVERSION_H */
