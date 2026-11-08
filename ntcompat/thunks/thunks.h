/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * ntcompat import thunks: stand-ins for functions that Windows NT 3.51 or
 * NT 4.0 lack, for imports made by the C runtime, YY-Thunks or our code.
 * The table is thunks.tsv. Link imp_x86.obj and thunks.obj before the system
 * import libraries; callers need no changes.
 */

#ifndef NTCOMPAT_THUNKS_H
#define NTCOMPAT_THUNKS_H

#ifdef __cplusplus
extern "C" {
#endif

/* How a thunked function has been served so far, for diagnostics. */
#define NTCOMPAT_THUNK_UNUSED   (-1)  /* not called yet */
#define NTCOMPAT_THUNK_FALLBACK 0     /* the system lacks it (or emulation hides it) */
#define NTCOMPAT_THUNK_NATIVE   1     /* calls go to the system's function */

/* Status of the thunk for *function* (for example "GetFileSizeEx"),
 * or -2 if it isn't a thunked function. */
int ntcompat_thunk_status(const char *function);

#ifdef __cplusplus
}
#endif

#endif /* NTCOMPAT_THUNKS_H */
