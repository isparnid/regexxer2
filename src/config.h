#ifndef CONFIG_H
#define CONFIG_H
#define PACKAGE_TARNAME "regexxer2"
#define PACKAGE_NAME "regexxer2"
/* Gettext domain: historic .mo files are installed as regexxer.mo. */
#define GETTEXT_PACKAGE "regexxer"
#define ENABLE_NLS 1
#define HAVE_BIND_TEXTDOMAIN_CODESET 1
/* Localedir is resolved at runtime via CMake define; fallback below. */
#ifndef REGEXXER_LOCALEDIR
#define REGEXXER_LOCALEDIR "/usr/share/locale"
#endif
#endif
