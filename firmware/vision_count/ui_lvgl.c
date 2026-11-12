#include "ui_lvgl.h"
#include "counting_app.h"
#include "audio_player.h"
#include "touch_port.h"
#include "lvgl/lvgl.h"
#include "lcd.h"
#include "st7789.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static lv_obj_t *counts, *state, *direction, *details, *notice, *page_label, *mute_label;
static lv_obj_t *reset_button, *reverse_button, *mute_button, *page_button;
static uint64_t last_tick, last_handler, clock_ms, frame_dt;
static obj_info_t latest;
static bool debug_page;

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels)
{
    lcd_set_area(area->x1, area->y1, area->x2, area->y2);
    tft_write_half((uint16_t *)pixels,
        (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1));
    lv_disp_flush_ready(driver);
}

static bool read_touch(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    (void)driver;
    static uint16_t x, y;
    bool down = touch_port_read(&x, &y);
    data->point.x = x;
    data->point.y = y;
    data->state = down ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    return false;
}

static void label_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}

static void refresh(void)
{
    char text[180];
    const greeter_snapshot_t *s = counting_app_snapshot();
    snprintf(text, sizeof(text), "IN: %" PRIu64 "    OUT: %" PRIu64, s->in_count, s->out_count);
    label_text(counts, text);
    snprintf(text, sizeof(text), "AI: %s   N:%u", counting_app_state_name(s->state),
             (unsigned)latest.obj_number);
    label_text(state, text);
    label_text(direction, counting_app_config()->reverse_direction ?
               "ENTER: B -> A" : "ENTER: A -> B");
    if (debug_page) {
        float p = -1;
        if (latest.obj_number == 1) p = counting_app_config()->axis == GREETER_X ?
            (latest.obj[0].x1 + latest.obj[0].x2) / (2.f * 319.f) : latest.obj[0].y2 / 239.f;
        snprintf(text, sizeof(text), "P: %.1f%%   DT: %" PRIu64 " ms\nError: %d   Touch errors: %" PRIu32,
                 (double)(p * 100), frame_dt, (int)s->last_error, touch_port_errors());
    } else {
        snprintf(text, sizeof(text), "Single-person crossing\nAudio: %s / %s", counting_app_muted() ? "MUTED" : "ON", audio_player_status());
    }
    label_text(details, text);
    label_text(mute_label, counting_app_muted() ? "Unmute" : "Mute");
    label_text(page_label, debug_page ? "Home" : "Debug");
}

static void on_click(lv_obj_t *object, lv_event_t event)
{
    if (event != LV_EVENT_CLICKED) return;
    if (object == reset_button) {
        counting_app_reset_counts();
        printf("{\"kind\":\"control\",\"t_ms\":%" PRIu64 ",\"action\":\"reset\"}\n", clock_ms);
        label_text(notice, "Counts and pending passage cleared");
    } else if (object == reverse_button) {
        bool reverse = !counting_app_config()->reverse_direction;
        counting_app_set_reverse(reverse);
        printf("{\"kind\":\"control\",\"t_ms\":%" PRIu64 ",\"action\":\"reverse\",\"value\":%s}\n",
               clock_ms, reverse ? "true" : "false");
        label_text(notice, "Direction changed; counts retained");
    } else if (object == mute_button) {
        counting_app_set_muted(!counting_app_muted());
        printf("{\"kind\":\"control\",\"t_ms\":%" PRIu64 ",\"action\":\"mute\",\"value\":%s}\n", clock_ms, counting_app_muted() ? "true" : "false");
        label_text(notice, counting_app_muted() ? "Muted; current clip may finish" : "Audio on; no old events replayed");
    } else if (object == page_button) debug_page = !debug_page;
    refresh();
}

static lv_obj_t *label(int x, int y, const char *text)
{
    lv_obj_t *obj = lv_label_create(lv_scr_act(), NULL);
    lv_obj_set_pos(obj, x, y);
    lv_label_set_text(obj, text);
    return obj;
}
static lv_obj_t *button(int x, const char *text, lv_obj_t **child)
{
    lv_obj_t *obj = lv_btn_create(lv_scr_act(), NULL);
    lv_obj_set_pos(obj, x, 190);
    lv_obj_set_size(obj, 74, 42);
    lv_obj_set_event_cb(obj, on_click);
    lv_obj_t *caption = lv_label_create(obj, NULL);
    lv_label_set_text(caption, text);
    if (child) *child = caption;
    return obj;
}
void ui_lvgl_init(uint64_t now_ms)
{
    lv_init();
    touch_port_init();
    static lv_disp_buf_t buffer;
    static lv_color_t pixels[320 * 10];
    lv_disp_buf_init(&buffer, pixels, NULL, 320 * 10);
    lv_disp_drv_t display;
    lv_disp_drv_init(&display);
    display.hor_res = 320;
    display.ver_res = 240;
    display.buffer = &buffer;
    display.flush_cb = flush;
    lv_disp_drv_register(&display);
    lv_indev_drv_t input;
    lv_indev_drv_init(&input);
    input.type = LV_INDEV_TYPE_POINTER;
    input.read_cb = read_touch;
    lv_indev_drv_register(&input);
    label(8, 6, "K210 AI GREETER / LVGL");
    counts = label(8, 32, "");
    state = label(8, 58, "");
    direction = label(8, 82, "");
    details = label(8, 108, "");
    notice = label(8, 160, "Touch a button below");
    reset_button = button(4, "Reset", NULL);
    reverse_button = button(83, "Reverse", NULL);
    mute_button = button(162, "Mute", &mute_label);
    page_button = button(241, "Debug", &page_label);
    last_tick = last_handler = clock_ms = now_ms;
    refresh();
}
void ui_lvgl_service(uint64_t now_ms)
{
    clock_ms = now_ms;
    lv_tick_inc((uint32_t)(now_ms - last_tick));
    last_tick = now_ms;
    if (now_ms - last_handler >= 5) {
        last_handler = now_ms;
        lv_task_handler();
    }
}
void ui_lvgl_update(const obj_info_t *people, uint64_t dt_ms)
{
    latest = *people;
    frame_dt = dt_ms;
    refresh();
}

