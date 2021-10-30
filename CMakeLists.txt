cmake_minimum_required(VERSION 2.8)
project(rofi-bluetooth)



# Plugin

find_package(PkgConfig)
pkg_search_module(GLIB2 REQUIRED glib-2.0)
pkg_search_module(DBUS-1 REQUIRED dbus-1)
pkg_search_module(CAIRO REQUIRED cairo)

pkg_get_variable(ROFI_PLUGINS_DIR rofi pluginsdir)

include_directories(
    include
    ${GLIB2_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
    ${DBUS-1_INCLUDE_DIRS}
)

file(GLOB SRC "src/*.c")

add_library(bluetooth SHARED ${SRC})
set_target_properties(bluetooth PROPERTIES PREFIX "")

target_link_libraries(bluetooth
    ${GLIB2_LIBRARIES}
    ${DBUS-1_LIBRARIES}
    ${CAIRO_LIBRARIES}
)

install(TARGETS bluetooth DESTINATION ${ROFI_PLUGINS_DIR})



# Manpage

# add_custom_command(OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/doc/rofi-file-browser-extended.1.gz"
#                    COMMAND gzip -kf "${CMAKE_CURRENT_SOURCE_DIR}/doc/rofi-file-browser-extended.1"
#                    COMMENT "Packing manpage")

# add_custom_target(manpage ALL DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/doc/rofi-file-browser-extended.1.gz")

# install(FILES "doc/rofi-file-browser-extended.1.gz" DESTINATION "/usr/share/man/man1")
