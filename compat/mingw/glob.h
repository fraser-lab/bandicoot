/* compat/mingw/glob.h
 *
 * POSIX glob() for MinGW-w64, whose C library has none. On the include path
 * only for MinGW builds (GLOB_CFLAGS). Covers what the tree uses: * ? [...]
 * in the last path component, GLOB_APPEND, sorted results.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 */

#ifndef COOT_COMPAT_MINGW_GLOB_H
#define COOT_COMPAT_MINGW_GLOB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
   size_t gl_pathc;   /* number of matched paths */
   char **gl_pathv;   /* matched paths, NULL-terminated */
   size_t gl_offs;    /* unused; present for source compatibility */
} glob_t;

#define GLOB_APPEND  0x01

#define GLOB_NOSPACE 1
#define GLOB_ABORTED 2
#define GLOB_NOMATCH 3

int glob(const char *pattern, int flags,
         int (*errfunc)(const char *epath, int eerrno), glob_t *pglob);
void globfree(glob_t *pglob);

#ifdef __cplusplus
}
#endif

#endif /* COOT_COMPAT_MINGW_GLOB_H */
