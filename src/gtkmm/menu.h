#ifndef GTKMM_MENU_H
#define GTKMM_MENU_H
#include <gtkmm/widget.h>
#include <glibmm/signalproxy.h>
namespace Gtk { 
class MenuItem : public Widget { 
public:
  Glib::SignalProxy<void()> signal_activate();
}; 
}
#endif
