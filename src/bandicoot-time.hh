// Bandicoot: portable monotonic-time helper.
//
// Coot used glutGet(GLUT_ELAPSED_TIME) as its millisecond timer source.
// Bandicoot no longer calls into freeglut on macOS -- glutInit() forced a
// pointless XQuartz launch, and freeglut 3.x aborts its geometry/font
// calls when glutInit() was never run -- so the elapsed-time source is
// provided here instead. Portable across platforms.

#ifndef BANDICOOT_TIME_HH
#define BANDICOOT_TIME_HH

#include <chrono>

namespace coot {

   // Milliseconds since the first call, monotonic. Drop-in replacement for
   // glutGet(GLUT_ELAPSED_TIME); callers only ever use the difference of two
   // readings, so the (arbitrary) zero point does not matter.
   inline long elapsed_time_ms() {
      static const std::chrono::steady_clock::time_point t0 =
         std::chrono::steady_clock::now();
      return static_cast<long>(
         std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count());
   }

}

#endif // BANDICOOT_TIME_HH
