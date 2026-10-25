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

#ifndef REGEXXER_MAINWINDOW_H_INCLUDED
#define REGEXXER_MAINWINDOW_H_INCLUDED

#include "controller.h"
#include "filebuffer.h"
#include "sharedptr.h"
#include "completionstack.h"

#include <sigc++/sigc++.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <giomm/menu.h>
#include <giomm/simpleaction.h>
#include <giomm/simpleactiongroup.h>
#include <list>
#include <map>
#include <memory>
#include <vector>
#include <gio/gio.h>

namespace Gtk
{
class Button;
class CheckButton;
class Dialog;
class Box;
class TextView;
class Entry;
class FileChooser;
class FileChooserNative;
class FileDialog;
class Toolbar;
class Window;
class ComboBox;
class ComboBoxText;
class VBox;
class ScrolledWindow;
class Table;
class Grid;
class Paned;
class PopoverMenuBar;
class EntryCompletion;
}

namespace Gsv
{
class View;
}

class AboutDialog;

namespace Regexxer
{

class FileTree;
class PrefDialog;
class StatusLine;
struct FileInfo;

struct InitState
{
  std::vector<std::string>  folder;
  Glib::ustring             pattern;
  Glib::ustring             regex;
  Glib::ustring             substitution;
  bool                      no_recursive;
  bool                      hidden;
  bool                      no_global;
  bool                      ignorecase;
  bool                      feedback;
  bool                      no_autorun;

  InitState();
  ~InitState();
};

class MainWindow : public sigc::trackable
{
public:
  MainWindow();
  virtual ~MainWindow();

  void initialize(const InitState& init);
  Gtk::Window* get_window() { return window_.get(); }

private:
  class BusyAction;

  std::unique_ptr<Gtk::Window>  window_;
  Controller                  controller_;

  Gtk::Box*                  vbox_main_;
  Gtk::PopoverMenuBar*       menubar_;
  Glib::RefPtr<Gio::Menu>    menu_model_;
  std::map<Glib::ustring, Glib::RefPtr<Gio::SimpleAction>> actions_;

  Gtk::Box*                  toolbar_;

  Gtk::ComboBoxText*          combobox_folder_;
  Gtk::Entry*                 entry_folder_;

  Gtk::Entry*                 entry_pattern_;

  Gtk::CheckButton*           button_recursive_;
  Gtk::CheckButton*           button_hidden_;

  Gtk::ComboBoxText*          comboboxentry_regex_;
  Gtk::Entry*                 entry_regex_;
  CompletionStack             entry_regex_completion_stack_;
  Glib::RefPtr<Gtk::EntryCompletion> entry_regex_completion_;

  Gtk::ComboBoxText*          comboboxentry_substitution_;
  Gtk::Entry*                 entry_substitution_;
  CompletionStack             entry_substitution_completion_stack_;
  Glib::RefPtr<Gtk::EntryCompletion> entry_substitution_completion_;

  Gtk::CheckButton*           button_multiple_;
  Gtk::CheckButton*           button_caseless_;

  FileTree*                   filetree_;
  Gtk::ScrolledWindow*        scrollwin_filetree_;
  Gtk::ScrolledWindow*        scrollwin_textview_;
  Gtk::TextView*            textview_;
  Gtk::Entry*                 entry_preview_;

  StatusLine*                 statusline_;

  bool                        busy_action_running_;
  bool                        busy_action_cancel_;
  unsigned int                busy_action_iteration_;

  UndoStackPtr                undo_stack_;

  std::list<sigc::connection> buffer_connections_;

  std::unique_ptr<Gtk::Window>  about_dialog_;
  std::unique_ptr<PrefDialog>   pref_dialog_;
  Glib::RefPtr<Gtk::FileDialog> folder_dialog_;

  // Temporary storage for window geometry during save_window_state().
  int x_;
  int y_;
  int width_;
  int height_;

  void load_xml();
  void build_menubar();
  void register_action(const Glib::ustring& name, ControlItem& item,
                       const Glib::ustring& accel = {});
  Glib::RefPtr<Gio::SimpleActionGroup> action_group_;
  void connect_signals();
  bool autorun_idle();
  void save_window_state();

  void on_hide();

  // GTK4 port: restore window geometry once the window is realized.
  void on_window_realize();

  // GTK4 port: save window geometry when the window is hidden/closed.
  void save_window_geometry();
  // GTK4 port: style updates are handled by the theme engine.
  // void on_style_updated();
  // GTK4 port: close handling is done via on_close_request().
  // bool on_delete_event(GdkEvent* event);

  // GTK4 port: clipboard actions are handled by the TextView itself.
  // void on_cut();
  // void on_copy();
  // void on_paste();
  // void on_erase();

  void on_quit();
  bool confirm_quit_request();

  void on_find_files();
  void on_exec_search();
  bool after_exec_search();

  void on_filetree_switch_buffer(Util::SharedPtr<FileInfo> fileinfo, int file_index);
  void on_filetree_file_count_changed();
  void on_filetree_match_count_changed();
  void on_filetree_modified_count_changed();

  void on_bound_state_changed();
  void on_buffer_modified_changed();

  bool do_scroll(const Glib::RefPtr<Gtk::TextMark> mark);

  void on_go_next_file(bool move_forward);
  void on_go_next(bool move_forward);
  void on_replace();
  void on_replace_file();
  void on_replace_all();

  void on_save_file();
  void on_save_all();

  void on_undo_stack_push(UndoActionPtr action);
  void on_undo();
  void undo_stack_clear();

  void on_entry_pattern_changed();
  void update_preview();
  void set_title_filename(const Glib::ustring& filename = Glib::ustring());

  void busy_action_enter();
  void busy_action_leave();
  bool on_busy_action_pulse();
  void on_busy_action_cancel();

  void on_about();
  void on_about_dialog_response(int);
  void on_about_dialog_hide();

  void on_preferences();
  void on_pref_dialog_hide();

  void on_config_changed(const std::string& key);
  void on_select_folder();
  void on_folder_dialog_done(const Glib::RefPtr<Gio::AsyncResult>& result);
  bool on_close_request();

  void on_conf_value_changed(const Glib::ustring& key);
};
} // namespace Regexxer

#endif /* REGEXXER_MAINWINDOW_H_INCLUDED */
