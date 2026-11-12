#include "app_ui.h"
#include "UI_design.h"
#include <string.h>

enum { HISTORY_POINTS = 12, TIME_LABEL_COUNT = 4 };

static lv_obj_t *pressure_labels[TIRE_COUNT];
static lv_obj_t *charts[TIRE_COUNT];
static lv_chart_series_t *series[TIRE_COUNT];
static lv_obj_t *range_labels[TIRE_COUNT];
static lv_obj_t *time_labels[TIRE_COUNT][TIME_LABEL_COUNT];
static lv_obj_t *status_label;
static lv_obj_t *wheel_status[TIRE_COUNT];

static struct {
    bool seen, lost, change_alert;
    uint16_t pressure;
    uint32_t last_seen, last_point, last_change;
    int32_t y_points[HISTORY_POINTS];
    uint32_t timestamps_ms[HISTORY_POINTS];
} wheels[TIRE_COUNT];

static uint32_t started_at;
static uint32_t s_elapsed_offset_ms;
static lv_timer_t *signal_timer;

static void refresh_status(uint32_t now);
static const uint32_t normal_colors[TIRE_COUNT] = {0x4DCAFF, 0x55D6E8, 0x8DADFF, 0x5DD6A8};

static uint32_t current_timestamp_ms(uint32_t now)
{
    return s_elapsed_offset_ms + (uint32_t)(now - started_at);
}

static void format_backward_time_label(lv_obj_t *label, uint32_t delta_ms)
{
    if (delta_ms < 500U) {
        lv_label_set_text(label, "0:00");
        return;
    }
    uint32_t total_sec = (delta_ms + 500U) / 1000U;
    unsigned hours = (unsigned)(total_sec / 3600U);
    unsigned mins = (unsigned)((total_sec / 60U) % 60U);
    unsigned secs = (unsigned)(total_sec % 60U);
    if (hours > 0) {
        lv_label_set_text_fmt(label, "-%u:%02u:%02u", hours, mins, secs);
    } else {
        lv_label_set_text_fmt(label, "-%u:%02u", mins, secs);
    }
}

static void update_time_labels(unsigned wheel)
{
    unsigned valid_idx[HISTORY_POINTS];
    unsigned count = 0;
    for (unsigned i = 0; i < HISTORY_POINTS; ++i) {
        if (wheels[wheel].y_points[i] != LV_CHART_POINT_NONE) {
            valid_idx[count++] = i;
        }
    }

    if (count == 0) {
        for (unsigned k = 0; k < TIME_LABEL_COUNT; ++k) {
            if (time_labels[wheel][k]) {
                lv_label_set_text(time_labels[wheel][k], "");
            }
        }
        return;
    }

    uint32_t latest_ts = wheels[wheel].timestamps_ms[valid_idx[count - 1]];

    if (count <= TIME_LABEL_COUNT) {
        unsigned empty_slots = TIME_LABEL_COUNT - count;
        for (unsigned k = 0; k < empty_slots; ++k) {
            if (time_labels[wheel][k]) {
                lv_label_set_text(time_labels[wheel][k], "");
            }
        }
        for (unsigned k = 0; k < count; ++k) {
            lv_obj_t *lbl = time_labels[wheel][empty_slots + k];
            if (lbl) {
                uint32_t pt_ts = wheels[wheel].timestamps_ms[valid_idx[k]];
                uint32_t delta_ms = (latest_ts >= pt_ts) ? (latest_ts - pt_ts) : 0;
                format_backward_time_label(lbl, delta_ms);
            }
        }
        return;
    }

    for (unsigned k = 0; k < TIME_LABEL_COUNT; ++k) {
        unsigned pick = (k == TIME_LABEL_COUNT - 1)
                            ? (count - 1)
                            : ((count - 1) * k) / (TIME_LABEL_COUNT - 1);
        lv_obj_t *lbl = time_labels[wheel][k];
        if (lbl) {
            uint32_t pt_ts = wheels[wheel].timestamps_ms[valid_idx[pick]];
            uint32_t delta_ms = (latest_ts >= pt_ts) ? (latest_ts - pt_ts) : 0;
            format_backward_time_label(lbl, delta_ms);
        }
    }
}

static lv_obj_t *chart_label(lv_obj_t *parent, const char *name, const char *text,
                             int32_t x, int32_t y, int32_t width, lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_name_static(label, name);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x8E9AA6), 0);
    lv_obj_set_style_text_align(label, align, 0);
    return label;
}

static void add_chart_details(unsigned wheel)
{
    lv_obj_t *chart = charts[wheel];
    lv_obj_t *panel = lv_obj_get_parent(chart);
    /* Reserve room for axis labels within the original 284 x 220 panel. */
    lv_obj_set_pos(chart, 50, 122);
    lv_obj_set_size(chart, 220, 68);
    lv_obj_set_style_pad_all(chart, 4, 0);
    lv_chart_set_hor_div_line_count(chart, 3);
    lv_obj_update_layout(chart);
    int32_t left = lv_obj_get_x(chart) + lv_obj_get_style_border_width(chart, 0) +
                   lv_obj_get_style_pad_left(chart, 0);
    int32_t top = lv_obj_get_y(chart) + lv_obj_get_style_border_width(chart, 0) +
                  lv_obj_get_style_pad_top(chart, 0);
    int32_t width = lv_obj_get_content_width(chart);
    int32_t height = lv_obj_get_content_height(chart);
    int32_t half_text = lv_font_montserrat_14.line_height / 2;
    chart_label(panel, "chart_unit", "bar", 14, 100, 30, LV_TEXT_ALIGN_LEFT);
    chart_label(panel, "axis_max", "3.3", 14, top - half_text, 30, LV_TEXT_ALIGN_RIGHT);
    chart_label(panel, "axis_mid", "2.4", 14, top + height / 2 - half_text, 30, LV_TEXT_ALIGN_RIGHT);
    chart_label(panel, "axis_min", "1.5", 14, top + height - half_text, 30, LV_TEXT_ALIGN_RIGHT);

    static const char *const k_time_names[TIME_LABEL_COUNT] = {
        "time_t0", "time_t1", "time_t2", "time_t3"
    };
    static const lv_text_align_t k_time_aligns[TIME_LABEL_COUNT] = {
        LV_TEXT_ALIGN_LEFT, LV_TEXT_ALIGN_CENTER, LV_TEXT_ALIGN_CENTER, LV_TEXT_ALIGN_RIGHT
    };
    int32_t slot_w = 54;
    int32_t span_w = width + 8;
    for (unsigned k = 0; k < TIME_LABEL_COUNT; ++k) {
        int32_t x_pos = (left - 4) + ((span_w - slot_w) * (int32_t)k) / (TIME_LABEL_COUNT - 1);
        time_labels[wheel][k] = chart_label(panel, k_time_names[k], "", x_pos, 198, slot_w, k_time_aligns[k]);
    }

    wheel_status[wheel] = chart_label(panel, "wheel_status", "Waiting", 120, 12, 150,
                                       LV_TEXT_ALIGN_RIGHT);
    range_labels[wheel] = chart_label(panel, "chart_range", "MIN --   MAX --",
                                      50, 100, 220, LV_TEXT_ALIGN_RIGHT);
}

static void sync_chart_series(unsigned wheel)
{
    int32_t *y = lv_chart_get_series_y_array(charts[wheel], series[wheel]);
    if (y) {
        for (unsigned i = 0; i < HISTORY_POINTS; ++i) {
            y[i] = wheels[wheel].y_points[i];
        }
    }
    lv_chart_set_x_start_point(charts[wheel], series[wheel], 0);
}

static void update_chart_range(unsigned wheel)
{
    int32_t minimum = INT32_MAX, maximum = 0;
    for (unsigned point = 0; point < HISTORY_POINTS; ++point) {
        int32_t val = wheels[wheel].y_points[point];
        if (val == LV_CHART_POINT_NONE) continue;
        if (val < minimum) minimum = val;
        if (val > maximum) maximum = val;
    }
    if (minimum == INT32_MAX) {
        lv_label_set_text(range_labels[wheel], "MIN --   MAX --");
        return;
    }
    lv_label_set_text_fmt(range_labels[wheel], "MIN %u.%02u   MAX %u.%02u",
                          (unsigned)minimum / 100, (unsigned)minimum % 100,
                          (unsigned)maximum / 100, (unsigned)maximum % 100);
}

static void append_point(unsigned wheel, uint32_t now, int32_t pressure)
{
    for (unsigned i = 0; i < HISTORY_POINTS - 1; ++i) {
        wheels[wheel].y_points[i] = wheels[wheel].y_points[i + 1];
        wheels[wheel].timestamps_ms[i] = wheels[wheel].timestamps_ms[i + 1];
    }
    wheels[wheel].y_points[HISTORY_POINTS - 1] = pressure;
    wheels[wheel].timestamps_ms[HISTORY_POINTS - 1] =
        (pressure != LV_CHART_POINT_NONE) ? current_timestamp_ms(now) : 0;

    wheels[wheel].last_point = now;
    sync_chart_series(wheel);
    update_chart_range(wheel);
    update_time_labels(wheel);
    lv_chart_refresh(charts[wheel]);
}

static void check_signals(lv_timer_t *timer)
{
    (void)timer;
    refresh_status(lv_tick_get());
}

static void screen_deleted(lv_event_t *event)
{
    (void)event;
    if (signal_timer) lv_timer_delete(signal_timer);
    signal_timer = NULL;
}

lv_obj_t *app_ui_start(void)
{
    memset(wheels, 0, sizeof(wheels));
    memset(time_labels, 0, sizeof(time_labels));
    started_at = lv_tick_get();
    s_elapsed_offset_ms = 0;
    /* All current fonts/images are C arrays; no filesystem is required. */
    UI_design_init("");
    lv_obj_t *screen = dashboard_create();
    if (screen == NULL) return NULL;
    const char *pressure_names[] = {"pressure_value_fl", "pressure_value_fr", "pressure_value_rl", "pressure_value_rr"};
    const char *chart_names[] = {"chart_fl", "chart_fr", "chart_rl", "chart_rr"};
    status_label = lv_obj_find_by_name(screen, "system_status");
    if (!status_label) {
        lv_obj_delete(screen);
        return NULL;
    }
    for (unsigned i = 0; i < TIRE_COUNT; ++i) {
        for (unsigned j = 0; j < HISTORY_POINTS; ++j) {
            wheels[i].y_points[j] = LV_CHART_POINT_NONE;
            wheels[i].timestamps_ms[j] = 0;
        }
        pressure_labels[i] = lv_obj_find_by_name(screen, pressure_names[i]);
        charts[i] = lv_obj_find_by_name(screen, chart_names[i]);
        if (!pressure_labels[i] || !charts[i] ||
            !(series[i] = lv_chart_get_series_next(charts[i], NULL))) {
            lv_obj_delete(screen);
            return NULL;
        }
        lv_label_set_text(pressure_labels[i], "--");
        lv_chart_set_type(charts[i], LV_CHART_TYPE_LINE);
        lv_chart_set_update_mode(charts[i], LV_CHART_UPDATE_MODE_SHIFT);
        lv_obj_set_style_size(charts[i], 4, 4, LV_PART_INDICATOR);
        lv_chart_set_point_count(charts[i], HISTORY_POINTS);
        lv_chart_set_axis_min_value(charts[i], LV_CHART_AXIS_PRIMARY_Y, 150);
        lv_chart_set_axis_max_value(charts[i], LV_CHART_AXIS_PRIMARY_Y, 330);
        lv_chart_set_all_values(charts[i], series[i], LV_CHART_POINT_NONE);
        add_chart_details(i);
    }
    lv_label_set_text(status_label, "WAITING DATA");
    signal_timer = lv_timer_create(check_signals, 250, NULL);
    lv_obj_add_event_cb(screen, screen_deleted, LV_EVENT_DELETE, NULL);
    lv_screen_load(screen);
    return screen;
}

static void refresh_status(uint32_t now)
{
    bool low = false, high = false, lost = false, changed = false, waiting = false;
    for (unsigned i = 0; i < TIRE_COUNT; ++i) {
        uint32_t last_seen = wheels[i].seen ? wheels[i].last_seen : started_at;
        if (!wheels[i].lost &&
            (uint32_t)(now - last_seen) >= TIRE_SIGNAL_TIMEOUT_MS) {
            wheels[i].lost = true;
            /* A missing point breaks the line across a sensor outage. */
            if (wheels[i].seen) append_point(i, now, LV_CHART_POINT_NONE);
        }
        if (wheels[i].change_alert &&
            (uint32_t)(now - wheels[i].last_change) >= TIRE_CHANGE_ALERT_MS)
            wheels[i].change_alert = false;
        if (wheels[i].lost || !wheels[i].seen) {
            lost |= wheels[i].lost;
            waiting |= !wheels[i].lost;
            lv_label_set_text(pressure_labels[i], "--");
            lv_obj_set_style_text_color(pressure_labels[i], lv_color_hex(0x8E9AA6), 0);
            lv_chart_set_series_color(charts[i], series[i], lv_color_hex(0x8E9AA6));
            lv_label_set_text(wheel_status[i], wheels[i].lost ? "No Signal" : "Waiting");
            lv_obj_set_style_text_color(wheel_status[i], lv_color_hex(0x8E9AA6), 0);
            continue;
        }
        unsigned pressure = wheels[i].pressure;
        tire_state_t state = tire_pressure_state(pressure);
        low |= state == TIRE_LOW;
        high |= state == TIRE_HIGH;
        changed |= wheels[i].change_alert;
        uint32_t color = state == TIRE_LOW ? 0xFF6969 :
                         state == TIRE_HIGH || wheels[i].change_alert ? 0xFFB547 : normal_colors[i];
        lv_label_set_text_fmt(pressure_labels[i], "%u.%02u", pressure / 100, pressure % 100);
        lv_obj_set_style_text_color(pressure_labels[i],
                                   lv_color_hex(state == TIRE_NORMAL && !wheels[i].change_alert ? 0xF4F7FA : color), 0);
        lv_chart_set_series_color(charts[i], series[i], lv_color_hex(color));
        lv_label_set_text(wheel_status[i], wheels[i].change_alert ? "Rapid change" :
                          state == TIRE_LOW ? "Low pressure" : state == TIRE_HIGH ? "High pressure" : "");
        lv_obj_set_style_text_color(wheel_status[i], lv_color_hex(color), 0);
    }
    lv_label_set_text(status_label, low && high ? "CHECK TIRES" :
                     low ? "LOW PRESSURE" : high ? "HIGH PRESSURE" :
                     changed ? "RAPID CHANGE" : lost ? "SENSOR LOST" :
                     waiting ? "WAITING DATA" : "SYSTEM NORMAL");
    lv_obj_set_style_text_color(status_label,
                               lv_color_hex(low ? 0xFF6969 : high || changed || lost ? 0xFFB547 :
                                            waiting ? 0x8E9AA6 : 0x66DDB0), 0);
}

void app_ui_receive(unsigned wheel, uint16_t pressure)
{
    if (wheel >= TIRE_COUNT) return;
    uint32_t now = lv_tick_get();
    /* Check elapsed time even if a packet arrives before the watchdog runs. */
    refresh_status(now);
    if (pressure < 150) {
        lv_chart_set_axis_min_value(charts[wheel], LV_CHART_AXIS_PRIMARY_Y, 0);
    }
    bool fresh = !wheels[wheel].seen || wheels[wheel].lost;
    int delta = (int)pressure - wheels[wheel].pressure;
    bool sudden = !fresh && (delta >= TIRE_SUDDEN_CHANGE || delta <= -TIRE_SUDDEN_CHANGE);
    if (fresh) wheels[wheel].change_alert = false;
    if (sudden) {
        wheels[wheel].change_alert = true;
        wheels[wheel].last_change = now;
    }
    wheels[wheel].pressure = pressure;
    wheels[wheel].last_seen = now;
    wheels[wheel].seen = true;
    wheels[wheel].lost = false;
    if (fresh || sudden || (uint32_t)(now - wheels[wheel].last_point) >= TIRE_CHART_INTERVAL_MS) {
        append_point(wheel, now, pressure);
    }
    refresh_status(now);
}

void app_ui_update(const tire_sample_t *sample)
{
    for (unsigned i = 0; i < TIRE_COUNT; ++i) app_ui_receive(i, sample->pressure[i]);
}

void app_ui_get_state(app_ui_state_t *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    for (unsigned i = 0; i < TIRE_COUNT; ++i) {
        state->wheels[i].seen = wheels[i].seen;
        state->wheels[i].pressure = wheels[i].pressure;
    }
}

void app_ui_set_state(const app_ui_state_t *state)
{
    if (!state) return;
    uint32_t now = lv_tick_get();
    started_at = now;
    s_elapsed_offset_ms = 0;
    for (unsigned i = 0; i < TIRE_COUNT; ++i) {
        if (!state->wheels[i].seen || !charts[i] || !series[i]) continue;
        wheels[i].seen = true;
        wheels[i].lost = false;
        wheels[i].change_alert = false;
        wheels[i].pressure = state->wheels[i].pressure;
        wheels[i].last_seen = now;
        wheels[i].last_point = now;
        if (state->wheels[i].pressure < 150) {
            lv_chart_set_axis_min_value(charts[i], LV_CHART_AXIS_PRIMARY_Y, 0);
        }
        /* History starts fresh after reboot - only last received value is kept */
        for (unsigned j = 0; j < HISTORY_POINTS; ++j) {
            wheels[i].y_points[j] = LV_CHART_POINT_NONE;
            wheels[i].timestamps_ms[j] = 0;
        }
        sync_chart_series(i);
        update_chart_range(i);
        update_time_labels(i);
        lv_chart_refresh(charts[i]);
    }
    refresh_status(now);
}
