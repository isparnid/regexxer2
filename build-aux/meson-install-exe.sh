#!/bin/sh
# Installation de l'exécutable regexxer2 pour Meson
# Meson définit les variables d'environnement suivantes :
#   MESON_BUILD_ROOT        répertoire de build
#   MESON_INSTALL_PREFIX    préfixe d'installation (ex: /usr/local)
#   MESON_INSTALL_DESTDIR_PREFIX  DESTDIR + PREFIX
set -e
mkdir -p "$MESON_INSTALL_DESTDIR_PREFIX/bin"
cp "$MESON_BUILD_ROOT/regexxer2" "$MESON_INSTALL_DESTDIR_PREFIX/bin/regexxer2"
chmod 755 "$MESON_INSTALL_DESTDIR_PREFIX/bin/regexxer2"