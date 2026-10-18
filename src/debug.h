#ifndef REGEXXER_DEBUG_H_INCLUDED
#define REGEXXER_DEBUG_H_INCLUDED

#include <glib.h>
#include <cstdarg>
#include <cstdio>

namespace Regexxer
{

inline bool debug_enabled()
{
  const gchar* value = g_getenv("REGEXXER_DEBUG");
  return value && *value && *value != '0';
}

inline void debug_log(const char* format, ...)
{
  if (!debug_enabled())
    return;

  va_list args;
  va_start(args, format);
  gchar* message = g_strdup_vprintf(format, args);
  va_end(args);

  g_printerr("[regexxer +%" G_GINT64_FORMAT "us] %s\n",
             g_get_monotonic_time(), message ? message : "(message unavailable)");
  fflush(stderr);
  g_free(message);
}

} // namespace Regexxer

#endif /* REGEXXER_DEBUG_H_INCLUDED */
