/* $Id$
 *
 * Copyright (c) 2002  Daniel Elstner  <daniel.elstner@gmx.net>
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

#include "filetree.h"
#include "debug.h"
#include "filetreeprivate.h"
#include "globalstrings.h"
/* PCRE replaced by Glib::Regex (GTK4 migration) */
#include "stringutils.h"
#include "settings.h"
#include "appsettings.h"
#include "appconfig.h"
#include "translation.h"


#include <config.h>

using namespace Regexxer::FileTreePrivate;


namespace Regexxer
{

/**** Regexxer::FileTree ***************************************************/

FileTree::FileTree()
:
  treestore_      (Gtk::TreeStore::create(FileTreeColumns::instance())),
  sum_matches_    (0)
{
  using namespace Gtk;

  color_modified_.set("red");
  color_load_failed_.set("#808080");

  set_model(treestore_);
  const FileTreeColumns& model_columns = FileTreeColumns::instance();

  treestore_->set_default_sort_func(&default_sort_func);
  treestore_->set_sort_func(model_columns.collatekey, &collatekey_sort_func);
  treestore_->set_sort_column(TreeStore::DEFAULT_SORT_COLUMN_ID, Gtk::SortType::ASCENDING);

  treestore_->signal_rows_reordered().connect(
      sigc::mem_fun(*this, &FileTree::on_treestore_rows_reordered));

  {
    Column *const column = new Column(_("File"));
    append_column(*manage(column));

    CellRendererPixbuf *const cell_icon = new CellRendererPixbuf();
    column->pack_start(*manage(cell_icon), false);

    CellRendererText *const cell_filename = new CellRendererText();
    column->pack_start(*manage(cell_filename));

    column->add_attribute(cell_filename->property_text(), model_columns.filename);
    column->set_cell_data_func(*cell_icon,     sigc::mem_fun(*this, &FileTree::icon_cell_data_func));
    column->set_cell_data_func(*cell_filename, sigc::mem_fun(*this, &FileTree::text_cell_data_func));

    column->set_resizable(true);

    column->set_sort_column(model_columns.collatekey);
  }

  {
    Column *const column = new Column(_("#"));
    append_column(*manage(column));

    CellRendererText *const cell_matchcount = new CellRendererText();
    column->pack_start(*manage(cell_matchcount));

    column->add_attribute(cell_matchcount->property_text(), model_columns.matchcount);
    column->set_cell_data_func(*cell_matchcount, sigc::mem_fun(*this, &FileTree::text_cell_data_func));

    column->set_alignment(1.0);
    cell_matchcount->property_xalign() = 1.0;

    column->set_sort_column(model_columns.matchcount);
  }

  set_search_column(model_columns.filename);

  const Glib::RefPtr<Gtk::TreeSelection> selection = get_selection();

  selection->set_select_function(&FileTree::select_func);
  selection->signal_changed().connect(sigc::mem_fun(*this, &FileTree::on_selection_changed));

  AppSettings& settings = app_settings();
  fallback_encoding_ = settings.get_string(AppConfig::modern_key_name(AppConfig::fallback_encoding), "ISO-8859-15");
  settings.signal_changed().connect(sigc::mem_fun(*this, &FileTree::on_settings_changed));
}

void FileTree::on_settings_changed(const std::string& key)
{
  try
  {
    if (key == AppConfig::modern_key_name(AppConfig::fallback_encoding))
      fallback_encoding_ = app_settings().get_string(key, "ISO-8859-15");
  }
  catch (...)
  {
    Regexxer::debug_log("FileTree::on_settings_changed: unhandled exception caught");
  }
}

FileTree::~FileTree()
{
}

void FileTree::find_files(const std::string& dirname, const Glib::RefPtr<Glib::Regex>& pattern,
                          bool recursive, bool hidden)
{
  Regexxer::debug_log("FileTree::find_files begin: '%s' recursive=%d hidden=%d",
                      dirname.c_str(), recursive, hidden);
  FindData find_data (pattern, recursive, hidden);
  Regexxer::debug_log("FileTree::find_files: FindData constructed");

  const bool modified_count_changed = (toplevel_.modified_count != 0);

  // Changing the filter calls find_files() again on new results. The
  // TreeStore::clear() below emits selection-changed on the old (now
  // invalid) rows, and the MainWindow would try to load a buffer for a
  // dying iterator -> crash. Detach the selection first.
  get_selection()->unselect_all();
  last_selected_rowref_.reset();
  last_selected_.reset();

  Regexxer::debug_log("FileTree::find_files: clearing Gtk::TreeStore");
  treestore_->clear();
  Regexxer::debug_log("FileTree::find_files: Gtk::TreeStore cleared");

  toplevel_.file_count     = 0;
  toplevel_.modified_count = 0;
  sum_matches_ = 0;

  Regexxer::debug_log("FileTree::find_files: emit initial bound-state");
  signal_bound_state_changed(); // emit
  Regexxer::debug_log("FileTree::find_files: emit initial match-count");
  signal_match_count_changed(); // emit

  if (modified_count_changed)
  {
    Regexxer::debug_log("FileTree::find_files: emit modified-count");
    signal_modified_count_changed(); // emit
  }

  try
  {
    find_recursively(dirname, find_data);
  }
  catch (const Glib::FileError& error)
  {
    find_data.error_list->push_back(error.what()); // collect errors but don't fail
  }
  catch (const Glib::RegexError& error)
  {
    find_data.error_list->push_back(error.what());
  }
  catch (const std::exception& error)
  {
    find_data.error_list->push_back(error.what());
  }

  signal_bound_state_changed(); // emit

  if (!find_data.error_list->empty())
    throw Error(find_data.error_list);

  Regexxer::debug_log("FileTree::find_files end: %d files", toplevel_.file_count);
}

int FileTree::get_file_count() const
{
  g_return_val_if_fail(toplevel_.file_count >= 0, 0);
  return toplevel_.file_count;
}

void FileTree::save_current_file()
{
  if (const Gtk::TreeModel::iterator selected = get_selection()->get_selected())
  {
    Util::SharedPtr<MessageList> error_list;

    {
      Util::ScopedBlock block (conn_modified_changed_);
      save_file_at_iter(selected, error_list);
    }

    if (error_list)
      throw Error(error_list);
  }
}

void FileTree::save_all_files()
{
  Util::SharedPtr<MessageList> error_list;

  {
    Util::ScopedBlock block (conn_modified_changed_);
    treestore_->foreach([this, &error_list](const Gtk::TreeModel::Path&,
                                             const Gtk::TreeModel::iterator& iter)
    {
      return save_file_at_iter(iter, error_list);
    });
  }

  if (error_list)
    throw Error(error_list);
}

void FileTree::select_first_file()
{
  if (sum_matches_ > 0)
    expand_and_select(path_match_first_);
}

bool FileTree::select_next_file(bool move_forward)
{
  if (Gtk::TreeModel::iterator iter = get_selection()->get_selected())
  {
    Gtk::TreeModel::Path collapse;

    if ((move_forward) ? next_match_file(iter, &collapse) : prev_match_file(iter, &collapse))
    {
      if (!collapse.empty())
        collapse_row(collapse);

      expand_and_select(Gtk::TreeModel::Path(iter));
      return true;
    }
  }

  return false;
}

BoundState FileTree::get_bound_state()
{
  BoundState bound = BOUND_FIRST | BOUND_LAST;

  if (sum_matches_ > 0)
  {
    if (const Gtk::TreeModel::iterator iter = get_selection()->get_selected())
    {
      Gtk::TreePath path (iter);

      if (path > path_match_first_)
        bound &= ~BOUND_FIRST;

      if (path < path_match_last_)
        bound &= ~BOUND_LAST;
    }
  }

  return bound;
}

void FileTree::find_matches(const Glib::RefPtr<Glib::Regex>& pattern, bool multiple)
{
  {
    Util::ScopedBlock  block_conn (conn_match_count_);
    ScopedBlockSorting block_sort (*this);
    FindMatchesData find_data (pattern, multiple);

    try
    {
      treestore_->foreach([this, &find_data](const Gtk::TreeModel::Path& path,
                                             const Gtk::TreeModel::iterator& iter)
      {
        // Le callback est invoqué depuis gtk_tree_model_foreach() (C pur) :
        // aucune exception ne doit en sortir, sinon glibmm logue
        // "unhandled exception in signal handler" et abort.
        try
        {
          return find_matches_at_path_iter(path, iter, find_data);
        }
        catch (const Glib::Error& e)
        {
          // fichier illisible / encodage invalide / etc. : on ignore
          // cette entrée et on poursuit la recherche.
          //g_debug("find_matches: skipping row: %s", e.what().c_str());
        }
        catch (const std::exception& e)
        {
          g_debug("find_matches: skipping row: %s", e.what());
        }
        catch (...)
        {
          g_debug("find_matches: skipping row: unknown exception");
        }
        return false; // continuer le foreach
      });
    }
    catch (const std::exception& e)
    {
      // Filet de sécurité : ne devrait plus arriver, mais on ne laisse
      // jamais une exception remonter hors du handler de signal.
      g_warning("find_matches: %s", e.what());
    }
    catch (...)
    {
      g_warning("find_matches: unknown exception");
    }
  }

  signal_bound_state_changed(); // emit
}

void FileTree::replace_all_matches(const Glib::ustring& substitution)
{
  {
    Util::ScopedBlock block_match_count      (conn_match_count_);
    Util::ScopedBlock block_modified_changed (conn_modified_changed_);
    Util::ScopedBlock block_undo_stack_push  (conn_undo_stack_push_);
    ScopedBlockSorting block_sort (*this);
    ReplaceMatchesData replace_data (*this, substitution);

    treestore_->foreach([this, &replace_data](const Gtk::TreeModel::Path& path,
                                              const Gtk::TreeModel::iterator& iter)
    {
      return replace_matches_at_path_iter(path, iter, replace_data);
    });

    // Adjust the boundary range if the operation has been interrupted.
    if (sum_matches_ > 0)
    {
      Gtk::TreeModel::iterator first = treestore_->get_iter(path_match_first_);

      if ((*first)[FileTreeColumns::instance().matchcount] == 0 && next_match_file(first))
        path_match_first_ = first;
    }

    signal_undo_stack_push(replace_data.undo_stack); // emit
  }

  signal_bound_state_changed(); // emit
}

long FileTree::get_match_count() const
{
  g_return_val_if_fail(sum_matches_ >= 0, 0);
  return sum_matches_;
}

int FileTree::get_modified_count() const
{
  g_return_val_if_fail(toplevel_.modified_count >= 0, 0);
  return toplevel_.modified_count;
}

/**** Regexxer::FileTree -- protected **************************************/

/**** Regexxer::FileTree -- private ****************************************/

void FileTree::icon_cell_data_func(Gtk::CellRenderer* cell, const Gtk::TreeModel::const_iterator& iter)
{
  Gtk::CellRendererPixbuf& renderer = dynamic_cast<Gtk::CellRendererPixbuf&>(*cell);
  const FileInfoBasePtr    infobase = (*iter)[FileTreeColumns::instance().fileinfo];

  if (const FileInfoPtr fileinfo = shared_dynamic_cast<FileInfo>(infobase))
  {
    renderer.property_icon_name() = (fileinfo->load_failed) ? "dialog-error" : "text-x-generic";
  }
  else if (shared_dynamic_cast<DirInfo>(infobase))
  {
    renderer.property_icon_name() = "folder";
  }
  else
  {
    renderer.property_icon_name().reset_value();
  }
}

void FileTree::text_cell_data_func(Gtk::CellRenderer* cell, const Gtk::TreeModel::const_iterator& iter)
{
  Gtk::CellRendererText& renderer = dynamic_cast<Gtk::CellRendererText&>(*cell);
  const FileInfoBasePtr  infobase = (*iter)[FileTreeColumns::instance().fileinfo];

  const Gdk::RGBA* color = 0;

  if (const FileInfoPtr fileinfo = shared_dynamic_cast<FileInfo>(infobase))
  {
    if (fileinfo->load_failed)
      color = &color_load_failed_;
    else if (fileinfo->buffer && fileinfo->buffer->get_modified())
      color = &color_modified_;
  }
  else if (const DirInfoPtr dirinfo = shared_dynamic_cast<DirInfo>(infobase))
  {
    if (dirinfo->modified_count > 0)
      color = &color_modified_;
  }

  if (color)
    renderer.property_foreground_rgba() = *color;
  else
    renderer.property_foreground_rgba().reset_value();
}

// static
bool FileTree::select_func(const Glib::RefPtr<Gtk::TreeModel>& model, const Gtk::TreePath& path, bool)
{
  // Don't allow selection of directory nodes.
  return get_fileinfo_from_iter(model->get_iter(path));
}

void FileTree::find_recursively(const std::string& dirname, FindData& find_data)
{
  using namespace Glib;

  Regexxer::debug_log("scan directory: '%s'", dirname.c_str());

  try
  {
    int file_count = 0;
    Dir dir (dirname);

    for (Dir::iterator pos = dir.begin(); pos != dir.end(); ++pos)
    {
      if (signal_pulse.emit()) // emit
        break;

      const std::string basename = *pos;

      if (basename.empty())
        continue;

      if (!find_data.hidden && *basename.begin() == '.')
        continue;

      const std::string fullname = build_filename(dirname, basename);

      try
      {
        Regexxer::debug_log("inspect: '%s'", fullname.c_str());

        if (file_test(fullname, FileTest::IS_SYMLINK))
          continue; // ignore symbolic links

        if (find_data.recursive && file_test(fullname, FileTest::IS_DIR))
        {
          // Put the directory name on the stack instead of creating a new node
          // immediately.  The corresponding node will be created on demand if
          // there's actually a matching file in the directory or one of its
          // subdirectories.
          //
          ScopedPushDir pushdir (find_data.dirstack, fullname);
          find_recursively(fullname, find_data); // recurse
        }
        else if (file_test(fullname, FileTest::IS_REGULAR))
        {
          const ustring basename_utf8 = Util::filename_to_utf8_fallback(basename);

          bool matches = false;

          try
          {
            matches = find_data.pattern->match(basename_utf8);
          }
          catch (const Glib::RegexError& error)
          {
            find_data.error_list->push_back(error.what());
            continue;
          }

          if (matches)
          {
            Regexxer::debug_log("inserted matching file: '%s'", fullname.c_str());
            find_add_file(basename_utf8, fullname, find_data);
            ++file_count;
          }
        }
      }
      catch (const Glib::FileError& error)
      {
        // Collect errors but don't interrupt the search.
        find_data.error_list->push_back(error.what());
      }
      catch (const Glib::ConvertError& error) // unlikely due to use of our own fallback conversion
      {
        // Don't use Glib::locale_to_utf8() because we already
        // tried that in Util::filename_to_utf8_fallback().
        //
        const Glib::ustring name = Util::convert_to_ascii(fullname);
        const Glib::ustring what = error.what();

        g_warning("Eeeek, can't convert filename `%s' to UTF-8: %s", name.c_str(), what.c_str());
      }
    }

    find_increment_file_count(find_data, file_count);
    Regexxer::debug_log("finished directory: '%s' (%d matching files)", dirname.c_str(), file_count);
  }
  catch (const Glib::FileError& error)
  {
    // Collect errors but don't interrupt the search.
    find_data.error_list->push_back(error.what());
  }
}

void FileTree::find_add_file(const Glib::ustring& basename, const std::string& fullname,
                             FindData& find_data)
{
  // Build the collate key with a leading '1' so that directories always
  // come first (they have a leading '0').  This is simpler and faster
  // than explicitely checking for directories in the sort function.
  std::string collate_key (1, '1');
  collate_key += basename.collate_key();

  const FileInfoBasePtr fileinfo (new FileInfo(fullname));

  Gtk::TreeModel::Row row;

  if (find_data.dirstack.empty())
  {
    row = *treestore_->prepend(); // new toplevel node
  }
  else
  {
    if (!find_data.dirstack.back().second)
      find_fill_dirstack(find_data); // build all directory nodes in the stack

    row = *treestore_->prepend(find_data.dirstack.back().second->children());
  }

  const FileTreeColumns& columns = FileTreeColumns::instance();

  row[columns.filename]   = basename;
  row[columns.collatekey] = collate_key;
  row[columns.fileinfo]   = fileinfo;
}

void FileTree::find_fill_dirstack(FindData& find_data)
{
  const FileTreeColumns& columns = FileTreeColumns::instance();

  const DirStack::iterator pend  = find_data.dirstack.end();
  DirStack::iterator       pprev = pend;

  for (DirStack::iterator pdir = find_data.dirstack.begin(); pdir != pend; pprev = pdir++)
  {
    if (pdir->second) // node already created
      continue;

    // Show only the directory basename (not the full path) to keep the
    // file list compact like the classic UI.
    const Glib::ustring dirname =
        Util::filename_to_utf8_fallback(Glib::path_get_basename(pdir->first));

    // Build the collate key with a leading '0' so that directories always
    // come first.  This is simpler and faster than explicitely checking for
    // directories in the sort function.
    std::string collate_key (1, '0');
    collate_key += dirname.collate_key();

    if (pprev == pend)
      pdir->second = treestore_->prepend(); // new toplevel node
    else
      pdir->second = treestore_->prepend(pprev->second->children());

    Gtk::TreeModel::Row row = *pdir->second;

    row[columns.filename]   = dirname;
    row[columns.collatekey] = collate_key;
    row[columns.fileinfo]   = FileInfoBasePtr(new DirInfo());
  }
}

void FileTree::find_increment_file_count(FindData& find_data, int file_count)
{
  if (file_count <= 0)
    return;

  const FileTreeColumns& columns = FileTreeColumns::instance();

  DirStack::reverse_iterator       pdir = find_data.dirstack.rbegin();
  const DirStack::reverse_iterator pend = find_data.dirstack.rend();

  for (; pdir != pend; ++pdir)
  {
    const FileInfoBasePtr base = (*pdir->second)[columns.fileinfo];
    shared_polymorphic_cast<DirInfo>(base)->file_count += file_count;
  }

  toplevel_.file_count += file_count;
  signal_file_count_changed(); // emit
}

bool FileTree::save_file_at_iter(const Gtk::TreeModel::iterator& iter,
                                 Util::SharedPtr<MessageList>& error_list)
{
  const FileInfoPtr fileinfo = get_fileinfo_from_iter(iter);

  if (fileinfo && fileinfo->buffer && fileinfo->buffer->get_modified())
  {
    try
    {
      save_file(fileinfo);
    }
    catch (const Glib::Error& error)
    {
      if (!error_list)
        error_list.reset(new MessageList());

      const Glib::ustring message = Util::compose(_("Failed to save file \"%1\": %2"),
                                                  Util::filename_to_utf8_fallback(fileinfo->fullname),
                                                  error.what());
      error_list->push_back(message);
    }

    if (!fileinfo->buffer->get_modified())
      propagate_modified_change(iter, false);

    if (fileinfo != last_selected_ && fileinfo->buffer->is_freeable())
      fileinfo->buffer.reset(); // reduce memory footprint
  }

  return false;
}

bool FileTree::find_matches_at_path_iter(const Gtk::TreeModel::Path&,
                                         const Gtk::TreeModel::iterator& iter,
                                         FindMatchesData& find_data)
{
  if (signal_pulse.emit()) // emit
    return true;

  if (const FileInfoPtr fileinfo = get_fileinfo_from_iter(iter))
  {
    if (!fileinfo->buffer)
      load_file_with_fallback(iter, fileinfo);

    if (fileinfo->load_failed)
      return false; // continue

    const Glib::RefPtr<FileBuffer> buffer = fileinfo->buffer;
    g_assert(buffer);

    Util::ScopedConnection conn (buffer->signal_pulse.connect([this]() { return signal_pulse.emit(); }));

    const int old_match_count = buffer->get_match_count();
    const int new_match_count = buffer->find_matches(*find_data.pattern, find_data.multiple);

    if (new_match_count > 0)
    {
      const Gtk::TreePath path (iter);

      if (!find_data.path_match_first_set)
      {
        path_match_first_ = path;
        find_data.path_match_first_set = true;
      }

      path_match_last_ = path;
    }

    if (new_match_count != old_match_count)
      propagate_match_count_change(iter, new_match_count - old_match_count);

    if (fileinfo != last_selected_ && buffer->is_freeable())
      fileinfo->buffer.reset(); // reduce memory footprint
  }

  return false;
}

bool FileTree::replace_matches_at_path_iter(const Gtk::TreeModel::Path&,
                                             const Gtk::TreeModel::iterator& iter,
                                             ReplaceMatchesData& replace_data)
{
  if (signal_pulse.emit()) // emit
    return true;

  const FileInfoPtr fileinfo = get_fileinfo_from_iter(iter);

  if (fileinfo && fileinfo->buffer)
  {
    const Glib::RefPtr<FileBuffer> buffer = fileinfo->buffer;

    const int match_count = buffer->get_match_count();

    if (match_count > 0)
    {
      path_match_first_ = Gtk::TreePath(iter);

      if (fileinfo != last_selected_)
        replace_data.row_reference.reset(new TreeRowRef(treestore_, path_match_first_));
      else
        replace_data.row_reference = last_selected_rowref_;

      const bool was_modified = buffer->get_modified();

      {
        // Redirect the buffer's signal_undo_stack_push() in order to create
        // a single user action object for all replacements in all buffers.
        // Note that the caller must block conn_undo_stack_push_ to avoid
        // double notification.
        Util::ScopedConnection conn1 (buffer->signal_undo_stack_push.
                                      connect(replace_data.slot_undo_stack_push));

        Util::ScopedConnection conn2 (buffer->signal_pulse.connect([this]() { return signal_pulse.emit(); }));

        buffer->replace_all_matches(replace_data.substitution);
      }

      const bool is_modified = buffer->get_modified();

      if (was_modified != is_modified)
        propagate_modified_change(iter, is_modified);

      propagate_match_count_change(iter, buffer->get_match_count() - match_count);
    }
  }

  return false;
}

void FileTree::expand_and_select(const Gtk::TreePath& path)
{
  expand_to_path(path);
  get_selection()->select(path);

  scroll_to_row(path); // lazy scrolling implemented since GTK+ 2.1.4
}

void FileTree::on_treestore_rows_reordered(const Gtk::TreeModel::Path& path,
                                           const Gtk::TreeModel::iterator& iter, int*)
{
  try
  {
    if (sum_matches_ > 0)
    {
    const FileTreeColumns& columns = FileTreeColumns::instance();
    bool bounds_changed = false;

    if (path.is_ancestor(path_match_first_))
    {
      Gtk::TreeModel::iterator first = iter->children().begin();

      while (first->children() && (*first)[columns.matchcount] > 0)
        first = first->children().begin();

      if ((*first)[columns.matchcount] == 0)
      {
        const bool found_first = next_match_file(first);
        g_return_if_fail(found_first);
      }

      path_match_first_ = first;
      bounds_changed = true;
    }

    if (path.is_ancestor(path_match_last_))
    {
      Gtk::TreeModel::iterator last_child = iter->children().begin();
      Gtk::TreeModel::iterator last = last_child;

      for (; last_child; ++last_child)
        last = last_child;

      while (last->children() && (*last)[columns.matchcount] > 0)
      {
        last_child = last->children().begin();
        last = last_child;

        for (; last_child; ++last_child)
          last = last_child;
      }

      if ((*last)[columns.matchcount] == 0)
      {
        const bool found_last = prev_match_file(last);
        g_return_if_fail(found_last);
      }

      path_match_last_ = last;
      bounds_changed = true;
    }

    if (bounds_changed)
      signal_bound_state_changed(); // emit
    }
  }
  catch (...)
  {
    Regexxer::debug_log("FileTree::on_treestore_rows_reordered: unhandled exception caught");
  }
}

void FileTree::on_selection_changed()
{
  Regexxer::debug_log("FileTree::on_selection_changed begin");
  last_selected_rowref_.reset();

  conn_match_count_     .disconnect();
  conn_modified_changed_.disconnect();
  conn_undo_stack_push_ .disconnect();

  FileInfoPtr fileinfo;
  int file_index = 0;

  if (const Gtk::TreeModel::iterator iter = get_selection()->get_selected())
  {
    // Guard against rows being destroyed (e.g. treestore_->clear() during
    // a new search emits selection-changed on dying iterators).
    try
    {
    const FileInfoBasePtr base = (*iter)[FileTreeColumns::instance().fileinfo];

    // Directories carry a DirInfo, not a FileInfo: skip them instead of
    // aborting in shared_polymorphic_cast (this was the selection crash).
    if ((fileinfo = shared_dynamic_cast<FileInfo>(base)))
    {
      file_index = calculate_file_index(iter) + 1;

      if (!fileinfo->buffer)
        load_file_with_fallback(iter, fileinfo);

      if (!fileinfo->load_failed)
      {
        conn_match_count_ = fileinfo->buffer->signal_match_count_changed.
            connect(sigc::mem_fun(*this, &FileTree::on_buffer_match_count_changed));

        conn_modified_changed_ = fileinfo->buffer->signal_modified_changed().
            connect(sigc::mem_fun(*this, &FileTree::on_buffer_modified_changed));

        conn_undo_stack_push_ = fileinfo->buffer->signal_undo_stack_push.
            connect(sigc::mem_fun(*this, &FileTree::on_buffer_undo_stack_push));
      }

      last_selected_rowref_.reset(new TreeRowRef(treestore_, Gtk::TreePath(iter)));
    }
    }
    catch (...)
    {
      fileinfo.reset();
      file_index = 0;
    }
  }

  try
  {
    if (last_selected_ && last_selected_ != fileinfo &&
        last_selected_->buffer && last_selected_->buffer->is_freeable())
    {
      last_selected_->buffer.reset(); // reduce memory footprint
    }

    last_selected_ = fileinfo;

    Regexxer::debug_log("FileTree::on_selection_changed: emit switch-buffer (index=%d)", file_index);
    signal_switch_buffer(fileinfo, file_index); // emit
    Regexxer::debug_log("FileTree::on_selection_changed: emit bound-state");
    signal_bound_state_changed(); // emit
  }
  catch (...)
  {
    Regexxer::debug_log("FileTree::on_selection_changed: unhandled exception caught");
  }
}

void FileTree::on_buffer_match_count_changed()
{
  try
  {
  const FileTreeColumns& columns = FileTreeColumns::instance();

  // There has to be a selection since we receive signal_match_count_changed()
  // from the currently selected FileBuffer.
  Gtk::TreeModel::iterator iter = get_selection()->get_selected();
  g_return_if_fail(iter);

  const FileInfoBasePtr base = (*iter)[columns.fileinfo];
  const FileInfoPtr fileinfo = shared_polymorphic_cast<FileInfo>(base);

  g_return_if_fail(fileinfo->buffer);

  const int match_count     = fileinfo->buffer->get_match_count();
  const int old_match_count = (*iter)[columns.matchcount];

  if (match_count == old_match_count)
    return; // spurious emission -- do nothing

  const long old_sum_matches = sum_matches_;
  propagate_match_count_change(iter, match_count - old_match_count);

  if (old_sum_matches == 0)
  {
    path_match_first_ = iter;
    path_match_last_  = path_match_first_;
  }
  else if ((sum_matches_ > 0) && (old_match_count == 0 || match_count == 0))
  {
    // OK, this nightmarish-looking code below is all about adjusting the
    // [path_match_first_,path_match_last_] range.  Adjustment is necessary
    // if either a) match_count was previously 0 and iter is not already
    // in the range, or b) match_count dropped to 0 and iter is a boundary
    // of the range.
    //
    // Preconditions we definitely know at this point:
    // 1) old_sum_matches > 0
    // 2) sum_matches != old_sum_matches && sum_matches > 0
    // 3) old_match_count == 0 || match_count == 0
    // 4) old_match_count != match_count

    g_assert(old_sum_matches > 0);
    g_assert(old_match_count != match_count);

    // The range should've been set up already since old_sum_matches > 0.
    g_return_if_fail(path_match_first_ <= path_match_last_);

    Gtk::TreePath path (iter);

    if (old_match_count == 0)
    {
      g_assert(match_count > 0);
      g_return_if_fail(path != path_match_first_ && path != path_match_last_);

      // Expand the range if necessary.
      if (path < path_match_first_)
        path_match_first_ = path;
      else if (path > path_match_last_)
        path_match_last_ = path;
    }
    else
    {
      g_assert(match_count == 0);
      g_return_if_fail(path >= path_match_first_ && path <= path_match_last_);

      if (path == path_match_first_)
      {
        // Find the new start boundary of the range.
        const bool found_next = next_match_file(iter);
        g_return_if_fail(found_next);

        path_match_first_ = iter;
      }
      else if (path == path_match_last_)
      {
        // Find the new end boundary of the range.
        const bool found_prev = prev_match_file(iter);
        g_return_if_fail(found_prev);

        path_match_last_ = iter;
      }
    }

    signal_bound_state_changed(); // emit
    }
  }
  catch (...)
  {
    Regexxer::debug_log("FileTree::on_buffer_match_count_changed: unhandled exception caught");
  }
}

void FileTree::on_buffer_modified_changed()
{
  try
  {
  const Gtk::TreeModel::iterator selected = get_selection()->get_selected();
  g_return_if_fail(selected);

  const FileInfoBasePtr base = (*selected)[FileTreeColumns::instance().fileinfo];
  const FileInfoPtr fileinfo = shared_polymorphic_cast<FileInfo>(base);

  g_return_if_fail(fileinfo == last_selected_);
  g_return_if_fail(fileinfo->buffer);

  propagate_modified_change(selected, fileinfo->buffer->get_modified());
  }
  catch (...)
  {
    Regexxer::debug_log("FileTree::on_buffer_modified_changed: unhandled exception caught");
  }
}

void FileTree::on_buffer_undo_stack_push(UndoActionPtr undo_action)
{
  try
  {
  g_return_if_fail(last_selected_rowref_);

  const UndoActionPtr action_shell (new BufferActionShell(*this, last_selected_rowref_, undo_action));

  signal_undo_stack_push(action_shell); // emit
  }
  catch (...)
  {
    Regexxer::debug_log("FileTree::on_buffer_undo_stack_push: unhandled exception caught");
  }
}

int FileTree::calculate_file_index(const Gtk::TreeModel::iterator& pos)
{
  int index = 0;

  Gtk::TreeModel::iterator iter = pos->parent();

  if (iter) // calculate the parent's index first if there is one
  {
    index = calculate_file_index(iter); // recurse
    iter = iter->children().begin();
  }
  else // toplevel reached
  {
    iter = treestore_->children().begin();
  }

  const FileTreeColumns& columns = FileTreeColumns::instance();

  for (; iter != pos; ++iter)
  {
    const FileInfoBasePtr base = (*iter)[columns.fileinfo];
    g_return_val_if_fail(base, index);

    if (const DirInfoPtr dirinfo = shared_dynamic_cast<DirInfo>(base))
      index += dirinfo->file_count; // count whole directory in one step
    else
      ++index; // single file
  }

  return index;
}

void FileTree::propagate_match_count_change(const Gtk::TreeModel::iterator& pos, int difference)
{
  const FileTreeColumns& columns = FileTreeColumns::instance();

  for (Gtk::TreeModel::iterator iter = pos; iter; iter = iter->parent())
  {
    const int match_count = (*iter)[columns.matchcount];
    (*iter)[columns.matchcount] = match_count + difference;
  }

  sum_matches_ += difference;

  signal_match_count_changed(); // emit
}

void FileTree::propagate_modified_change(const Gtk::TreeModel::iterator& pos, bool modified)
{
  const int difference = (modified) ? 1 : -1;
  const FileTreeColumns& columns = FileTreeColumns::instance();

  Gtk::TreePath path (pos);
  treestore_->row_changed(path, pos);

  for (Gtk::TreeModel::iterator iter = pos->parent(); iter && path.up(); iter = iter->parent())
  {
    const FileInfoBasePtr base = (*iter)[columns.fileinfo];
    const DirInfoPtr dirinfo = shared_polymorphic_cast<DirInfo>(base);

    dirinfo->modified_count += difference;

    // Update the view only if the count flipped from 0 to 1 or vice versa.
    if (dirinfo->modified_count == int(modified))
      treestore_->row_changed(path, iter);
  }

  toplevel_.modified_count += difference;

  signal_modified_count_changed(); // emit
}

void FileTree::load_file_with_fallback(const Gtk::TreeModel::iterator& iter,
                                       const FileInfoPtr& fileinfo)
{
  g_return_if_fail(!fileinfo->buffer);

  const bool old_load_failed = fileinfo->load_failed;

  try
  {
    load_file(fileinfo, fallback_encoding_);
  }
  catch (const Glib::Error& error)
  {
    fileinfo->buffer = FileBuffer::create_with_error_message(error.what());
  }

  if (!fileinfo->buffer)
  {
    g_assert(fileinfo->load_failed);

    const Glib::ustring message = Util::compose(_("\"%1\" seems to be a binary file."),
        Util::filename_to_utf8_fallback(Glib::path_get_basename(fileinfo->fullname)));

    fileinfo->buffer = FileBuffer::create_with_error_message(message);
  }

  if (old_load_failed != fileinfo->load_failed)
  {
    // Trigger signal_row_changed() because the value of fileinfo->load_failed
    // changed, which means we have to change icon and color of the row.
    treestore_->row_changed(Gtk::TreePath(iter), iter);
  }
}

extern "C"
GtkWidget* regexxer_create_file_tree(char*, char*, char*, int, int)
{
  try
  {
    Gtk::Widget *const widget = new FileTree();
    widget->show();
    return Gtk::manage(widget)->gobj();
  }
  catch (...)
  {
    g_return_val_if_reached(0);
  }
}

} // namespace Regexxer

