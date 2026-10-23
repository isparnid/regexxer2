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
        const std::string value = lang.raw() + ".UTF-8";
        g_setenv("LANGUAGE", lang.c_str(), TRUE);
        g_setenv("LANG", value.c_str(), TRUE);
        setlocale(LC_ALL, "");
      }
    }
  }
  catch (...)
  {}
}

} // anonymous namespace


int main(int argc, char** argv)
{
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
    g_error("unhandled exception: %s", what.c_str());
  }
  catch (const std::exception& ex)
  {
    g_error("unhandled exception: %s", ex.what());
  }
  catch (...)
  {
    g_error("unhandled exception: (type unknown)");
  }

  return 0;
}

