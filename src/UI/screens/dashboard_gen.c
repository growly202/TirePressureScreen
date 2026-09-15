/**
 * @file dashboard_gen.c
 * @brief Template source file for LVGL objects
 */

/*********************
 *      INCLUDES
 *********************/

#include "dashboard_gen.h"
#include "../UI_design.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/***********************
 *  STATIC VARIABLES
 **********************/

/***********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * dashboard_create(void)
{
    LV_TRACE_OBJ_CREATE("begin");

    static lv_style_t style_panel;
    static lv_style_t style_center_panel;
    static lv_style_t style_wheel_label;
    static lv_style_t style_pressure;
    static lv_style_t style_unit;
    static lv_style_t style_center_text;
    static lv_style_t style_center_status;
    static lv_style_t style_chart;
    static lv_style_t style_chart_line;
    static lv_style_t style_hide_points;

    static bool style_inited = false;

    if (!style_inited) {
        #if UI_DESIGN_CHECK_COMPILE_TARGET(UI_DESIGN_TARGET_ALL)
        if (UI_design_check_target(UI_DESIGN_TARGET_ALL)) {
            lv_style_init(&style_panel);
            lv_style_set_bg_color(&style_panel, lv_color_hex(0x11171E));
            lv_style_set_bg_opa(&style_panel, (255 * 100 / 100));
            lv_style_set_border_color(&style_panel, lv_color_hex(0x29343F));
            lv_style_set_border_width(&style_panel, 1);
            lv_style_set_radius(&style_panel, 18);
            lv_style_set_pad_all(&style_panel, 0);

            lv_style_init(&style_center_panel);
            lv_style_set_bg_color(&style_center_panel, lv_color_hex(0x0D1218));
            lv_style_set_bg_opa(&style_center_panel, (255 * 100 / 100));
            lv_style_set_border_color(&style_center_panel, lv_color_hex(0x202A33));
            lv_style_set_border_width(&style_center_panel, 1);
            lv_style_set_radius(&style_center_panel, 36);
            lv_style_set_pad_all(&style_center_panel, 0);

            lv_style_init(&style_wheel_label);
            lv_style_set_text_color(&style_wheel_label, lv_color_hex(0x8E9AA6));
            lv_style_set_text_font(&style_wheel_label, font_label_18);

            lv_style_init(&style_pressure);
            lv_style_set_text_color(&style_pressure, lv_color_hex(0xF4F7FA));
            lv_style_set_text_font(&style_pressure, font_pressure_52);

            lv_style_init(&style_unit);
            lv_style_set_text_color(&style_unit, lv_color_hex(0x52D5FF));
            lv_style_set_text_font(&style_unit, font_unit_22);

            lv_style_init(&style_center_text);
            lv_style_set_text_color(&style_center_text, lv_color_hex(0x71808D));
            lv_style_set_text_font(&style_center_text, font_label_18);
            lv_style_set_text_align(&style_center_text, LV_TEXT_ALIGN_CENTER);

            lv_style_init(&style_center_status);
            lv_style_set_text_color(&style_center_status, lv_color_hex(0x66DDB0));
            lv_style_set_text_font(&style_center_status, font_label_18);
            lv_style_set_text_align(&style_center_status, LV_TEXT_ALIGN_CENTER);

            lv_style_init(&style_chart);
            lv_style_set_bg_color(&style_chart, lv_color_hex(0x080D12));
            lv_style_set_bg_opa(&style_chart, (255 * 100 / 100));
            lv_style_set_border_color(&style_chart, lv_color_hex(0x222D36));
            lv_style_set_border_width(&style_chart, 1);
            lv_style_set_radius(&style_chart, 13);
            lv_style_set_pad_all(&style_chart, 9);
            lv_style_set_line_color(&style_chart, lv_color_hex(0x27323C));
            lv_style_set_line_width(&style_chart, 1);

            lv_style_init(&style_chart_line);
            lv_style_set_line_width(&style_chart_line, 3);
            lv_style_set_line_rounded(&style_chart_line, true);

            lv_style_init(&style_hide_points);
            lv_style_set_width(&style_hide_points, 0);
            lv_style_set_height(&style_hide_points, 0);

        }
        #endif
        style_inited = true;
    }


    lv_obj_t * the_root = NULL;

    #if UI_DESIGN_CHECK_COMPILE_TARGET(UI_DESIGN_TARGET_ALL)
    if (UI_design_check_target(UI_DESIGN_TARGET_ALL)) {
        lv_obj_t * lv_obj_0 = lv_obj_create(NULL);
        lv_obj_set_name_static(lv_obj_0, "dashboard_#");
        lv_obj_set_width(lv_obj_0, lv_pct(100));
        lv_obj_set_height(lv_obj_0, lv_pct(100));
        lv_obj_set_flag(lv_obj_0, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_set_style_bg_color(lv_obj_0, lv_color_hex(0x070A0E), 0);
        lv_obj_set_style_bg_opa(lv_obj_0, (255 * 100 / 100), 0);
        lv_obj_set_style_border_width(lv_obj_0, 0, 0);
        lv_obj_set_style_pad_all(lv_obj_0, 0, 0);

        lv_obj_t * center_panel = lv_obj_create(lv_obj_0);
        lv_obj_set_name(center_panel, "center_panel");
        lv_obj_set_x(center_panel, 312);
        lv_obj_set_y(center_panel, 12);
        lv_obj_set_width(center_panel, 176);
        lv_obj_set_height(center_panel, 456);
        lv_obj_set_flag(center_panel, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_add_style(center_panel, &style_center_panel, 0);
        lv_obj_t * lv_label_0 = lv_label_create(center_panel);
        lv_obj_set_x(lv_label_0, 0);
        lv_obj_set_y(lv_label_0, 421);
        lv_obj_set_width(lv_label_0, 176);
        lv_label_set_text(lv_label_0, "SYSTEM NORMAL");
        lv_obj_add_style(lv_label_0, &style_center_status, 0);

        lv_obj_t * panel_fl = lv_obj_create(lv_obj_0);
        lv_obj_set_name(panel_fl, "panel_fl");
        lv_obj_set_x(panel_fl, 12);
        lv_obj_set_y(panel_fl, 12);
        lv_obj_set_width(panel_fl, 284);
        lv_obj_set_height(panel_fl, 220);
        lv_obj_set_flag(panel_fl, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_add_style(panel_fl, &style_panel, 0);
        lv_obj_t * label_fl = lv_label_create(panel_fl);
        lv_obj_set_name(label_fl, "label_fl");
        lv_obj_set_x(label_fl, 14);
        lv_obj_set_y(label_fl, 12);
        lv_label_set_text(label_fl, "FRONT LEFT");
        lv_obj_add_style(label_fl, &style_wheel_label, 0);

        lv_obj_t * pressure_value_fl = lv_label_create(panel_fl);
        lv_obj_set_name(pressure_value_fl, "pressure_value_fl");
        lv_obj_set_x(pressure_value_fl, 14);
        lv_obj_set_y(pressure_value_fl, 34);
        lv_label_set_text(pressure_value_fl, "2.31");
        lv_obj_add_style(pressure_value_fl, &style_pressure, 0);

        lv_obj_t * lv_label_1 = lv_label_create(panel_fl);
        lv_obj_set_x(lv_label_1, 164);
        lv_obj_set_y(lv_label_1, 64);
        lv_label_set_text(lv_label_1, "bar");
        lv_obj_add_style(lv_label_1, &style_unit, 0);

        lv_obj_t * chart_fl = lv_chart_create(panel_fl);
        lv_obj_set_name(chart_fl, "chart_fl");
        lv_obj_set_x(chart_fl, 14);
        lv_obj_set_y(chart_fl, 118);
        lv_obj_set_width(chart_fl, 256);
        lv_obj_set_height(chart_fl, 88);
        lv_chart_set_type(chart_fl, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(chart_fl, 12);
        lv_chart_set_update_mode(chart_fl, LV_CHART_UPDATE_MODE_SHIFT);
        lv_chart_set_hor_div_line_count(chart_fl, 3);
        lv_chart_set_ver_div_line_count(chart_fl, 5);
        lv_obj_add_style(chart_fl, &style_chart, 0);
        lv_obj_add_style(chart_fl, &style_chart_line, LV_PART_ITEMS);
        lv_obj_add_style(chart_fl, &style_hide_points, LV_PART_INDICATOR);
        lv_chart_set_axis_min_value(chart_fl, LV_CHART_AXIS_PRIMARY_Y, 215);
        lv_chart_set_axis_max_value(chart_fl, LV_CHART_AXIS_PRIMARY_Y, 245);
        lv_chart_series_t * lv_chart_series_0 = lv_chart_add_series(chart_fl, lv_color_hex(0x4DCAFF), LV_CHART_AXIS_PRIMARY_Y);
        static const int32_t chart_fl_values_0[] = {229, 230, 230, 231, 232, 231, 231, 232, 231, 230, 231, 231};
        lv_chart_set_series_values(chart_fl, lv_chart_series_0, chart_fl_values_0, 12);

        lv_obj_t * panel_fr = lv_obj_create(lv_obj_0);
        lv_obj_set_name(panel_fr, "panel_fr");
        lv_obj_set_x(panel_fr, 504);
        lv_obj_set_y(panel_fr, 12);
        lv_obj_set_width(panel_fr, 284);
        lv_obj_set_height(panel_fr, 220);
        lv_obj_set_flag(panel_fr, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_add_style(panel_fr, &style_panel, 0);
        lv_obj_t * label_fr = lv_label_create(panel_fr);
        lv_obj_set_name(label_fr, "label_fr");
        lv_obj_set_x(label_fr, 14);
        lv_obj_set_y(label_fr, 12);
        lv_label_set_text(label_fr, "FRONT RIGHT");
        lv_obj_add_style(label_fr, &style_wheel_label, 0);

        lv_obj_t * pressure_value_fr = lv_label_create(panel_fr);
        lv_obj_set_name(pressure_value_fr, "pressure_value_fr");
        lv_obj_set_x(pressure_value_fr, 14);
        lv_obj_set_y(pressure_value_fr, 34);
        lv_label_set_text(pressure_value_fr, "2.29");
        lv_obj_add_style(pressure_value_fr, &style_pressure, 0);

        lv_obj_t * lv_label_2 = lv_label_create(panel_fr);
        lv_obj_set_x(lv_label_2, 164);
        lv_obj_set_y(lv_label_2, 64);
        lv_label_set_text(lv_label_2, "bar");
        lv_obj_add_style(lv_label_2, &style_unit, 0);

        lv_obj_t * chart_fr = lv_chart_create(panel_fr);
        lv_obj_set_name(chart_fr, "chart_fr");
        lv_obj_set_x(chart_fr, 14);
        lv_obj_set_y(chart_fr, 118);
        lv_obj_set_width(chart_fr, 256);
        lv_obj_set_height(chart_fr, 88);
        lv_chart_set_type(chart_fr, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(chart_fr, 12);
        lv_chart_set_update_mode(chart_fr, LV_CHART_UPDATE_MODE_SHIFT);
        lv_chart_set_hor_div_line_count(chart_fr, 3);
        lv_chart_set_ver_div_line_count(chart_fr, 5);
        lv_obj_add_style(chart_fr, &style_chart, 0);
        lv_obj_add_style(chart_fr, &style_chart_line, LV_PART_ITEMS);
        lv_obj_add_style(chart_fr, &style_hide_points, LV_PART_INDICATOR);
        lv_chart_set_axis_min_value(chart_fr, LV_CHART_AXIS_PRIMARY_Y, 215);
        lv_chart_set_axis_max_value(chart_fr, LV_CHART_AXIS_PRIMARY_Y, 245);
        lv_chart_series_t * lv_chart_series_1 = lv_chart_add_series(chart_fr, lv_color_hex(0x55D6E8), LV_CHART_AXIS_PRIMARY_Y);
        static const int32_t chart_fr_values_0[] = {231, 230, 229, 230, 230, 229, 228, 229, 230, 229, 229, 229};
        lv_chart_set_series_values(chart_fr, lv_chart_series_1, chart_fr_values_0, 12);

        lv_obj_t * panel_rl = lv_obj_create(lv_obj_0);
        lv_obj_set_name(panel_rl, "panel_rl");
        lv_obj_set_x(panel_rl, 12);
        lv_obj_set_y(panel_rl, 248);
        lv_obj_set_width(panel_rl, 284);
        lv_obj_set_height(panel_rl, 220);
        lv_obj_set_flag(panel_rl, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_add_style(panel_rl, &style_panel, 0);
        lv_obj_t * label_rl = lv_label_create(panel_rl);
        lv_obj_set_name(label_rl, "label_rl");
        lv_obj_set_x(label_rl, 14);
        lv_obj_set_y(label_rl, 12);
        lv_label_set_text(label_rl, "REAR LEFT");
        lv_obj_add_style(label_rl, &style_wheel_label, 0);

        lv_obj_t * pressure_value_rl = lv_label_create(panel_rl);
        lv_obj_set_name(pressure_value_rl, "pressure_value_rl");
        lv_obj_set_x(pressure_value_rl, 14);
        lv_obj_set_y(pressure_value_rl, 34);
        lv_label_set_text(pressure_value_rl, "2.34");
        lv_obj_add_style(pressure_value_rl, &style_pressure, 0);

        lv_obj_t * lv_label_3 = lv_label_create(panel_rl);
        lv_obj_set_x(lv_label_3, 164);
        lv_obj_set_y(lv_label_3, 64);
        lv_label_set_text(lv_label_3, "bar");
        lv_obj_add_style(lv_label_3, &style_unit, 0);

        lv_obj_t * chart_rl = lv_chart_create(panel_rl);
        lv_obj_set_name(chart_rl, "chart_rl");
        lv_obj_set_x(chart_rl, 14);
        lv_obj_set_y(chart_rl, 118);
        lv_obj_set_width(chart_rl, 256);
        lv_obj_set_height(chart_rl, 88);
        lv_chart_set_type(chart_rl, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(chart_rl, 12);
        lv_chart_set_update_mode(chart_rl, LV_CHART_UPDATE_MODE_SHIFT);
        lv_chart_set_hor_div_line_count(chart_rl, 3);
        lv_chart_set_ver_div_line_count(chart_rl, 5);
        lv_obj_add_style(chart_rl, &style_chart, 0);
        lv_obj_add_style(chart_rl, &style_chart_line, LV_PART_ITEMS);
        lv_obj_add_style(chart_rl, &style_hide_points, LV_PART_INDICATOR);
        lv_chart_set_axis_min_value(chart_rl, LV_CHART_AXIS_PRIMARY_Y, 215);
        lv_chart_set_axis_max_value(chart_rl, LV_CHART_AXIS_PRIMARY_Y, 245);
        lv_chart_series_t * lv_chart_series_2 = lv_chart_add_series(chart_rl, lv_color_hex(0x6BB8FF), LV_CHART_AXIS_PRIMARY_Y);
        static const int32_t chart_rl_values_0[] = {232, 233, 233, 234, 235, 234, 234, 235, 234, 233, 234, 234};
        lv_chart_set_series_values(chart_rl, lv_chart_series_2, chart_rl_values_0, 12);

        lv_obj_t * panel_rr = lv_obj_create(lv_obj_0);
        lv_obj_set_name(panel_rr, "panel_rr");
        lv_obj_set_x(panel_rr, 504);
        lv_obj_set_y(panel_rr, 248);
        lv_obj_set_width(panel_rr, 284);
        lv_obj_set_height(panel_rr, 220);
        lv_obj_set_flag(panel_rr, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_add_style(panel_rr, &style_panel, 0);
        lv_obj_t * label_rr = lv_label_create(panel_rr);
        lv_obj_set_name(label_rr, "label_rr");
        lv_obj_set_x(label_rr, 14);
        lv_obj_set_y(label_rr, 12);
        lv_label_set_text(label_rr, "REAR RIGHT");
        lv_obj_add_style(label_rr, &style_wheel_label, 0);

        lv_obj_t * pressure_value_rr = lv_label_create(panel_rr);
        lv_obj_set_name(pressure_value_rr, "pressure_value_rr");
        lv_obj_set_x(pressure_value_rr, 14);
        lv_obj_set_y(pressure_value_rr, 34);
        lv_label_set_text(pressure_value_rr, "2.30");
        lv_obj_add_style(pressure_value_rr, &style_pressure, 0);

        lv_obj_t * lv_label_4 = lv_label_create(panel_rr);
        lv_obj_set_x(lv_label_4, 164);
        lv_obj_set_y(lv_label_4, 64);
        lv_label_set_text(lv_label_4, "bar");
        lv_obj_add_style(lv_label_4, &style_unit, 0);

        lv_obj_t * chart_rr = lv_chart_create(panel_rr);
        lv_obj_set_name(chart_rr, "chart_rr");
        lv_obj_set_x(chart_rr, 14);
        lv_obj_set_y(chart_rr, 118);
        lv_obj_set_width(chart_rr, 256);
        lv_obj_set_height(chart_rr, 88);
        lv_chart_set_type(chart_rr, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(chart_rr, 12);
        lv_chart_set_update_mode(chart_rr, LV_CHART_UPDATE_MODE_SHIFT);
        lv_chart_set_hor_div_line_count(chart_rr, 3);
        lv_chart_set_ver_div_line_count(chart_rr, 5);
        lv_obj_add_style(chart_rr, &style_chart, 0);
        lv_obj_add_style(chart_rr, &style_chart_line, LV_PART_ITEMS);
        lv_obj_add_style(chart_rr, &style_hide_points, LV_PART_INDICATOR);
        lv_chart_set_axis_min_value(chart_rr, LV_CHART_AXIS_PRIMARY_Y, 215);
        lv_chart_set_axis_max_value(chart_rr, LV_CHART_AXIS_PRIMARY_Y, 245);
        lv_chart_series_t * lv_chart_series_3 = lv_chart_add_series(chart_rr, lv_color_hex(0x5DD6A8), LV_CHART_AXIS_PRIMARY_Y);
        static const int32_t chart_rr_values_0[] = {229, 229, 230, 231, 230, 230, 231, 230, 229, 230, 230, 230};
        lv_chart_set_series_values(chart_rr, lv_chart_series_3, chart_rr_values_0, 12);

        lv_obj_t * car_top_view = lv_image_create(lv_obj_0);
        lv_obj_set_name(car_top_view, "car_top_view");
        lv_obj_set_x(car_top_view, 322);
        lv_obj_set_y(car_top_view, 70);
        lv_obj_set_width(car_top_view, 156);
        lv_obj_set_height(car_top_view, 340);
        lv_image_set_src(car_top_view, car_top_image);
        lv_image_set_inner_align(car_top_view, LV_IMAGE_ALIGN_CENTER);
        lv_obj_set_flag(car_top_view, LV_OBJ_FLAG_CLICKABLE, false);
        lv_obj_set_flag(car_top_view, LV_OBJ_FLAG_SCROLLABLE, false);
        lv_obj_set_style_bg_opa(car_top_view, (255 * 0 / 100), 0);
        lv_obj_set_style_border_width(car_top_view, 0, 0);

        the_root = lv_obj_0;
    }
    #endif

    LV_TRACE_OBJ_CREATE("finished");

    return the_root;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

