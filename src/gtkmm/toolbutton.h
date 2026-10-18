#ifndef GTKMM_TOOLBUTTON_H
#define GTKMM_TOOLBUTTON_H
#include <gtkmm/widget.h>
#include <glibmm/signalproxy.h>
namespace Gtk { 
class ToolButton : public Widget { 
public:
  Glib::SignalProxy<void()> signal_clicked();
}; 
}
#endif
