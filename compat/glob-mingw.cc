/* compat/glob-mingw.cc
 *
 * glob()/globfree() for MinGW-w64 (see compat/mingw/glob.h). Compiles to
 * nothing elsewhere, where the C library provides them.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.
 */

#ifdef _WIN32

#include "mingw/glob.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

// fnmatch() semantics for one path component: * ? [set] [!set] [a-z].
bool match_component(const char *p, const char *s) {

   while (*p) {
      if (*p == '*') {
         while (*p == '*') p++;
         if (!*p) return true;
         for (; *s; s++)
            if (match_component(p, s)) return true;
         return false;
      }
      if (!*s) return false;
      if (*p == '?') {
         p++; s++;
         continue;
      }
      if (*p == '[') {
         const char *q = p + 1;
         bool negate = (*q == '!' || *q == '^');
         if (negate) q++;
         bool hit = false;
         bool first = true;
         while (*q && (first || *q != ']')) {
            first = false;
            if (q[1] == '-' && q[2] && q[2] != ']') {
               if (*s >= q[0] && *s <= q[2]) hit = true;
               q += 3;
            } else {
               if (*s == *q) hit = true;
               q++;
            }
         }
         if (*q == ']') {
            if (hit == negate) return false;
            p = q + 1; s++;
            continue;
         }
         // no closing bracket: '[' is literal
      }
      if (*p != *s) return false;
      p++; s++;
   }
   return *s == '\0';
}

bool has_wildcard(const std::string &s) {
   return s.find_first_of("*?[") != std::string::npos;
}

} // namespace

extern "C" int
glob(const char *pattern, int flags,
     int (*errfunc)(const char *, int), glob_t *pglob) {

   if (!(flags & GLOB_APPEND)) {
      pglob->gl_pathc = 0;
      pglob->gl_pathv = NULL;
   }
   if (!pattern) return GLOB_NOMATCH;

   // Wildcards are honoured in the last component only; the directory part
   // is kept verbatim in the results, as POSIX glob does.
   std::string pat(pattern);
   std::string::size_type sep = pat.find_last_of("/\\");
   std::string prefix = (sep == std::string::npos) ? "" : pat.substr(0, sep + 1);
   std::string leaf   = (sep == std::string::npos) ? pat : pat.substr(sep + 1);
   std::string dir    = prefix.empty() ? "." : prefix;

   std::vector<std::string> found;
   std::error_code ec;

   if (!has_wildcard(leaf)) {
      if (std::filesystem::exists(std::filesystem::path(pat), ec))
         found.push_back(pat);
   } else {
      std::filesystem::directory_iterator it(std::filesystem::path(dir), ec);
      if (ec) {
         if (errfunc && errfunc(dir.c_str(), ec.value()))
            return GLOB_ABORTED;
      } else {
         for (; it != std::filesystem::directory_iterator(); it.increment(ec)) {
            if (ec) break;
            std::string name = it->path().filename().string();
            // a leading dot is matched only explicitly
            if (!name.empty() && name[0] == '.' && leaf[0] != '.')
               continue;
            if (match_component(leaf.c_str(), name.c_str()))
               found.push_back(prefix + name);
         }
      }
   }

   if (found.empty())
      return GLOB_NOMATCH;

   std::sort(found.begin(), found.end());

   size_t n_old = pglob->gl_pathc;
   size_t n_new = n_old + found.size();
   char **v = static_cast<char **>(std::realloc(pglob->gl_pathv,
                                                (n_new + 1) * sizeof(char *)));
   if (!v) return GLOB_NOSPACE;
   pglob->gl_pathv = v;
   for (size_t i = 0; i < found.size(); i++) {
      char *s = static_cast<char *>(std::malloc(found[i].size() + 1));
      if (!s) {
         v[n_old + i] = NULL;
         pglob->gl_pathc = n_old + i;
         return GLOB_NOSPACE;
      }
      std::memcpy(s, found[i].c_str(), found[i].size() + 1);
      v[n_old + i] = s;
   }
   v[n_new] = NULL;
   pglob->gl_pathc = n_new;
   return 0;
}

extern "C" void
globfree(glob_t *pglob) {

   if (!pglob || !pglob->gl_pathv) return;
   for (size_t i = 0; i < pglob->gl_pathc; i++)
      std::free(pglob->gl_pathv[i]);
   std::free(pglob->gl_pathv);
   pglob->gl_pathv = NULL;
   pglob->gl_pathc = 0;
}

#endif // _WIN32
