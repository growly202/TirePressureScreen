# Use the exported source list; never glob XML, assets, or desktop build files.
include(${CMAKE_CURRENT_LIST_DIR}/UI/file_list_gen.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/UI/user_config.cmake)

idf_component_register(
    SRCS main.c app_ui.c board_display.c gt911_report.c ${LV_EDITOR_PROJECT_SOURCES}
    INCLUDE_DIRS ../include UI
    REQUIRES lvgl
    PRIV_REQUIRES esp_timer log freertos esp_lcd esp_driver_i2c esp_driver_gpio esp_psram heap
)
