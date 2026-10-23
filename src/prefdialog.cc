/*
 * Copyright (c) 2002-2007  Daniel Elstner  <daniel.kitta@gmail.com>
 *
 * This file is part of regexxer.
 *
 * regexxer is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * regexxer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with regexxer; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "prefdialog.h"
#include "globalstrings.h"
#include "stringutils.h"
#include "translation.h"
#include "settings.h"

#include <glib.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/colorbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/dialog.h>
#include <gtkmm/entry.h>
#include <gtkmm/fontbutton.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/notebook.h>
#include <giomm/settingsschema.h>
#include <vector>

#include <config.h>

namespace Regexxer
{

/**** Regexxer::PrefDialog *************************************************/

PrefDialog::PrefDialog(Gtk::Window& parent)
:
  dialog_                 (),
  button_textview_font_   (0),
  button_match_color_     (0),
  button_current_color_   (0),
  combo_fallback_         (0),
  combo_language_         (0),
  label_language_hint_    (0)
{
  load_xml();
  dialog_->set_transient_for(parent);

  initialize_configuration();
  connect_signals();
}

PrefDialog::~PrefDialog()
{}

void PrefDialog::load_xml()
{
  using namespace Gtk;

  // GTK4: the historic prefdialog.ui is GTK2 (GtkVBox, type_hint, stock…)
  // and Gtk::Builder rejects it. Build the dialog in code instead.
  dialog_.reset(new Gtk::Dialog(_("Preferences")));
  dialog_->set_default_size(480, 320);
  dialog_->add_button(_("_Close"), Gtk::ResponseType::CLOSE);

  Notebook* const notebook = Gtk::make_managed<Notebook>();
  notebook->set_margin(10);
  notebook->set_vexpand(true);
  notebook->set_hexpand(true);
  dialog_->get_content_area()->append(*notebook);

  // ---- Page 1: Look'n'feel ----
  Grid* const table_look = Gtk::make_managed<Grid>();
  table_look->set_margin(10);
  table_look->set_column_spacing(12);
  table_look->set_row_spacing(12);
  notebook->append_page(*table_look, Util::translate_ui("_Look&#x2019;n&#x2019;feel"));

  Label* const label_font = Gtk::make_managed<Label>(Util::translate_ui("_Text view font:"), true);
  label_font->set_halign(Gtk::Align::START);
  table_look->attach(*label_font, 0, 0);
  button_textview_font_ = Gtk::make_managed<FontButton>();
  table_look->attach(*button_textview_font_, 1, 0);
  label_font->set_mnemonic_widget(*button_textview_font_);

  Label* const label_match = Gtk::make_managed<Label>(Util::translate_ui("_Match color:"), true);
  label_match->set_halign(Gtk::Align::START);
  table_look->attach(*label_match, 0, 1);
  button_match_color_ = Gtk::make_managed<ColorButton>();
  table_look->attach(*button_match_color_, 1, 1);
  label_match->set_mnemonic_widget(*button_match_color_);

  Label* const label_current = Gtk::make_managed<Label>(Util::translate_ui("C_urrent match color:"), true);
  label_current->set_halign(Gtk::Align::START);
  table_look->attach(*label_current, 0, 2);
  button_current_color_ = Gtk::make_managed<ColorButton>();
  table_look->attach(*button_current_color_, 1, 2);
  label_current->set_mnemonic_widget(*button_current_color_);

  // ---- Page 2: File access (fallback encoding + language) ----
  Grid* const table_file = Gtk::make_managed<Grid>();
  table_file->set_margin(10);
  table_file->set_column_spacing(12);
  table_file->set_row_spacing(12);
  notebook->append_page(*table_file, Util::translate_ui("_File access"));

  Label* const label_utf8 = Gtk::make_managed<Label>("UTF-8");
  table_file->attach(*label_utf8, 0, 0);
  Label* const label_locale = Gtk::make_managed<Label>(_("The encoding specified by the current locale"));
  table_file->attach(*label_locale, 0, 1);

  Box* const box_fallback = Gtk::make_managed<Box>(Gtk::Orientation::HORIZONTAL, 6);
  table_file->attach(*box_fallback, 0, 2, 2, 1);
  Label* const label_fallback = Gtk::make_managed<Label>(Util::translate_ui("Fallback _encoding:"), true);
  box_fallback->append(*label_fallback);
  combo_fallback_ = Gtk::make_managed<ComboBoxText>();
  combo_fallback_->set_hexpand(true);
  box_fallback->append(*combo_fallback_);
  label_fallback->set_mnemonic_widget(*combo_fallback_);

  Label* const label_lang = Gtk::make_managed<Label>(_("Language:"), true);
  label_lang->set_halign(Gtk::Align::START);
  table_file->attach(*label_lang, 0, 3);
  combo_language_ = Gtk::make_managed<ComboBoxText>();
  combo_language_->set_hexpand(true);
  table_file->attach(*combo_language_, 1, 3);
  label_lang->set_mnemonic_widget(*combo_language_);

  label_language_hint_ = Gtk::make_managed<Label>(
      _("Restart the application to apply the language."));
  label_language_hint_->set_wrap(true);
  table_file->attach(*label_language_hint_, 0, 4, 2, 1);
}

void PrefDialog::connect_signals()
{
  dialog_->signal_response().connect(
      sigc::mem_fun(*this, &PrefDialog::on_response));

  button_textview_font_->signal_font_set().connect(
      sigc::mem_fun(*this, &PrefDialog::on_textview_font_set));

  button_match_color_->signal_color_set().connect(
      sigc::mem_fun(*this, &PrefDialog::on_match_color_set));

  button_current_color_->signal_color_set().connect(
      sigc::mem_fun(*this, &PrefDialog::on_current_color_set));

  combo_fallback_->signal_changed().connect(
      sigc::mem_fun(*this, &PrefDialog::on_fallback_changed));

  combo_language_->signal_changed().connect(
      sigc::mem_fun(*this, &PrefDialog::on_language_changed));
}

void PrefDialog::on_response(int)
{
  dialog_->hide();
}

void PrefDialog::on_conf_value_changed(const Glib::ustring& key)
{
  Glib::RefPtr<Gio::Settings> settings = Settings::instance();

  if (key.raw() == conf_key_textview_font)
  {
    button_textview_font_->set_font(settings->get_string(key));
  }
  else if (key.raw() == conf_key_match_color)
  {
    button_match_color_->set_rgba(Gdk::RGBA(settings->get_string(key)));
  }
  else if (key.raw() == conf_key_current_match_color)
  {
    button_current_color_->set_rgba(Gdk::RGBA(settings->get_string(key)));
  }
  else if (key.raw() == conf_key_fallback_encoding)
  {
    fill_fallback_combo(settings->get_string(key));
  }
  else if (key.raw() == conf_key_preferred_language)
  {
    fill_language_combo(resolve_language(settings->get_string(key)));
  }
}

void PrefDialog::fill_fallback_combo(const Glib::ustring& current)
{
  static const char* const encodings[] = {
    "UTF-8", "ISO-8859-1", "ISO-8859-15", "WINDOWS-1252", "IBM850",
    "ISO-8859-2", "ISO-8859-5", "KOI8-R", "SHIFT_JIS", "EUC-JP",
    "GB18030", "BIG5", 0
  };
  combo_fallback_->remove_all();
  for (const char* const* p = encodings; *p; ++p)
    combo_fallback_->append(*p, *p);
  const Glib::ustring upper = current.uppercase();
  combo_fallback_->set_active_id(upper);
  if (combo_fallback_->get_active_row_number() < 0 && !current.empty())
  {
    combo_fallback_->prepend(current, current);
    combo_fallback_->set_active(0);
  }
  else if (combo_fallback_->get_active_row_number() < 0)
  {
    combo_fallback_->set_active_id("ISO-8859-15");
  }
}

Glib::ustring PrefDialog::resolve_language(const Glib::ustring& stored)
{
  // Empty = system default: show the actual system language in the combo
  // so the list never looks empty.
  if (!stored.empty())
    return stored;
  const char* lang = g_getenv("LANGUAGE");
  if (!lang || !*lang)
    lang = g_getenv("LANG");
  Glib::ustring sys = lang ? lang : "";
  // Keep only the language code part (fr_FR.UTF-8 -> fr, sr@Latn -> sr@Latn).
  const std::string::size_type dot = sys.raw().find_first_of("._@");
  // NB: keep "@..." variants (sr@Latn) intact.
  Glib::ustring code = sys;
  if (dot != std::string::npos && sys.raw()[dot] != '@')
    code = sys.substr(0, dot);
  // Map back to a known code when possible.
  for (const auto& entry : available_languages())
  {
    if (entry.code == code || entry.code == sys)
      return entry.code;
  }

  // Language not in list: return the system language code so the combo
  // can show it as the selected item.
  return code;
}

void PrefDialog::fill_language_combo(const Glib::ustring& current)
{
  // Block the changed handler while refilling.
  sigc::connection block = combo_language_->signal_changed().connect(sigc::mem_fun(*this, &PrefDialog::on_language_changed));
  block.block();

  combo_language_->remove_all();
  // value "" = system default; keep the visible label translatable-free
  // on purpose (language names stay in their own language).
  combo_language_->append("", _("System default"));
  for (const auto& lang : available_languages())
    combo_language_->append(lang.code, lang.label);

  combo_language_->set_active_id(current);
  if (combo_language_->get_active_row_number() < 0)
    combo_language_->set_active(0);

  block.disconnect();
}

std::vector<PrefDialog::LanguageEntry> PrefDialog::available_languages()
{
  // Keep in sync with po/LINGUAS.
  static const char* const codes[] = {
    "ar","bg","ca","cs","da","de","dz","el","en_CA","en_GB","es","eu",
    "fi","fr","gl","gu","hu","it","ja","nb","ne","nl","oc","pa","pl",
    "pt","pt_BR","ru","rw","sk","sl","sq","sr","sr@Latn","sv","ta",
    "uk","vi","zh_CN","zh_HK","zh_TW", 0
  };
  std::vector<LanguageEntry> result;
  for (const char* const* p = codes; *p; ++p)
  {
    LanguageEntry entry;
    entry.code = *p;
    entry.label = Glib::ustring(*p);
    result.push_back(entry);
  }
  return result;
}

void PrefDialog::on_language_changed()
{
  Glib::ustring id = combo_language_->get_active_id();
  Settings::instance()->set_string(conf_key_preferred_language, id);
}

void PrefDialog::initialize_configuration()
{
  Glib::RefPtr<Gio::Settings> settings = Settings::instance();
  const std::vector<Glib::ustring> entries = settings->property_settings_schema().get_value()->list_keys();

  for (std::vector<Glib::ustring>::const_iterator p = entries.begin(); p != entries.end(); ++p)
    on_conf_value_changed(*p);

  // GTK4 FontButton exposes the "font" property (GSettings key holds a
  // Pango font description string like "Monospace 10").
  settings->bind(conf_key_textview_font, button_textview_font_, "font");

  if (settings->property_settings_schema().get_value()->has_key(conf_key_textview_font))
  {
    const Glib::ustring font = settings->get_string(conf_key_textview_font);
    if (!font.empty())
      button_textview_font_->set_font(font);
  }
}

void PrefDialog::on_textview_font_set()
{
  const Glib::ustring value = button_textview_font_->get_font();
  Settings::instance()->set_string(conf_key_textview_font, value);
}

void PrefDialog::on_match_color_set()
{
  const Glib::ustring value = button_match_color_->get_rgba().to_string();
  Settings::instance()->set_string(conf_key_match_color, value);
}

void PrefDialog::on_current_color_set()
{
  const Glib::ustring value = button_current_color_->get_rgba().to_string();
  Settings::instance()->set_string(conf_key_current_match_color, value);
}

void PrefDialog::on_fallback_changed()
{
  const Glib::ustring fallback_encoding = combo_fallback_->get_active_id();

  if (fallback_encoding.empty())
    return;

  if (Util::validate_encoding(fallback_encoding.raw()))
  {
    Settings::instance()->set_string(conf_key_fallback_encoding,
                                     fallback_encoding.uppercase());
  }
  else
  {
    const Glib::ustring message =
        Util::compose(_("\342\200\234%1\342\200\235 is not a valid encoding."), fallback_encoding);

    Gtk::MessageDialog error_dialog (*dialog_, message, false,
                                     Gtk::MessageType::ERROR, Gtk::ButtonsType::OK, true);
    error_dialog.signal_response().connect([&error_dialog](int) { error_dialog.hide(); });
    error_dialog.show();
  }
}

} // namespace Regexxer
