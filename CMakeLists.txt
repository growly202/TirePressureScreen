cmake_minimum_required(VERSION 3.16.0)
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/.pio/libdeps/waveshare_lcd_7/lvgl")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(TirePressureScreen)

idf_component_get_property(lvgl_lib lvgl COMPONENT_LIB)
target_include_directories(${lvgl_lib} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_compile_definitions(${lvgl_lib} PUBLIC LV_CONF_INCLUDE_SIMPLE LV_LVGL_H_INCLUDE_SIMPLE LV_KCONFIG_IGNORE)
