// Bandicoot: timeouts and idle callbacks for code written against GLib's
// main loop.
//
// The wx build runs wx's event loop, not GLib's. On macOS GTK's Quartz
// backend happens to run GLib's loop inside the Cocoa one, but on Windows
// nothing does, so a GLib source added there never fires. These run the
// callback on a wx timer instead, falling back to GLib when there is no wx
// interface (and in the GTK build).
//
// The returned id is for bookkeeping only: a wx timer cannot be removed
// with g_source_remove(); its callback stops it by returning FALSE.

#ifndef BANDICOOT_MAIN_LOOP_HH
#define BANDICOOT_MAIN_LOOP_HH

#include <glib.h>
#include <functional>

#ifdef BANDICOOT_UI_WX
int bandicoot_wx_timeout_add(int interval_ms, std::function<bool()> callback);
#endif

// Call f(data) every interval_ms until it returns FALSE.
inline guint bandicoot_timeout_add(guint interval_ms, GSourceFunc f, gpointer data) {
#ifdef BANDICOOT_UI_WX
   int id = bandicoot_wx_timeout_add(interval_ms, [f, data]() { return f(data) != FALSE; });
   if (id) return id;
#endif
   return g_timeout_add(interval_ms, f, data);
}

// Call f(data) once pending events are handled, again until it returns FALSE.
inline guint bandicoot_idle_add(GSourceFunc f, gpointer data) {
#ifdef BANDICOOT_UI_WX
   int id = bandicoot_wx_timeout_add(1, [f, data]() { return f(data) != FALSE; });
   if (id) return id;
#endif
   return g_idle_add(f, data);
}

#endif // BANDICOOT_MAIN_LOOP_HH
