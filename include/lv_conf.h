#ifndef LV_CONF_H
#define LV_CONF_H

/* Shared by ESP-IDF and the desktop renderer (LVGL 9.5). */
#define LV_COLOR_DEPTH 16
#define LV_USE_OS LV_OS_NONE
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1
#define LV_USE_OBJ_NAME 1
#define LV_USE_TRANSLATION 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

/* Leave LV_USE_XML undefined: the exported headers use #ifdef, and this
 * project uses generated C rather than the optional XML runtime. */
#endif
