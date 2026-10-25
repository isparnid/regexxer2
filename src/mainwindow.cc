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

#include "mainwindow.h"
#include "aboutdialog.h"
#include "debug.h"
#include "filetree.h"
#include "globalstrings.h"
#include "gtk4compat.h"
#include <glibmm/regex.h>
#include <giomm/file.h>
#include "prefdialog.h"
#include "statusline.h"
#include "settings.h"
#include "appsettings.h"
#include "appconfig.h"
#include "stringutils.h"
#include "translation.h"

#include <glib.h>
#include <gtkmm/applicationwindow.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/dialog.h>
#include <gtkmm/entry.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/frame.h>
#include <gtkmm/grid.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/paned.h>
#include <gtkmm/popovermenubar.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>
#include <gtkmm/textview.h>
#include <gtkmm/window.h>
#include <giomm/menu.h>
#include <giomm/simpleaction.h>
#include <algorithm>
#include <functional>

#include <config.h>

namespace
{

// Small helper: button with a symbolic icon + text label (classic look).
Gtk::Button* new_icon_button(const Glib::ustring& label, const Glib::ustring& icon_name,
                             bool use_underline = false)
{
  Gtk::Button* const button = Gtk::make_managed<Gtk::Button>();
  Gtk::Box* const content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  Gtk::Image* const icon = Gtk::make_managed<Gtk::Image>();
  icon->set_from_icon_name(icon_name);
  content->append(*icon);
  Gtk::Label* const text = Gtk::make_managed<Gtk::Label>(label, use_underline);
  content->append(*text);
  button->set_child(*content);
  button->set_tooltip_text(label);
  return button;
}

enum { BUSY_GUI_UPDATE_INTERVAL = 16 };

typedef Glib::RefPtr<Regexxer::FileBuffer> FileBufferPtr;

bool matches_config_key(const std::string& key_name, const std::string& expected_key)
{
  return key_name == expected_key;
}

class FileErrorDialog : public Gtk::MessageDialog
{
public:
  FileErrorDialog(Gtk::Window& parent, const Glib::ustring& message,
                  Gtk::MessageType type, const Regexxer::FileTree::Error& error);
  virtual ~FileErrorDialog();
};

FileErrorDialog::FileErrorDialog(Gtk::Window& parent, const Glib::ustring& message,
                                 Gtk::MessageType type, const Regexxer::FileTree::Error& error)
:
  Gtk::MessageDialog(parent, message, false, type, Gtk::ButtonsType::OK, true)
{
  typedef std::list<Glib::ustring> ErrorList;
  const ErrorList& error_list = error.get_error_list();
  Glib::ustring details;

  for (ErrorList::const_iterator perr = error_list.begin(); perr != error_list.end(); ++perr)
    details += *perr + '\n';

  set_secondary_text(details);
  set_default_size(520, 300);
}

FileErrorDialog::~FileErrorDialog()
{}

} // anonymous namespace


namespace Regexxer
{

/**** Regexxer::MainWindow::BusyAction *************************************/

class MainWindow::BusyAction
{
private:
  MainWindow& object_;

  BusyAction(const BusyAction&);
  BusyAction& operator=(const BusyAction&);

public:
  explicit BusyAction(MainWindow& object)
    : object_ (object) { object_.busy_action_enter(); }

  ~BusyAction() { object_.busy_action_leave(); }
};


/**** Regexxer::MainWindow *************************************************/

MainWindow::MainWindow()
:
  vbox_main_              (0),
  menubar_                (0),
  toolbar_                (0),
  entry_folder_           (0),
  entry_pattern_          (0),
  button_recursive_       (0),
  button_hidden_          (0),
  entry_regex_            (0),
  entry_substitution_     (0),
  button_multiple_        (0),
  button_caseless_        (0),
  filetree_               (0),
  textview_               (0),
  entry_preview_          (0),
  statusline_             (0),
  busy_action_running_    (false),
  busy_action_cancel_     (false),
  busy_action_iteration_  (0),
  undo_stack_             (new UndoStack())
{
  load_xml();

  textview_->set_buffer(FileBuffer::create());
  set_title_filename();

  entry_folder_->set_text(Util::shorten_pathname(
      Util::filename_to_utf8_fallback(Glib::get_current_dir())));

  connect_signals();
  on_config_changed(AppConfig::modern_key_name(AppConfig::textview_font));

  entry_pattern_->set_text("*");
}

MainWindow::~MainWindow()
{}

/**** Regexxer::MainWindow -- private **************************************/

void MainWindow::register_action(const Glib::ustring& name, ControlItem& item,
                                 const Glib::ustring& accel)
{
  auto action = Gio::SimpleAction::create(name);
  action->signal_activate().connect(
      [slot = item.slot()](const Glib::VariantBase&) { slot(); });
  item.add_widget_action(action);
  actions_[name] = action;
  action_group_->add_action(action);
  if (!accel.empty())
  {
    if (auto app = window_->get_application())
      app->set_accel_for_action(Glib::ustring("win.") + name, accel);
  }
}

void MainWindow::build_menubar()
{
  // Classic menus from the screenshot / historic mainwindow.ui.
  // Labels reuse historic msgids so po/*.po keeps applying.
  menu_model_ = Gio::Menu::create();
  action_group_ = Gio::SimpleActionGroup::create();
  window_->insert_action_group("win", action_group_);

  register_action("save",         controller_.save_file,   "<Primary>s");
  register_action("save-all",     controller_.save_all);
  register_action("quit",         controller_.quit,        "<Primary>q");
  register_action("undo",         controller_.undo,        "<Primary>z");
  register_action("cut",          controller_.cut,         "<Primary>x");
  register_action("copy",         controller_.copy,        "<Primary>c");
  register_action("paste",        controller_.paste,       "<Primary>v");
  register_action("erase",        controller_.erase,       "Delete");
  register_action("preferences",  controller_.preferences);
  register_action("prev-file",    controller_.prev_file,   "<Primary>p");
  register_action("prev-match",   controller_.prev_match,  "<Primary>b");
  register_action("next-match",   controller_.next_match,  "<Primary>n");
  register_action("next-file",    controller_.next_file,   "<Primary>e");
  register_action("replace",      controller_.replace,     "<Primary>r");
  register_action("replace-file", controller_.replace_file);
  register_action("replace-all",  controller_.replace_all);
  register_action("about",        controller_.about);

  auto file_menu = Gio::Menu::create();
  file_menu->append(_("Save"), "win.save");
  file_menu->append(_("Save all"), "win.save-all");
  file_menu->append(_("Quit"), "win.quit");

  auto edit_menu = Gio::Menu::create();
  edit_menu->append(_("Undo"), "win.undo");
  edit_menu->append(_("Cut"), "win.cut");
  edit_menu->append(_("Copy"), "win.copy");
  edit_menu->append(_("Paste"), "win.paste");
  edit_menu->append(_("Delete"), "win.erase");
  edit_menu->append(_("Preferences"), "win.preferences");

  auto match_menu = Gio::Menu::create();
  match_menu->append(_("File backward"), "win.prev-file");
  match_menu->append(_("Backward"), "win.prev-match");
  match_menu->append(_("Forward"), "win.next-match");
  match_menu->append(_("File forward"), "win.next-file");
  match_menu->append(_("Replace"), "win.replace");
  match_menu->append(_("This file"), "win.replace-file");
  match_menu->append(_("All files"), "win.replace-all");

  auto help_menu = Gio::Menu::create();
  help_menu->append(_("About"), "win.about");

  menu_model_->append_submenu(_("File"), file_menu);
  menu_model_->append_submenu(_("Edit"), edit_menu);
  menu_model_->append_submenu(_("Match"), match_menu);
  menu_model_->append_submenu(_("Help"), help_menu);

  menubar_ = Gtk::make_managed<Gtk::PopoverMenuBar>(menu_model_);
  menubar_->set_hexpand(true);
  vbox_main_->append(*menubar_);
}

void MainWindow::load_xml()
{
  window_.reset(new Gtk::Window());
  window_->set_title(PACKAGE_NAME);

  // Restore geometry from GSettings once the window is realized (after
  // the native surface exists).  Applying the size here (early) triggered
  // an assertion "quark > 0" with some recent Pango / Glib versions on
  // Wayland/X11, because the underlying GObject structures were not
  // fully initialised yet.
  window_->signal_realize().connect(
      sigc::mem_fun(*this, &MainWindow::on_window_realize));

  vbox_main_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  window_->set_child(*vbox_main_);

  build_menubar();

  toolbar_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
  toolbar_->set_margin(4);
  vbox_main_->append(*toolbar_);

  const auto add_toolbar_button = [this](const Glib::ustring& label,
                                          const Glib::ustring& icon_name,
                                          ControlItem& item)
  {
    Gtk::Button* const button = new_icon_button(label, icon_name);
    toolbar_->append(*button);
    item.add_widget(*button);
    button->signal_clicked().connect(item.slot());
  };

  add_toolbar_button(_("Save"), "document-save", controller_.save_file);
  add_toolbar_button(_("Save all"), "document-save-as", controller_.save_all);
  toolbar_->append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL));
  add_toolbar_button(_("Undo"), "edit-undo", controller_.undo);
  toolbar_->append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL));
  add_toolbar_button(_("Quit"), "application-exit", controller_.quit);

  Gtk::Paned* const work_area = Gtk::make_managed<Gtk::Paned>(Gtk::Orientation::HORIZONTAL);
  work_area->set_vexpand(true);
  work_area->set_hexpand(true);
  vbox_main_->append(*work_area);

  // ---- Left pane (historic vbox_left): Dossier/Motif + file list ----
  Gtk::Box* const vbox_left = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
  vbox_left->set_margin(6);
  work_area->set_start_child(*vbox_left);
  work_area->set_position(340);

  Gtk::Grid* const table_file = Gtk::make_managed<Gtk::Grid>();
  table_file->set_column_spacing(6);
  table_file->set_row_spacing(6);
  vbox_left->append(*table_file);

  Gtk::Label* const label_folder = Gtk::make_managed<Gtk::Label>(_("Fol_der:"), true);
  label_folder->set_halign(Gtk::Align::START);
  table_file->attach(*label_folder, 0, 0);

  combobox_folder_ = Gtk::make_managed<Gtk::ComboBoxText>(true);
  combobox_folder_->set_hexpand(true);
  table_file->attach(*combobox_folder_, 1, 0);
  entry_folder_ = combobox_folder_->get_entry();
  label_folder->set_mnemonic_widget(*combobox_folder_);

  Gtk::Button* const folder_button = new_icon_button(_("Browse…"), "folder-open");
  table_file->attach(*folder_button, 2, 0);
  folder_button->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_select_folder));

  Gtk::Label* const label_pattern = Gtk::make_managed<Gtk::Label>(_("Pattern:"));
  label_pattern->set_halign(Gtk::Align::START);
  table_file->attach(*label_pattern, 0, 1);

  entry_pattern_ = Gtk::make_managed<Gtk::Entry>();
  entry_pattern_->set_hexpand(true);
  entry_pattern_->set_tooltip_text(_("Find all files that match the filename pattern"));
  table_file->attach(*entry_pattern_, 1, 1, 2, 1);
  label_pattern->set_mnemonic_widget(*entry_pattern_);

  Gtk::Box* const options_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 3);
  table_file->attach(*options_box, 0, 2);

  button_recursive_ = Gtk::make_managed<Gtk::CheckButton>(_("recursive"), true);
  button_recursive_->set_active(true);
  button_recursive_->set_tooltip_text(_("Recurse into subdirectories"));
  options_box->append(*button_recursive_);

  button_hidden_ = Gtk::make_managed<Gtk::CheckButton>(_("hidden"), true);
  button_hidden_->set_tooltip_text(_("Also find hidden files"));
  options_box->append(*button_hidden_);

  Gtk::Button* const find_files_button = new_icon_button(_("Find fi_les"), "system-search", true);
  find_files_button->set_halign(Gtk::Align::END);
  find_files_button->set_hexpand(true);
  controller_.find_files.add_widget(*find_files_button);
  find_files_button->signal_clicked().connect(controller_.find_files.slot());
  table_file->attach(*find_files_button, 1, 2, 2, 1);

  Gtk::Frame* const frame_filetree = Gtk::make_managed<Gtk::Frame>();
  frame_filetree->set_vexpand(true);
  frame_filetree->set_hexpand(true);
  vbox_left->append(*frame_filetree);

  scrollwin_filetree_ = Gtk::make_managed<Gtk::ScrolledWindow>();
  scrollwin_filetree_->set_vexpand(true);
  frame_filetree->set_child(*scrollwin_filetree_);

  filetree_ = Gtk::make_managed<FileTree>();
  scrollwin_filetree_->set_child(*filetree_);

  // ---- Right pane (historic vbox_right): Search/Replace + editor ----
  Gtk::Box* const vbox_right = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
  vbox_right->set_margin(6);
  work_area->set_end_child(*vbox_right);

  Gtk::Grid* const table_regex = Gtk::make_managed<Gtk::Grid>();
  table_regex->set_column_spacing(6);
  table_regex->set_row_spacing(6);
  vbox_right->append(*table_regex);

  Gtk::Label* const label_search = Gtk::make_managed<Gtk::Label>(_("Search:"), true);
  label_search->set_halign(Gtk::Align::START);
  table_regex->attach(*label_search, 0, 0);

  comboboxentry_regex_ = Gtk::make_managed<Gtk::ComboBoxText>(true);
  comboboxentry_regex_->set_hexpand(true);
  comboboxentry_regex_->set_tooltip_text(_("A regular expression in Perl syntax"));
  table_regex->attach(*comboboxentry_regex_, 1, 0);
  entry_regex_ = comboboxentry_regex_->get_entry();
  label_search->set_mnemonic_widget(*comboboxentry_regex_);

  Gtk::Box* const flags_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  table_regex->attach(*flags_box, 2, 0);

  button_multiple_ = Gtk::make_managed<Gtk::CheckButton>("/g");
  button_multiple_->set_active(true);
  button_multiple_->set_tooltip_text(_("Find all possible matches in a line"));
  flags_box->append(*button_multiple_);

  button_caseless_ = Gtk::make_managed<Gtk::CheckButton>("/i");
  button_caseless_->set_tooltip_text(_("Do case insensitive matching"));
  flags_box->append(*button_caseless_);

  Gtk::Label* const label_replace = Gtk::make_managed<Gtk::Label>(_("Replace:"), true);
  label_replace->set_halign(Gtk::Align::START);
  table_regex->attach(*label_replace, 0, 1);

  comboboxentry_substitution_ = Gtk::make_managed<Gtk::ComboBoxText>(true);
  comboboxentry_substitution_->set_hexpand(true);
  comboboxentry_substitution_->set_tooltip_text(_("Replacement string"));
  table_regex->attach(*comboboxentry_substitution_, 1, 1);
  entry_substitution_ = comboboxentry_substitution_->get_entry();
  label_replace->set_mnemonic_widget(*comboboxentry_substitution_);

  Gtk::Button* const find_matches_button = new_icon_button(_("Search"), "edit-find");
  find_matches_button->set_tooltip_text(_("Find all matches of the regular expression"));
  controller_.find_matches.add_widget(*find_matches_button);
  find_matches_button->signal_clicked().connect(controller_.find_matches.slot());
  table_regex->attach(*find_matches_button, 2, 1);

  Gtk::Frame* const frame_textview = Gtk::make_managed<Gtk::Frame>();
  frame_textview->set_vexpand(true);
  frame_textview->set_hexpand(true);
  vbox_right->append(*frame_textview);

  scrollwin_textview_ = Gtk::make_managed<Gtk::ScrolledWindow>();
  scrollwin_textview_->set_vexpand(true);
  frame_textview->set_child(*scrollwin_textview_);

  textview_ = Gtk::make_managed<Gtk::TextView>();
  textview_->set_monospace(true);
  textview_->set_wrap_mode(Gtk::WrapMode::NONE);
  scrollwin_textview_->set_child(*textview_);

  Gtk::Box* const preview_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  vbox_right->append(*preview_box);

  Gtk::Label* const label_preview = Gtk::make_managed<Gtk::Label>(_("Preview"), true);
  preview_box->append(*label_preview);

  entry_preview_ = Gtk::make_managed<Gtk::Entry>();
  entry_preview_->set_editable(false);
  entry_preview_->set_hexpand(true);
  entry_preview_->set_tooltip_text(_("Preview of the substitution"));
  preview_box->append(*entry_preview_);
  label_preview->set_mnemonic_widget(*entry_preview_);

  const auto add_nav_button = [&](const Glib::ustring& label,
                                  const Glib::ustring& icon_name,
                                  const Glib::ustring& tooltip, ControlItem& item)
  {
    Gtk::Button* const button = new_icon_button(label, icon_name);
    button->set_tooltip_text(tooltip);
    item.add_widget(*button);
    button->signal_clicked().connect(item.slot());
    preview_box->append(*button);
  };
  add_nav_button("|<", "go-first", _("Go to the previous matching file"), controller_.prev_file);
  add_nav_button("<", "go-previous", _("Go to previous match"), controller_.prev_match);
  add_nav_button(">", "go-next", _("Go to next match"), controller_.next_match);
  add_nav_button(">|", "go-last", _("Go to the next matching file"), controller_.next_file);

  Gtk::Box* const replace_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  replace_box->set_halign(Gtk::Align::END);
  vbox_right->append(*replace_box);

  const auto add_replace_button = [&](const Glib::ustring& label,
                                      const Glib::ustring& icon_name, ControlItem& item)
  {
    Gtk::Button* const button = new_icon_button(label, icon_name, true);
    item.add_widget(*button);
    button->signal_clicked().connect(item.slot());
    replace_box->append(*button);
  };
  add_replace_button(_("_Replace"), "edit-find-replace", controller_.replace);
  add_replace_button(_("_This file"), "edit-find-replace", controller_.replace_file);
  add_replace_button(_("_All files"), "edit-find-replace", controller_.replace_all);

  statusline_ = Gtk::make_managed<StatusLine>();
  vbox_main_->append(*statusline_);
}

void MainWindow::on_window_realize()
{
  // Restore geometry from GSettings now that the native surface exists.
  // (Wayland ignores absolute position; size and maximized are restored.)
  AppSettings& settings = app_settings();
  const int width = settings.get_int(AppConfig::modern_key_name(AppConfig::window_width), 980);
  const int height = settings.get_int(AppConfig::modern_key_name(AppConfig::window_height), 680);
  if (width > 0 && height > 0)
  {
    window_->set_default_size(width, height);
  }
  if (settings.get_bool(AppConfig::modern_key_name(AppConfig::window_maximized), false))
  {
    window_->maximize();
  }
}

void MainWindow::connect_signals()
{
  using sigc::bind;
  using sigc::mem_fun;

  window_->signal_hide         ().connect(mem_fun(*this, &MainWindow::on_hide));
  window_->signal_unmap().connect(mem_fun(*this, &MainWindow::save_window_geometry));
  window_->signal_close_request().connect(mem_fun(*this, &MainWindow::on_close_request), false);

  entry_folder_ ->signal_activate().connect(controller_.find_files.slot());
  entry_pattern_->signal_activate().connect(controller_.find_files.slot());
  entry_pattern_->signal_changed ().connect(mem_fun(*this, &MainWindow::on_entry_pattern_changed));

  entry_regex_       ->signal_activate().connect(controller_.find_matches.slot());
  entry_substitution_->signal_activate().connect(controller_.find_matches.slot());
  entry_substitution_->signal_changed ().connect(mem_fun(*this, &MainWindow::update_preview));

  controller_.save_file   .connect(mem_fun(*this, &MainWindow::on_save_file));
  controller_.save_all    .connect(mem_fun(*this, &MainWindow::on_save_all));
  controller_.undo        .connect(mem_fun(*this, &MainWindow::on_undo));
  controller_.preferences .connect(mem_fun(*this, &MainWindow::on_preferences));
  controller_.quit        .connect(mem_fun(*this, &MainWindow::on_quit));
  controller_.about       .connect(mem_fun(*this, &MainWindow::on_about));
  controller_.find_files  .connect(mem_fun(*this, &MainWindow::on_find_files));
  controller_.find_matches.connect(mem_fun(*this, &MainWindow::on_exec_search));
  controller_.next_file   .connect(bind(mem_fun(*this, &MainWindow::on_go_next_file), true));
  controller_.prev_file   .connect(bind(mem_fun(*this, &MainWindow::on_go_next_file), false));
  controller_.next_match  .connect(bind(mem_fun(*this, &MainWindow::on_go_next), true));
  controller_.prev_match  .connect(bind(mem_fun(*this, &MainWindow::on_go_next), false));
  controller_.replace     .connect(mem_fun(*this, &MainWindow::on_replace));
  controller_.replace_file.connect(mem_fun(*this, &MainWindow::on_replace_file));
  controller_.replace_all .connect(mem_fun(*this, &MainWindow::on_replace_all));

  AppSettings& settings = app_settings();
  settings.signal_changed().connect(mem_fun(*this, &MainWindow::on_config_changed));

  statusline_->signal_cancel_clicked.connect(
      mem_fun(*this, &MainWindow::on_busy_action_cancel));

  filetree_->signal_switch_buffer.connect(
      mem_fun(*this, &MainWindow::on_filetree_switch_buffer));

  filetree_->signal_bound_state_changed.connect(
      mem_fun(*this, &MainWindow::on_bound_state_changed));

  filetree_->signal_file_count_changed.connect(
      mem_fun(*this, &MainWindow::on_filetree_file_count_changed));

  filetree_->signal_match_count_changed.connect(
      mem_fun(*this, &MainWindow::on_filetree_match_count_changed));

  filetree_->signal_modified_count_changed.connect(
      mem_fun(*this, &MainWindow::on_filetree_modified_count_changed));

  filetree_->signal_pulse.connect(
      mem_fun(*this, &MainWindow::on_busy_action_pulse));

  filetree_->signal_undo_stack_push.connect(
      mem_fun(*this, &MainWindow::on_undo_stack_push));
}

void MainWindow::on_hide()
{
  on_busy_action_cancel();

  // Kill the dialogs if they're mapped right now.  This isn't strictly
  // necessary since they'd be deleted in the destructor anyway.  But if we
  // have to do a lot of cleanup the dialogs would stay open for that time,
  // which doesn't look neat.

  {
    // Play safe and transfer ownership, and let the dtor do the delete.
    std::unique_ptr<Gtk::Window> temp = std::move(about_dialog_);
  }
  {
    std::unique_ptr<PrefDialog> temp = std::move(pref_dialog_);
  }
}

void MainWindow::save_window_geometry()
{
  // Save the current window geometry (size and maximized state) for
  // the next run.  This is called when the window is hidden or unmapped,
  // i.e. when the user closes the application window.
  int width = 0, height = 0;
  window_->get_default_size(width, height);
  if (width > 0)
    app_settings().set_int(AppConfig::modern_key_name(AppConfig::window_width), width);
  if (height > 0)
    app_settings().set_int(AppConfig::modern_key_name(AppConfig::window_height), height);
  app_settings().set_bool(AppConfig::modern_key_name(AppConfig::window_maximized),
                          window_->is_maximized());
}

void MainWindow::save_window_state()
{
  AppSettings& settings = app_settings();
  int width = 0, height = 0;
  window_->get_default_size(width, height);
  if (width > 0)
    settings.set_int(AppConfig::modern_key_name(AppConfig::window_width), width);
  if (height > 0)
    settings.set_int(AppConfig::modern_key_name(AppConfig::window_height), height);
  settings.set_bool(AppConfig::modern_key_name(AppConfig::window_maximized),
                    window_->is_maximized());
}

bool MainWindow::on_close_request()
{
  save_window_state();
  return !confirm_quit_request();
}

void MainWindow::on_quit()
{
  if (confirm_quit_request())
    window_->hide();
}

bool MainWindow::confirm_quit_request()
{
  if (filetree_->get_modified_count() == 0)
    return true;

  const Glib::ustring message = _("Some files haven't been saved yet.\nQuit anyway?");
  Gtk::MessageDialog dialog (*window_, message, false,
                                              Gtk::MessageType::WARNING,
                                              Gtk::ButtonsType::NONE,
                                              true);

  dialog.add_button(_("_Cancel"), Gtk::ResponseType::CANCEL);
  dialog.add_button(_("_Quit"),   Gtk::ResponseType::OK);

  return (Regexxer::GtkCompat::run_dialog(dialog) == Gtk::ResponseType::OK);
}

void MainWindow::on_select_folder()
{
  // GTK4: use the modern asynchronous Gtk::FileDialog. Keep it alive in a
  // member until the async callback runs.
  folder_dialog_ = Gtk::FileDialog::create();
  folder_dialog_->set_title(_("Select a folder"));
  folder_dialog_->set_accept_label(_("Select"));

  try
  {
    const std::string current_folder = Glib::filename_from_utf8(
        Util::expand_pathname(entry_folder_->get_text()));
    if (!current_folder.empty())
      folder_dialog_->set_initial_folder(Gio::File::create_for_path(current_folder));
  }
  catch (const Glib::Error&)
  {}

  folder_dialog_->select_folder(
      *window_,
      sigc::mem_fun(*this, &MainWindow::on_folder_dialog_done));
}

void MainWindow::on_folder_dialog_done(const Glib::RefPtr<Gio::AsyncResult>& result)
{
  Glib::RefPtr<Gtk::FileDialog> dialog = folder_dialog_;
  folder_dialog_.reset();

  if (!dialog)
    return;

  Glib::RefPtr<Gio::File> selected;
  try
  {
    selected = dialog->select_folder_finish(result);
  }
  catch (const Glib::Error& error)
  {
    // Dismissed, cancelled, or failed: leave the current folder untouched.
    Regexxer::debug_log("folder chooser dismissed: %s", error.what());
    return;
  }

  if (!selected)
    return;

  std::string filename = selected->get_path();
  if (filename.empty())
    return;

  // SELECT_FOLDER always returns a directory: store it directly.
  entry_folder_->set_text(Util::shorten_pathname(
      Glib::filename_to_utf8(filename)));
}

void MainWindow::on_find_files()
{
  try
  {
  if (filetree_->get_modified_count() > 0)
  {
    const Glib::ustring message = _("Some files haven't been saved yet.\nContinue anyway?");
    Gtk::MessageDialog dialog (*window_, message, false, Gtk::MessageType::WARNING,
                   Gtk::ButtonsType::OK_CANCEL, true);

    if (Regexxer::GtkCompat::run_dialog(dialog) != Gtk::ResponseType::OK)
      return;
  }

  undo_stack_clear();

  std::string folder = Glib::filename_from_utf8(Util::expand_pathname(entry_folder_->get_text()));

  if (folder.empty())
    folder = Glib::get_current_dir();

  BusyAction busy (*this);

  try
  {
    const Glib::ustring glob = entry_pattern_->get_text();
    const Glib::ustring regex = Util::shell_pattern_to_regex(glob);
    Regexxer::debug_log("find files: folder='%s', glob='%s', regex='%s', recursive=%d, hidden=%d",
                        folder.c_str(), glob.c_str(), regex.c_str(),
                        button_recursive_->get_active(), button_hidden_->get_active());
    Glib::RefPtr<Glib::Regex> pattern = Glib::Regex::create(regex, Glib::Regex::CompileFlags::DOTALL);

    filetree_->find_files(
        folder, pattern,
        button_recursive_->get_active(),
        button_hidden_->get_active());
      Regexxer::debug_log("find files complete: %d files", filetree_->get_file_count());
  }
  catch (const Glib::RegexError& error)
  {
      Regexxer::debug_log("invalid filename filter: %s", error.what());
    const Glib::ustring message = _("The file search pattern is invalid.");
    Gtk::MessageDialog dialog (*window_, message, false,
                                                Gtk::MessageType::ERROR,
                                                Gtk::ButtonsType::OK,
                                                true);
    Regexxer::GtkCompat::run_dialog(dialog);
  }
  catch (const FileTree::Error& error)
  {
    Regexxer::debug_log("filesystem search returned %zu error(s)", error.get_error_list().size());
    const Glib::ustring message = _("The following errors occurred during search:");
    FileErrorDialog dialog (*window_, message, Gtk::MessageType::WARNING, error);
    Regexxer::GtkCompat::run_dialog(dialog);
  }
  catch (const std::exception& error)
  {
    Regexxer::debug_log("unexpected search exception: %s", error.what());
    const Glib::ustring message = Util::compose(
        _("The file search could not be completed: %1"), error.what());
    auto dialog = Regexxer::GtkCompat::make_message_dialog(
        *window_, message, Gtk::MessageType::ERROR, Gtk::ButtonsType::OK, false);
    Regexxer::GtkCompat::run_dialog(dialog);
  }
  catch (...)
  {
    Regexxer::debug_log("unknown exception escaped file search");
    auto dialog = Regexxer::GtkCompat::make_message_dialog(
        *window_, _("The file search failed for an unknown reason."),
        Gtk::MessageType::ERROR, Gtk::ButtonsType::OK, false);
    Regexxer::GtkCompat::run_dialog(dialog);
  }

  statusline_->set_file_count(filetree_->get_file_count());
  }
  catch (...)
  {
    Regexxer::debug_log("unknown exception escaped file search handler");
    auto dialog = Regexxer::GtkCompat::make_message_dialog(
        *window_, _("The file search failed for an unknown reason."),
        Gtk::MessageType::ERROR, Gtk::ButtonsType::OK, false);
    Regexxer::GtkCompat::run_dialog(dialog);
  }
}

void MainWindow::on_exec_search()
{
  try
  {
  BusyAction busy (*this);

  const Glib::ustring regex = entry_regex_->get_text();
  const bool caseless = button_caseless_->get_active();
  const bool multiple = button_multiple_->get_active();

  try
  {
    Glib::RefPtr<Glib::Regex> pattern = Glib::Regex::create(regex, (caseless) ? Glib::Regex::CompileFlags::CASELESS : Glib::Regex::CompileFlags(0));
    filetree_->find_matches(pattern, multiple);
  }
  catch (const Glib::RegexError& error)
  {
    const Glib::ustring message =
        Util::compose(_("Error in regular expression:\n%1"),
                      Glib::ustring(error.what()));

    Gtk::MessageDialog dialog (*window_, message, false,
                                                Gtk::MessageType::ERROR,
                                                Gtk::ButtonsType::OK,
                                                true);
    Regexxer::GtkCompat::run_dialog(dialog);

    entry_regex_->grab_focus();

    return;
  }

  if (const FileBufferPtr buffer = std::dynamic_pointer_cast<FileBuffer>(textview_->get_buffer()))
  {
    statusline_->set_match_count(buffer->get_original_match_count());
    statusline_->set_match_index(buffer->get_match_index());
  }

  if (filetree_->get_match_count() > 0)
  {
    // Scrolling has to be post-poned after the redraw, otherwise we might
    // not end up where we want to.  So do that by installing an idle handler.

    Glib::signal_idle().connect(
        sigc::mem_fun(*this, &MainWindow::after_exec_search),
        Glib::PRIORITY_HIGH_IDLE + 25); // slightly less than redraw (+20)
  }
  }
  catch (...)
  {
    Regexxer::debug_log("unknown exception escaped regex search handler");
    auto dialog = Regexxer::GtkCompat::make_message_dialog(
        *window_, _("The search failed for an unknown reason."),
        Gtk::MessageType::ERROR, Gtk::ButtonsType::OK, false);
    Regexxer::GtkCompat::run_dialog(dialog);
  }
}

bool MainWindow::after_exec_search()
{
  try
  {
    filetree_->select_first_file();
    on_go_next(true);
  }
  catch (...)
  {
    Regexxer::debug_log("exception in after_exec_search idle handler");
  }

  return false;
}

void MainWindow::on_filetree_switch_buffer(FileInfoPtr fileinfo, int file_index)
{
  try
  {
    Regexxer::debug_log("MainWindow::on_filetree_switch_buffer begin (file_index=%d, has_file=%d)",
                        file_index, static_cast<bool>(fileinfo));
    // The textview may hold a plain Gtk::TextBuffer (e.g. the empty buffer
    // installed when nothing is selected). Use dynamic cast: a static cast
    // on a non-FileBuffer yields an invalid pointer -> segfault in
    // forget_current_match(). This was the crash when clicking through files.
    const FileBufferPtr old_buffer =
        std::dynamic_pointer_cast<FileBuffer>(textview_->get_buffer());

    if (fileinfo && fileinfo->buffer == old_buffer)
      return;

    if (old_buffer)
    {
      std::for_each(buffer_connections_.begin(), buffer_connections_.end(),
                    [](sigc::connection& c) { c.disconnect(); });

      buffer_connections_.clear();
      Regexxer::debug_log("forgetting current match in previous buffer");
      old_buffer->forget_current_match();
      Regexxer::debug_log("previous buffer match forgotten");
    }

    if (fileinfo)
    {
      const FileBufferPtr buffer = fileinfo->buffer;
      g_return_if_fail(buffer);

      textview_->set_buffer(buffer);
      textview_->set_editable(!fileinfo->load_failed);
      textview_->set_cursor_visible(!fileinfo->load_failed);

      if (!fileinfo->load_failed)
      {
        buffer_connections_.push_back(buffer->signal_modified_changed().
            connect(sigc::mem_fun(*this, &MainWindow::on_buffer_modified_changed)));

        buffer_connections_.push_back(buffer->signal_bound_state_changed.
            connect(sigc::mem_fun(*this, &MainWindow::on_bound_state_changed)));

        buffer_connections_.push_back(buffer->signal_preview_line_changed.
            connect(sigc::mem_fun(*this, &MainWindow::update_preview)));
      }

      set_title_filename(Util::filename_to_utf8_fallback(fileinfo->fullname));

      controller_.replace_file.set_enabled(buffer->get_match_count() > 0);
      controller_.save_file.set_enabled(buffer->get_modified());

      statusline_->set_match_count(buffer->get_original_match_count());
      statusline_->set_match_index(buffer->get_match_index());
      statusline_->set_file_encoding(fileinfo->encoding);
    }
    else
    {
      textview_->set_buffer(FileBuffer::create());
      textview_->set_editable(false);
      textview_->set_cursor_visible(false);

      set_title_filename();

      controller_.replace_file.set_enabled(false);
      controller_.save_file.set_enabled(false);

      statusline_->set_match_count(0);
      statusline_->set_match_index(0);
      statusline_->set_file_encoding("");
    }

    statusline_->set_file_index(file_index);
    update_preview();
  }
  catch (...)
  {
    Regexxer::debug_log("exception in on_filetree_switch_buffer signal handler");
  }
}

void MainWindow::on_bound_state_changed()
{
  try
  {
    Regexxer::debug_log("MainWindow::on_bound_state_changed begin");
  BoundState bound = filetree_->get_bound_state();

  controller_.prev_file.set_enabled((bound & BOUND_FIRST) == 0);
  controller_.next_file.set_enabled((bound & BOUND_LAST)  == 0);

  // During a new file search, Gtk::TreeStore::clear() emits a transient
  // bound-state change before the new selection/buffer pair has settled.
  // Do not query the editor's previous buffer in that interval.
  if (filetree_->get_selection()->get_selected())
  {
    if (const FileBufferPtr buffer = std::dynamic_pointer_cast<FileBuffer>(textview_->get_buffer()))
    {
      Regexxer::debug_log("MainWindow::on_bound_state_changed: querying selected FileBuffer state");
      bound &= buffer->get_bound_state();
    }
  }

  controller_.prev_match.set_enabled((bound & BOUND_FIRST) == 0);
  controller_.next_match.set_enabled((bound & BOUND_LAST)  == 0);
  }
  catch (...)
  {
    Regexxer::debug_log("exception in on_bound_state_changed signal handler");
  }
}

void MainWindow::on_filetree_file_count_changed()
{
  try
  {
    const int file_count = filetree_->get_file_count();

    statusline_->set_file_count(file_count);
    controller_.find_matches.set_enabled(file_count > 0);
  }
  catch (...)
  {
    Regexxer::debug_log("exception in on_filetree_file_count_changed signal handler");
  }
}

void MainWindow::on_filetree_match_count_changed()
{
  try
  {
    controller_.replace_all.set_enabled(filetree_->get_match_count() > 0);

    if (const FileBufferPtr buffer = std::dynamic_pointer_cast<FileBuffer>(textview_->get_buffer()))
      controller_.replace_file.set_enabled(buffer->get_match_count() > 0);
  }
  catch (...)
  {
    Regexxer::debug_log("exception in on_filetree_match_count_changed signal handler");
  }
}

void MainWindow::on_filetree_modified_count_changed()
{
  try
  {
    controller_.save_all.set_enabled(filetree_->get_modified_count() > 0);
  }
  catch (...)
  {
    Regexxer::debug_log("exception in on_filetree_modified_count_changed signal handler");
  }
}

void MainWindow::on_buffer_modified_changed()
{
  try
  {
    controller_.save_file.set_enabled(textview_->get_buffer()->get_modified());
  }
  catch (...)
  {
    Regexxer::debug_log("exception in on_buffer_modified_changed signal handler");
  }
}

void MainWindow::on_go_next_file(bool move_forward)
{
  filetree_->select_next_file(move_forward);
  on_go_next(move_forward);
}

void MainWindow::on_go_next(bool move_forward)
{
  if (const FileBufferPtr buffer = std::dynamic_pointer_cast<FileBuffer>(textview_->get_buffer()))
  {
    if (const Glib::RefPtr<Gtk::TextMark> mark = buffer->get_next_match(move_forward))
    {
      textview_->scroll_to(mark, 0.125);
      statusline_->set_match_index(buffer->get_match_index());
      return;
    }
  }

  if (filetree_->select_next_file(move_forward))
  {
    on_go_next(move_forward); // recursive call
  }
}

void MainWindow::on_replace()
{
  if (const FileBufferPtr buffer = std::dynamic_pointer_cast<FileBuffer>(textview_->get_buffer()))
  {
    buffer->replace_current_match(entry_substitution_->get_text());
    on_go_next(true);
  }
}

void MainWindow::on_replace_file()
{
  if (const FileBufferPtr buffer = std::dynamic_pointer_cast<FileBuffer>(textview_->get_buffer()))
  {
    buffer->replace_all_matches(entry_substitution_->get_text());
    statusline_->set_match_index(0);
  }
}

void MainWindow::on_replace_all()
{
  BusyAction busy (*this);

  filetree_->replace_all_matches(entry_substitution_->get_text());
  statusline_->set_match_index(0);
}

void MainWindow::on_save_file()
{
  try
  {
    filetree_->save_current_file();
  }
  catch (const FileTree::Error& error)
  {
    const std::list<Glib::ustring>& error_list = error.get_error_list();
    g_assert(error_list.size() == 1);

    Gtk::MessageDialog dialog (*window_, error_list.front(), false,
                                                Gtk::MessageType::ERROR,
                                                Gtk::ButtonsType::OK,
                                                true);
    Regexxer::GtkCompat::run_dialog(dialog);
  }
}

void MainWindow::on_save_all()
{
  try
  {
    filetree_->save_all_files();
  }
  catch (const FileTree::Error& error)
  {
    const Glib::ustring message = _("The following errors occurred during save:");
    FileErrorDialog dialog (*window_, message, Gtk::MessageType::ERROR, error);
    Regexxer::GtkCompat::run_dialog(dialog);
  }
}

void MainWindow::on_undo_stack_push(UndoActionPtr action)
{
  undo_stack_->push(action);
  controller_.undo.set_enabled(true);
}

void MainWindow::on_undo()
{
  BusyAction busy (*this);

  undo_stack_->undo_step(sigc::mem_fun(*this, &MainWindow::on_busy_action_pulse));
  controller_.undo.set_enabled(!undo_stack_->empty());
}

void MainWindow::undo_stack_clear()
{
  controller_.undo.set_enabled(false);
  undo_stack_.reset(new UndoStack());
}

void MainWindow::on_entry_pattern_changed()
{
  try
  {
    controller_.find_files.set_enabled(entry_pattern_->get_text_length() > 0);
  }
  catch (...)
  {
    Regexxer::debug_log("exception in on_entry_pattern_changed signal handler");
  }
}

void MainWindow::update_preview()
{
  try
  {
    if (const FileBufferPtr buffer = std::dynamic_pointer_cast<FileBuffer>(textview_->get_buffer()))
    {
      Glib::ustring preview;
      const int pos = buffer->get_line_preview(entry_substitution_->get_text(), preview);
      entry_preview_->set_text(preview);

      controller_.replace.set_enabled(pos >= 0);

      // Beware, strange code ahead!
      //
      // The goal is to scroll the preview entry so that it shows the entire
      // replaced text if possible.  In order to do that we first move the cursor
      // to 0, forcing scrolling to the left boundary.  Then we set the cursor to
      // the end of the replaced text, thus forcing the entry widget to scroll
      // again.  The replacement should then be entirely visible provided that it
      // fits into the entry.
      //
      // The problem is that Gtk::Entry doesn't update its scroll position
      // immediately but in an idle handler, thus any calls to set_position()
      // but the last one have no effect at all.
      //
      // To workaround that, we install an idle handler that's executed just
      // after the entry updated its scroll position, but before redrawing is
      // done.

      entry_preview_->set_position(0);

      if (pos > 0)
      {
        Gtk::Entry* const preview_entry = entry_preview_;

        Glib::signal_idle().connect(
            [preview_entry, pos]() -> bool {
              try
              {
                preview_entry->set_position(pos);
              }
              catch (...)
              {
                Regexxer::debug_log("exception in preview idle handler");
              }
              return false;
            },
            Glib::PRIORITY_HIGH_IDLE + 17); // between scroll update (+ 15) and redraw (+ 20)
      }
    }
  }
  catch (...)
  {
    Regexxer::debug_log("exception in update_preview signal handler");
  }
}

void MainWindow::set_title_filename(const Glib::ustring& filename)
{
  Glib::ustring title;

  if (!filename.empty())
  {
    title  = Glib::path_get_basename(filename.raw());
    title += " (";
    title += Util::shorten_pathname(Glib::path_get_dirname(filename.raw()));
    title += ") \342\200\223 "; // U+2013 EN DASH
  }

  title += PACKAGE_NAME;

  window_->set_title(title);
}

void MainWindow::busy_action_enter()
{
  g_return_if_fail(!busy_action_running_);

  controller_.match_actions.set_enabled(false);

  statusline_->pulse_start();

  busy_action_running_    = true;
  busy_action_cancel_     = false;
  busy_action_iteration_  = 0;
}

void MainWindow::busy_action_leave()
{
  g_return_if_fail(busy_action_running_);

  busy_action_running_ = false;
  busy_action_cancel_  = false;

  statusline_->pulse_stop();

  controller_.match_actions.set_enabled(true);
}

bool MainWindow::on_busy_action_pulse()
{
  try
  {
    g_return_val_if_fail(busy_action_running_, true);

    if (!busy_action_cancel_ && (++busy_action_iteration_ % BUSY_GUI_UPDATE_INTERVAL) == 0)
      statusline_->pulse();

    return busy_action_cancel_;
  }
  catch (...)
  {
    Regexxer::debug_log("exception in on_busy_action_pulse idle handler");
    return true;
  }
}

void MainWindow::on_busy_action_cancel()
{
  try
  {
    if (busy_action_running_)
      busy_action_cancel_ = true;
  }
  catch (...)
  {
    Regexxer::debug_log("exception in on_busy_action_cancel signal handler");
  }
}

void MainWindow::on_about()
{
  if (about_dialog_.get())
  {
    about_dialog_->present();
  }
  else
  {
    std::unique_ptr<Gtk::Window> dialog (AboutDialog::create(*window_));

    dialog->signal_hide().connect(sigc::mem_fun(*this, &MainWindow::on_about_dialog_hide));
    dialog->show();

    about_dialog_ = std::move(dialog);
  }
}

void MainWindow::on_about_dialog_hide()
{
  // Play safe and transfer ownership, and let the dtor do the delete.
  std::unique_ptr<Gtk::Window> temp = std::move(about_dialog_);
}

void MainWindow::on_preferences()
{
  if (pref_dialog_.get())
  {
    pref_dialog_->get_dialog()->present();
  }
  else
  {
    std::unique_ptr<PrefDialog> dialog (new PrefDialog(*window_));

    dialog->get_dialog()->signal_hide().connect(sigc::mem_fun(*this, &MainWindow::on_pref_dialog_hide));
    dialog->get_dialog()->show();

    pref_dialog_ = std::move(dialog);
  }
}

void MainWindow::on_pref_dialog_hide()
{
  // Play safe and transfer ownership, and let the dtor do the delete.
  std::unique_ptr<PrefDialog> temp = std::move(pref_dialog_);
}

void MainWindow::on_config_changed(const std::string& key)
{
  try
  {
    AppSettings& settings = app_settings();

    if (matches_config_key(key, AppConfig::modern_key_name(AppConfig::textview_font)))
  {
    // GTK4 removed Gtk::TextView::override_font(); apply the font via CSS
    // on the textview's "text" node. Split the Pango font description into
    // family + size so the CSS parses.
    const Pango::FontDescription font_desc(settings.get_string(key, "Monospace 10"));
    const Glib::ustring css_data =
        "textview text { font-family: \"" + font_desc.get_family() + "\"; font-size: " +
        Util::int_to_string(font_desc.get_size() / PANGO_SCALE) + "pt; }";
    const Glib::RefPtr<Gtk::CssProvider> css = Gtk::CssProvider::create();
    css->load_from_data(css_data);
    textview_->get_style_context()->add_provider(css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  }
  // Note: the historical 'toolbar-style' and 'override-direction' keys were
  // removed from the code and the schema; any direct get_string()/get_boolean()
  // on a missing key aborts the process with GLib-GIO-ERROR.
  }
  catch (...)
  {
    Regexxer::debug_log("exception in on_config_changed signal handler");
  }
}

} // namespace Regexxer

