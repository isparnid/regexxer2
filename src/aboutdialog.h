#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H
#include <gtkmm/window.h>
#include <gtkmm/dialog.h>
#include <gtkmm/aboutdialog.h>
#include <glibmm/ustring.h>

#include <config.h>

#include <vector>

class AboutDialog {
public:
    static Gtk::Window* create(Gtk::Window& parent)
    {
      Gtk::AboutDialog* const dialog = new Gtk::AboutDialog();
      dialog->set_transient_for(parent);
      dialog->set_modal(true);
      dialog->set_program_name(PACKAGE_NAME);
      dialog->set_comments("A search and replace tool using regular expressions");
      dialog->set_website("https://github.com/isparnid/regexxer2");
      dialog->set_authors(std::vector<Glib::ustring>(1, "Daniel Elstner"));
      return dialog;
    }
};

#endif
