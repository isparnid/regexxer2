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
    fallback_encoding
  };

  static std::string modern_key_name(Key key)
  {
    switch (key)
    {
      case textview_font:         return conf_key_textview_font;
      case match_color:          return conf_key_match_color;
      case current_match_color:  return conf_key_current_match_color;
      case fallback_encoding:    return conf_key_fallback_encoding;
      default:                   return "";
    }
  }
};

} // namespace Regexxer

#endif
