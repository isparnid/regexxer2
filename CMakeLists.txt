cmake_minimum_required(VERSION 3.16)
project(regexxer2)
include_directories(${CMAKE_SOURCE_DIR}/src)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM4 IMPORTED_TARGET REQUIRED gtkmm-4.0)

add_executable(regexxer2
  src/mainwindow.cc
  src/main.cc
  src/controller.cc
  src/filebuffer.cc
  src/filebufferundo.cc
  src/fileio.cc
  src/fileshared.cc
  src/filetree.cc
  src/filetreeprivate.cc
  src/prefdialog.cc
  src/statusline.cc
  src/completionstack.cc
  src/signalutils.cc
  src/stringutils.cc
  src/translation.cc
  src/undostack.cc
)

target_link_libraries(regexxer2 PRIVATE PkgConfig::GTKMM4 ${SIGC3_LIBRARIES})

# GSettings schema: generate org.regexxer.gschema.xml from the .in template,
# compile it for local runs and install it system-wide.
find_program(GLIB_COMPILE_SCHEMAS glib-compile-schemas REQUIRED)
find_program(INTLTOOL_MERGE intltool-merge)
if(INTLTOOL_MERGE)
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/org.regexxer.gschema.xml
    COMMAND ${INTLTOOL_MERGE} -s -u -c ${CMAKE_SOURCE_DIR}/po/.intltool-merge-cache
            ${CMAKE_SOURCE_DIR}/po ${CMAKE_SOURCE_DIR}/ui/org.regexxer.gschema.xml.in
            ${CMAKE_CURRENT_BINARY_DIR}/org.regexxer.gschema.xml
    DEPENDS ${CMAKE_SOURCE_DIR}/ui/org.regexxer.gschema.xml.in
    COMMENT "Merging GSettings schema translations")
else()
  # Fallback without translations: strip the intltool underscores (<_x> -> <x>).
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/org.regexxer.gschema.xml
    COMMAND python3 ${CMAKE_SOURCE_DIR}/build-aux/strip-intltool.py
            ${CMAKE_SOURCE_DIR}/ui/org.regexxer.gschema.xml.in
            ${CMAKE_CURRENT_BINARY_DIR}/org.regexxer.gschema.xml
    DEPENDS ${CMAKE_SOURCE_DIR}/ui/org.regexxer.gschema.xml.in
            ${CMAKE_SOURCE_DIR}/build-aux/strip-intltool.py
    COMMENT "Generating GSettings schema (untranslated fallback)")
endif()
add_custom_target(regexxer-schema ALL
  DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/org.regexxer.gschema.xml)
add_custom_command(TARGET regexxer-schema POST_BUILD
  COMMAND ${GLIB_COMPILE_SCHEMAS} ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Compiling GSettings schemas")
add_dependencies(regexxer2 regexxer-schema)

include(GNUInstallDirs)
install(TARGETS regexxer2 DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.regexxer.gschema.xml
  DESTINATION ${CMAKE_INSTALL_DATADIR}/glib-2.0/schemas)
install(CODE "execute_process(COMMAND ${GLIB_COMPILE_SCHEMAS} \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/glib-2.0/schemas)")
