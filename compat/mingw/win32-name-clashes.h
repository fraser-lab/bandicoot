/* compat/mingw/win32-name-clashes.h
 *
 * windows.h maps Win32 API names onto their ANSI variants with macros
 * (GetAtomName -> GetAtomNameA). That also renames methods of the same name
 * in MMDB and elsewhere, in any file that sees windows.h, however indirectly.
 * Mapping the ANSI names back to the plain ones, before windows.h is read,
 * leaves those identifiers unchanged. Force-included in MinGW builds.
 */

#ifndef COOT_COMPAT_MINGW_WIN32_NAME_CLASHES_H
#define COOT_COMPAT_MINGW_WIN32_NAME_CLASHES_H

#define AddAtomA AddAtom
#define GetAtomNameA GetAtomName

#endif
