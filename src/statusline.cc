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

#include "statusline.h"
#include "stringutils.h"
#include "translation.h"

#include <gtkmm.h>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace Regexxer
{

/**** Regexxer::CounterBox *************************************************/

class CounterBox : public Gtk::Frame
{
public:
  explicit CounterBox(const Glib::ustring& label);
  virtual ~CounterBox();

  void set_index(int index);
  void set_count(int count);

private:
  enum { MIN_RANGE = 100 };

  Gtk::Label*         label_index_;
  Gtk::Label*         label_count_;
  int                 digits_range_;
  int                 widest_digit_;
  int                 second_widest_digit_;
  std::wostringstream stringstream_;

  Glib::ustring number_to_string(int number);

  void recalculate_label_width();
  void on_label_style_updated();
};

CounterBox::CounterBox(const Glib::ustring& label)
:
  label_index_          (0),
  label_count_          (0),
  digits_range_         (MIN_RANGE),
  widest_digit_         (0),
  second_widest_digit_  (9)
{
  using namespace Gtk;

  // GTK4: Frame no longer provides set_shadow_type()/ShadowType.
  // The frame's appearance is controlled by CSS/theme instead.

  Box *const paddingbox = new Gtk::Box(Gtk::Orientation::HORIZONTAL, 0);
  set_child(*paddingbox);

  Box *const box = new Gtk::Box(Gtk::Orientation::HORIZONTAL, 0);
  paddingbox->append(*manage(box));

  box->append(*manage(new Label(label + ' ')));

  label_index_ = new Label("");
  label_index_->set_halign(Gtk::Align::FILL);
  label_index_->set_valign(Gtk::Align::CENTER);
  box->append(*manage(label_index_));

  box->append(*manage(new Label("/")));

  label_count_ = new Label("");
  label_count_->set_halign(Gtk::Align::FILL);
  label_count_->set_valign(Gtk::Align::CENTER);
  box->append(*manage(label_count_));

  label_index_->set_single_line_mode(true);
  label_count_->set_single_line_mode(true);

  try // don't abort if the user-specified locale doesn't exist
  {
    stringstream_.imbue(std::locale(""));
  }
  catch (const std::runtime_error& error)
  {
    g_warning("%s", error.what());
  }

  set_index(0);
  set_count(0);

  paddingbox->show();
}

CounterBox::~CounterBox()
{}

void CounterBox::set_index(int index)
{
  label_index_->set_text(number_to_string(index));

  // Work around a bug in GTK+ that causes right-aligned labels to be
  // cut off at the end.  Forcing resize seems to solve the problem.
  queue_resize();
}

void CounterBox::set_count(int count)
{
  int range = digits_range_;

  while (range <= count)
    range *= 10;

  while (range > MIN_RANGE && range / 10 > count)
    range /= 10;

  if (range != digits_range_)
  {
    digits_range_ = range;
    recalculate_label_width();
  }

  label_count_->set_text(number_to_string(count));
}

Glib::ustring CounterBox::number_to_string(int number)
{
  stringstream_.str(std::wstring());
  stringstream_ << number;
  return Util::wstring_to_utf8(stringstream_.str());
}

/*
 * This tricky piece of code calculates the optimal label width for any
 * number < digits_range_.  It does assume the decimal system is used,
 * but it relies neither on the string representation of the digits nor
 * on the font of the label.
 */
void CounterBox::recalculate_label_width()
{
  int widest_number = 0;

  if (widest_digit_ != 0)
  {
    // E.g. range 1000 becomes 222 if '2' is the widest digit of the font.
    widest_number = ((digits_range_ - 1) / 9) * widest_digit_;
  }
  else // eeeks, 0 has to be special-cased
  {
    g_assert(second_widest_digit_ != 0);

    // E.g. range 1000 becomes 900 if '9' is the 2nd-widest digit.
    widest_number = (digits_range_ / 10) * second_widest_digit_;
  }

  int width = 0, height = 0;

  const Glib::ustring text = number_to_string(widest_number);
  label_index_->create_pango_layout(text)->get_pixel_size(width, height);

  label_index_->set_size_request(width, -1);

  label_count_->set_size_request(width, -1);
}

/*
 * The code relies on both labels having the same style.
 * I think that's a quite safe assumption to make ;)
 */
void CounterBox::on_label_style_updated()
{
  const Glib::RefPtr<Pango::Layout> layout = label_index_->create_pango_layout("");

  widest_digit_        = 0;
  second_widest_digit_ = 9;

  int max_width        = 0;
  int second_max_width = 0;

  int width  = 0;
  int height = 0;

  for (int digit = 0; digit <= 9; ++digit)
  {
    layout->set_text(number_to_string(digit));
    layout->get_pixel_size(width, height);

    if (width > max_width)
    {
      max_width = width;
      widest_digit_ = digit;
    }
    else if (width > second_max_width)
    {
      second_max_width = width;
      second_widest_digit_ = digit;
    }
  }

  recalculate_label_width();
}

/**** Regexxer::StatusLine *************************************************/

StatusLine::StatusLine()
:
  Gtk::Box(Gtk::Orientation::HORIZONTAL, 3),
  stop_button_    (0),
  progressbar_    (0),
  file_counter_   (0),
  match_counter_  (0),
  status_label_   (0),
  file_index_     (0),
  file_count_     (0),
  match_index_    (0),
  match_count_    (0)
{
  using namespace Gtk;

  // Slim single-row statusline, about menubar height: small Stop button,
  // two compact counter labels and a plain encoding label (no bulky
  // Gtk::Statusbar / Gtk::Frame which made the bar too tall).
  std::string style = "#regexxer-stop-button\n"
                      "{\n"
                      "  padding: 0 6px;\n"
                      "  min-height: 0;\n"
                      "}\n"
                      "#regexxer-statusline\n"
                      "{\n"
                      "  padding: 0;\n"
                      "}\n"
                      "#regexxer-statusline label\n"
                      "{\n"
                      "  padding: 0 4px;\n"
                      "}\n";
  Glib::RefPtr<Gtk::CssProvider> css = Gtk::CssProvider::create();
  css->load_from_data(style);

  set_name("regexxer-statusline");
  get_style_context()->add_provider(css, 1);

  stop_button_ = new Button(_("Stop"));
  append(*manage(stop_button_));
  stop_button_->set_name("regexxer-stop-button");
  stop_button_->set_valign(Gtk::Align::CENTER);

  progressbar_ = new ProgressBar();
  progressbar_->set_valign(Gtk::Align::CENTER);
  progressbar_->set_size_request(80, -1);
  append(*manage(progressbar_));

  file_counter_ = new Label();
  file_counter_->set_valign(Gtk::Align::CENTER);
  append(*manage(file_counter_));

  match_counter_ = new Label();
  match_counter_->set_valign(Gtk::Align::CENTER);
  append(*manage(match_counter_));

  status_label_ = new Label();
  status_label_->set_valign(Gtk::Align::CENTER);
  status_label_->set_hexpand(true);
  status_label_->set_halign(Gtk::Align::START);
  status_label_->set_ellipsize(Pango::EllipsizeMode::END);
  append(*manage(status_label_));

  update_file_label();
  update_match_label();

  stop_button_->set_sensitive(false);
  stop_button_->signal_clicked().connect(signal_cancel_clicked.make_slot());

  progressbar_->set_pulse_step(0.025);

  show();
}

StatusLine::~StatusLine()
{}

void StatusLine::set_file_index(int file_index)
{
  file_index_ = file_index;
  update_file_label();
}

void StatusLine::set_file_count(int file_count)
{
  file_count_ = file_count;
  update_file_label();
}

void StatusLine::set_match_index(int match_index)
{
  match_index_ = match_index;
  update_match_label();
}

void StatusLine::set_match_count(int match_count)
{
  match_count_ = match_count;
  update_match_label();
}

void StatusLine::set_file_encoding(const std::string& file_encoding)
{
  const Glib::ustring encoding = file_encoding;
  g_return_if_fail(encoding.is_ascii());

  status_label_->set_text(encoding);
}

void StatusLine::pulse_start()
{
  stop_button_->set_sensitive(true);
}

void StatusLine::pulse()
{
  progressbar_->pulse();
}

void StatusLine::pulse_stop()
{
  progressbar_->set_fraction(0.0);
  stop_button_->set_sensitive(false);
}

void StatusLine::update_file_label()
{
  // Classic compact format “File: 0/0”, kept translatable via existing msgids.
  file_counter_->set_text(
      Util::compose(_("%1: %2/%3"), _("File"), Util::int_to_string(file_index_),
                    Util::int_to_string(file_count_)));
}

void StatusLine::update_match_label()
{
  match_counter_->set_text(
      Util::compose(_("%1: %2/%3"), _("Match"), Util::int_to_string(match_index_),
                    Util::int_to_string(match_count_)));
}

void StatusLine::on_hierarchy_changed(Gtk::Widget*)
{
  // GTK4: hierarchy_changed is no longer a Gtk::Widget virtual method.
  // The Escape-key accelerator previously installed here is not re-implemented;
  // the stop button remains activable through its clicked signal.
}

} // namespace Regexxer
