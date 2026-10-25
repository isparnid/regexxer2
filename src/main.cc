/* $Id$
 *
 * Copyright (c) 2004  Daniel Elstner  <daniel.elstner@gmx.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License VERSION 2 as
 * published by the Free Software Foundation.  You are not allowed to
 * use any other version of the license; unless you got the explicit
 * permission from the author to do so.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "globalstrings.h"
#include "debug.h"
#include "mainwindow.h"
#include "miscutils.h"
#include "settings.h"
#include "appsettings.h"
#include "translation.h"

#include <glib.h>
#include <glibmm.h>
#include <giomm/settings.h>
#include <gtkmm/application.h>
#include <gtkmm/window.h>

#include <clocale>
#include <exception>
#include <memory>
#include <vector>

#include <config.h>


namespace
{

void initialize_configuration()
{
  (void) Regexxer::app_settings();
}

void apply_preferred_language()
{
  // Optional override stored in GSettings ("preferred-language", "" = system).
  // Must run before initialize_gettext() so gettext picks it up. Needs the
  // compiled schema installed; silently keep system default otherwise.
  try
  {
    const Glib::RefPtr<Gio::Settings> settings =
        Gio::Settings::create(Regexxer::conf_schema);
    if (settings->property_settings_schema().get_value()->has_key(
            Regexxer::conf_key_preferred_language))
    {
      const Glib::ustring lang = settings->get_string(
          Regexxer::conf_key_preferred_language);
      if (!lang.empty())
      {
        // Build candidate locale names: try "fr.UTF-8", then "fr_FR.UTF-8",
        // then fall back to the raw code.  setlocale() fails silently when
        // the locale is not installed, so we must try several forms.
        const std::string raw = lang.raw();
        const std::string upper = Glib::ustring(raw).uppercase().raw();
        std::vector<std::string> candidates;
        candidates.push_back(raw + ".UTF-8");
        candidates.push_back(raw + "_" + upper + ".UTF-8");
        candidates.push_back(raw);

        bool ok = false;
        for (const auto& cand : candidates)
        {
          g_setenv("LANGUAGE", lang.c_str(), TRUE);
          g_setenv("LANG", cand.c_str(), TRUE);
          if (setlocale(LC_ALL, cand.c_str()) != nullptr)
          {
            ok = true;
            break;
          }
        }

        if (!ok)
        {
          // None of the candidates is installed: keep the system default.
          g_unsetenv("LANGUAGE");
          g_unsetenv("LANG");
        }
      }
    }
  }
  catch (...)
  {}
}

} // anonymous namespace


namespace
{

// Terminate handler: called when an exception escapes a signal handler and
// std::terminate is invoked.  Try to report the exception type before aborting.
void terminate_handler()
{
  std::exception_ptr eptr = std::current_exception();
  if (eptr)
  {
    try
    {
      std::rethrow_exception(eptr);
    }
    catch (const std::exception& ex)
    {
      g_error("terminate: std::exception: %s", ex.what());
    }
    catch (...)
    {
      g_error("terminate: unknown exception type");
    }
  }
  else
  {
    g_error("terminate: no active exception");
  }
  std::abort();
}

} // anonymous namespace

namespace
{

// Global log handler: intercept GTK/GLib "unhandled exception in signal
// handler" errors and demote them to warnings, preventing the application
// from aborting when a C++ exception escapes a signal handler that we may
// have missed protecting with try/catch.
void log_handler(const gchar* log_domain, GLogLevelFlags log_level,
                 const gchar* message, gpointer user_data)
{
  if ((log_level & G_LOG_LEVEL_ERROR) || (log_level & G_LOG_LEVEL_CRITICAL))
  {
    if (g_strstr_len(message, -1, "unhandled exception"))
    {
      // Demote to warning: log it but don't abort the application.
      g_log_default_handler(log_domain, G_LOG_LEVEL_WARNING, message, user_data);
      return;
    }
  }
  g_log_default_handler(log_domain, log_level, message, user_data);
}

} // anonymous namespace

int main(int argc, char** argv)
{
  std::set_terminate(terminate_handler);
  g_log_set_handler(nullptr,
                    static_cast<GLogLevelFlags>(G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL),
                    log_handler, nullptr);
  try
  {
    bool debug = false;
    int write_arg = 1;
    for (int read_arg = 1; read_arg < argc; ++read_arg)
    {
      if (g_strcmp0(argv[read_arg], "--debug") == 0)
        debug = true;
      else
        argv[write_arg++] = argv[read_arg];
    }
    argc = write_arg;
    argv[argc] = 0;

    if (debug)
      g_setenv("REGEXXER_DEBUG", "1", TRUE);
    Regexxer::debug_log("application startup (debug=%s)", debug ? "on" : "off");

    apply_preferred_language();

    /* Initialize locale so that gettext() picks up the system default 
     * (LANG/LANGUAGE environment) when no explicit override is set. */
    setlocale(LC_ALL, "");

    Util::initialize_gettext(GETTEXT_PACKAGE, REGEXXER_LOCALEDIR);
    Glib::set_application_name(PACKAGE_NAME);
    initialize_configuration();

    const Glib::RefPtr<Gtk::Application> application = Gtk::Application::create("org.regexxer");
    std::unique_ptr<Regexxer::MainWindow> window;
    application->signal_activate().connect([&application, &window]()
    {
      if (!window)
      {
        Regexxer::debug_log("creating main window");
        window.reset(new Regexxer::MainWindow());
        application->add_window(*window->get_window());
      }
      Regexxer::debug_log("presenting main window");
      window->get_window()->present();
    });
    return application->run(argc, argv);
  }
  catch (const Glib::Error& error)
  {
    const Glib::ustring what = error.what();
    Regexxer::debug_log("unhandled exception: %s", what.c_str());
    return 1;
  }
  catch (const std::exception& ex)
  {
    Regexxer::debug_log("unhandled exception: %s", ex.what());
    return 1;
  }
  catch (...)
  {
    Regexxer::debug_log("unhandled exception: (type unknown)");
    return 1;
  }

  return 0;
}

