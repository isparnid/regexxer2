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
