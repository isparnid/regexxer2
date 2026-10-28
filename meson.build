project('regexxer2', 'cpp', version: '0.1.0', license: 'GPL-2.0-or-later')

# ---------------------------------------------------------------------------
# Dépendances
# ---------------------------------------------------------------------------
gtkmm4 = dependency('gtkmm-4.0')
x11    = dependency('x11')

# ---------------------------------------------------------------------------
# Répertoires d'inclusion
# ---------------------------------------------------------------------------
inc_src = include_directories('src')

# ---------------------------------------------------------------------------
# Sources (fichiers .cc ; les .h sont trouvés via include_directories)
# ---------------------------------------------------------------------------
sources = [
  'src/main.cc',
  'src/mainwindow.cc',
  'src/controller.cc',
  'src/filebuffer.cc',
  'src/filebufferundo.cc',
  'src/fileio.cc',
  'src/fileshared.cc',
  'src/filetree.cc',
  'src/filetreeprivate.cc',
  'src/prefdialog.cc',
  'src/statusline.cc',
  'src/completionstack.cc',
  'src/signalutils.cc',
  'src/stringutils.cc',
  'src/translation.cc',
  'src/undostack.cc',
]

# ---------------------------------------------------------------------------
# Exécutable
# ---------------------------------------------------------------------------
exe = executable('regexxer2', sources,
  include_directories: inc_src,
  dependencies: [gtkmm4, x11],
)

# ---------------------------------------------------------------------------
# Schéma GSettings
#   Génère org.regexxer.gschema.xml depuis le template .in (marques intltool
#   <_...>...</_>). On utilise intltool-merge s'il est disponible, sinon on
#   recourt au script build-aux/strip-intltool.py qui enlève simplement les
#   marques de traduction.
# ---------------------------------------------------------------------------
intltool_merge = find_program('intltool-merge', required: false)

if intltool_merge.found()
  gschema_xml = custom_target('regexxer-gschema-xml',
    input: 'ui/org.regexxer.gschema.xml.in',
    output: 'org.regexxer.gschema.xml',
    command: [intltool_merge, '-s', '-u', 'po', '@INPUT@', '@OUTPUT@'],
    install: true,
    install_dir: join_paths(get_option('datadir'), 'glib-2.0', 'schemas'),
  )
else
  gschema_xml = custom_target('regexxer-gschema-xml',
    input: 'ui/org.regexxer.gschema.xml.in',
    output: 'org.regexxer.gschema.xml',
    command: [find_program('python3'), '@SOURCE_ROOT@/build-aux/strip-intltool.py', '@INPUT@', '@OUTPUT@'],
    install: true,
    install_dir: join_paths(get_option('datadir'), 'glib-2.0', 'schemas'),
  )
endif

# ---------------------------------------------------------------------------
# Fichier .desktop (contient aussi des marques intltool _Name / _Comment)
# ---------------------------------------------------------------------------
desktop = custom_target('regexxer-desktop',
  input: 'ui/regexxer.desktop.in',
  output: 'regexxer.desktop',
  command: [find_program('python3'), '@SOURCE_ROOT@/build-aux/strip-intltool.py', '@INPUT@', '@OUTPUT@'],
  install: true,
  install_dir: join_paths(get_option('datadir'), 'applications'),
)

# ---------------------------------------------------------------------------
# Installation de l'icône (fichier statique)
# ---------------------------------------------------------------------------
install_data('ui/regexxer.png',
  install_dir: join_paths(get_option('datadir'), 'icons', 'hicolor', '48x48', 'apps'),
)

# ---------------------------------------------------------------------------
# Installation de l'exécutable
#   Utilise un script d'installation dédié car la fonction globale
#   install() n'est pas disponible dans cette version de Meson.
# ---------------------------------------------------------------------------
meson.add_install_script('build-aux/meson-install-exe.sh')