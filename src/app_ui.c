#include "app_ui.h"
#include "UI_design.h"

lv_obj_t *app_ui_start(void)
{
    /* All current fonts/images are C arrays; no filesystem is required. */
    UI_design_init("");
    lv_obj_t *screen = dashboard_create();
    if (screen != NULL) {
        lv_screen_load(screen);
    }
    return screen;
}
