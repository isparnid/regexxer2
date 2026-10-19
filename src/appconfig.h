/*
 * AppConfig header - configuration key enumeration
 */
#ifndef REGEXXER_APPCONFIG_H_INCLUDED
#define REGEXXER_APPCONFIG_H_INCLUDED

#include <string>
#include "globalstrings.h"

namespace Regexxer
{

class AppConfig
{
public:
  enum Key
  {
    textview_font,
    match_color,
    current_match_color,
    fallback_encoding,
    window_width,
    window_height,
    window_position_x,
    window_position_y,
    window_maximized
  };

  static std::string modern_key_name(Key key)
  {
    switch (key)
    {
      case textview_font:         return conf_key_textview_font;
      case match_color:          return conf_key_match_color;
      case current_match_color:  return conf_key_current_match_color;
      case fallback_encoding:    return conf_key_fallback_encoding;
      case window_width:         return conf_key_window_width;
      case window_height:        return conf_key_window_height;
      case window_position_x:    return conf_key_window_position_x;
      case window_position_y:    return conf_key_window_position_y;
      case window_maximized:     return conf_key_window_maximized;
      default:                   return "";
    }
  }
};

} // namespace Regexxer

#endif
