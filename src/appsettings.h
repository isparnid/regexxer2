/*
 * AppSettings header - wrapper around Gio::Settings
 */
#ifndef REGEXXER_APPSETTINGS_H_INCLUDED
#define REGEXXER_APPSETTINGS_H_INCLUDED

#include <glibmm/ustring.h>
#include <giomm/settings.h>
#include <giomm/settingsschema.h>
#include <giomm/settingsschemakey.h>
#include <algorithm>
#include <vector>
#include "settings.h"

namespace Regexxer
{

class AppSettings
{
public:
  // Defensive accessors: Gio::Settings::get_*() aborts the whole process
  // with GLib-GIO-ERROR when the key is missing from the installed schema.
  // Guard every access so a missing key degrades to the default instead of crashing.
  static bool has_key(const Glib::RefPtr<Gio::Settings>& settings, const Glib::ustring& key)
  {
    // Gio::SettingsSchema is final and exposed via RefPtr — has_key() needs
    // the full definition from giomm/settingsschema.h (included above).
    const Glib::RefPtr<Gio::SettingsSchema> schema =
        settings->property_settings_schema().get_value();
    return schema && schema->has_key(key);
  }

  Glib::ustring get_string(const Glib::ustring& key, const Glib::ustring& default_value = "") const
  {
    Glib::RefPtr<Gio::Settings> settings = Settings::instance();
    if (!has_key(settings, key))
      return default_value;
    return settings->get_string(key);
  }

  bool get_bool(const Glib::ustring& key, bool default_value = false) const
  {
    Glib::RefPtr<Gio::Settings> settings = Settings::instance();
    if (!has_key(settings, key))
      return default_value;
    return settings->get_boolean(key);
  }

  void set_string(const Glib::ustring& key, const Glib::ustring& value)
  {
    Glib::RefPtr<Gio::Settings> settings = Settings::instance();
    if (!has_key(settings, key))
      return;
    settings->set_string(key, value);
  }

  Glib::SignalProxyDetailed<void(const Glib::ustring&)> signal_changed()
  {
    return Settings::instance()->signal_changed();
  }
};

inline AppSettings& app_settings()
{
  static AppSettings instance;
  return instance;
}

} // namespace Regexxer

#endif
