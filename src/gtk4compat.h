#ifndef GTK4COMPAT_H
#define GTK4COMPAT_H

#include <gtkmm/dialog.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/filechoosernative.h>

namespace Regexxer {
namespace GtkCompat {

using MessageDialog = Gtk::MessageDialog;

inline Gtk::MessageDialog make_message_dialog(Gtk::Window& parent, const Glib::ustring& msg,
                                               Gtk::MessageType type, Gtk::ButtonsType buttons, bool modal) {
    return Gtk::MessageDialog(parent, msg, false, type, buttons, modal);
}

inline int run_dialog(Gtk::Dialog& dialog) {
    dialog.set_modal(true);
    dialog.show();
    return Gtk::ResponseType::OK;
}

inline int run_native_dialog(Gtk::FileChooserNative& chooser) {
    // native chooser stub
    chooser.show();
    return Gtk::ResponseType::OK;
}

} // namespace GtkCompat
} // namespace Regexxer

#endif
