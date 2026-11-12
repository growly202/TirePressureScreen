# Use the exported source list; never glob XML, assets, or desktop build files.
include(${CMAKE_CURRENT_LIST_DIR}/UI/file_list_gen.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/UI/user_config.cmake)

set(RTL_433_SOURCES
    rtl_433/abuf.c
    rtl_433/bitbuffer.c
    rtl_433/bit_util.c
    rtl_433/compat_time.c
    rtl_433/data.c
    rtl_433/decoder_util.c
    rtl_433/list.c
    rtl_433/logger.c
    rtl_433/output_log.c
    rtl_433/pulse_analyzer.c
    rtl_433/pulse_data.c
    rtl_433/pulse_slicer.c
    rtl_433/r_api.c
    rtl_433/r_util.c
    rtl_433/devices/schraeder.c
    rtl_433/devices/tpms_abarth124.c
    rtl_433/devices/tpms_airpuxem.c
    rtl_433/devices/tpms_ave.c
    rtl_433/devices/tpms_bmw.c
    rtl_433/devices/tpms_bmw_g3.c
    rtl_433/devices/tpms_citroen.c
    rtl_433/devices/tpms_eezrv.c
    rtl_433/devices/tpms_elantra2012.c
    rtl_433/devices/tpms_ford.c
    rtl_433/devices/tpms_gear_hive.c
    rtl_433/devices/tpms_gm.c
    rtl_433/devices/tpms_honda.c
    rtl_433/devices/tpms_hyundai_vdo.c
    rtl_433/devices/tpms_imars_t240.c
    rtl_433/devices/tpms_jansite.c
    rtl_433/devices/tpms_jansite_solar.c
    rtl_433/devices/tpms_jansite_ty468.c
    rtl_433/devices/tpms_jansite_ty588.c
    rtl_433/devices/tpms_jeep.c
    rtl_433/devices/tpms_kia.c
    rtl_433/devices/tpms_mercedes_benz.c
    rtl_433/devices/tpms_nissan.c
    rtl_433/devices/tpms_pmv107j.c
    rtl_433/devices/tpms_porsche.c
    rtl_433/devices/tpms_renault_0435r.c
    rtl_433/devices/tpms_renault.c
    rtl_433/devices/tpms_schrader_motorcycle.c
    rtl_433/devices/tpms_sefis_m3.c
    rtl_433/devices/tpms_smartire.c
    rtl_433/devices/tpms_toyota.c
    rtl_433/devices/tpms_truck.c
    rtl_433/devices/tpms_trw.c
    rtl_433/devices/tpms_tyreguard400.c
)

idf_component_register(
    SRCS main.c app_ui.c tire_data.c board_display.c gt911_report.c tpms_espnow.c tpms_cc1101.c ${RTL_433_SOURCES} ${LV_EDITOR_PROJECT_SOURCES}
    INCLUDE_DIRS ../include ../include/rtl_433 UI
    REQUIRES lvgl
    PRIV_REQUIRES esp_timer log freertos esp_lcd esp_driver_i2c esp_driver_gpio esp_driver_spi esp_psram heap esp_wifi esp_netif esp_event nvs_flash
)

set_source_files_properties(${RTL_433_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wno-error;-Wno-format")
