
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_BINARY_DIR}/../
)

# mengshee

set(mengshee_SRCS
   main.cpp
   multifile.cpp
   okular_main.cpp
   shell.cpp
   shellutils.cpp
   welcomescreen.cpp

   ../gui/recentitemsmodel.cpp

   shell.qrc
)

if(WIN32)
    set(MENGSHEE_ICON_RC_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../icons/mengshee.ico")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/../icons/mengshee.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/mengshee.rc"
        @ONLY
    )
    list(APPEND mengshee_SRCS "${CMAKE_CURRENT_BINARY_DIR}/mengshee.rc")
endif()

ki18n_wrap_ui(mengshee_SRCS
    welcomescreen.ui)

file(GLOB ICONS_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/../icons/*-apps-mengshee.png")
ecm_add_app_icon(mengshee_SRCS ICONS ${ICONS_SRCS})

add_executable(mengshee ${mengshee_SRCS})
target_compile_definitions(mengshee PRIVATE MENGSHEE_ABOUT_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(mengshee KF6::I18n KF6::Parts KF6::WindowSystem KF6::Crash KF6::IconThemes KF6::KIOGui Qt6::Network Qt6::Concurrent)
if (TARGET Qt6::DBus)
    target_link_libraries(mengshee Qt6::DBus)
endif()
if (HAVE_X11)
    target_link_libraries(mengshee Qt6::GuiPrivate)
endif()

install(TARGETS mengshee ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# Mengshee shell data files

install( PROGRAMS org.jairy.mengshee.desktop  DESTINATION  ${KDE_INSTALL_APPDIR} )
install( FILES org.jairy.mengshee.appdata.xml DESTINATION  ${KDE_INSTALL_METAINFODIR} )
