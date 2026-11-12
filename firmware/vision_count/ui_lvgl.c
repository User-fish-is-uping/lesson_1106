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

enum { SCREEN_WIDTH = 320, SCREEN_HEIGHT = 240 };
static lv_obj_t *camera_page, *panel_page, *camera;
static lv_obj_t *counts, *state, *direction, *details, *notice;
static lv_obj_t *camera_counts, *camera_state, *mute_label;
static lv_obj_t *reset_button, *reverse_button, *mute_button;
static lv_obj_t *camera_button, *panel_button;
static uint64_t last_tick, last_handler, clock_ms, frame_dt;
static obj_info_t latest;
static lv_color_t camera_pixels[SCREEN_WIDTH * SCREEN_HEIGHT];
static lv_style_t overlay_style;

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
    snprintf(text, sizeof(text), "IN:%" PRIu64 " OUT:%" PRIu64, s->in_count, s->out_count);
    label_text(counts, text);
    label_text(camera_counts, text);
    snprintf(text, sizeof(text), "AI:%s N:%u", counting_app_state_name(s->state),
             (unsigned)latest.obj_number);
    label_text(state, text);
    label_text(camera_state, text);
    label_text(direction, counting_app_config()->reverse_direction ?
               "ENTER: B -> A" : "ENTER: A -> B");
    float p = -1;
    if (latest.obj_number == 1) p = counting_app_config()->axis == GREETER_X ?
        (latest.obj[0].x1 + latest.obj[0].x2) / (2.f * 319.f) : latest.obj[0].y2 / 239.f;
    snprintf(text, sizeof(text), "Single-person crossing\nAudio: %s / %s\n"
             "P: %.1f%%  DT: %" PRIu64 " ms  E:%d  T:%" PRIu32,
             counting_app_muted() ? "MUTED" : "ON", audio_player_status(),
             (double)(p * 100), frame_dt, (int)s->last_error, touch_port_errors());
    label_text(details, text);
    label_text(mute_label, counting_app_muted() ? "Unmute" : "Mute");
}

static void show_camera(bool show)
{
    lv_obj_set_hidden(camera_page, !show);
    lv_obj_set_hidden(panel_page, show);
    lv_obj_invalidate(lv_scr_act());
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
    } else if (object == camera_button) {
        show_camera(true);
    } else if (object == panel_button) {
        show_camera(false);
    }
    refresh();
}

static lv_obj_t *label(lv_obj_t *parent, int x, int y, const char *text, bool overlay)
{
    lv_obj_t *obj = lv_label_create(parent, NULL);
    lv_obj_set_pos(obj, x, y);
    if (overlay) {
        lv_label_set_body_draw(obj, true);
        lv_label_set_style(obj, LV_LABEL_STYLE_MAIN, &overlay_style);
    }
    lv_label_set_text(obj, text);
    return obj;
}
static lv_obj_t *button(lv_obj_t *parent, int x, int y, int width,
                        const char *text, lv_obj_t **child)
{
    lv_obj_t *obj = lv_btn_create(parent, NULL);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, 38);
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
    lv_style_copy(&overlay_style, &lv_style_plain);
    overlay_style.body.main_color = LV_COLOR_BLACK;
    overlay_style.body.grad_color = LV_COLOR_BLACK;
    overlay_style.body.opa = LV_OPA_70;
    overlay_style.body.border.width = 0;
    overlay_style.body.padding.left = 2;
    overlay_style.body.padding.right = 2;
    overlay_style.body.padding.top = 1;
    overlay_style.body.padding.bottom = 1;
    overlay_style.text.color = LV_COLOR_WHITE;
    camera_page = lv_obj_create(lv_scr_act(), NULL);
    lv_obj_set_pos(camera_page, 0, 0);
    lv_obj_set_size(camera_page, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style(camera_page, &lv_style_plain);
    camera = lv_canvas_create(camera_page, NULL);
    lv_canvas_set_buffer(camera, camera_pixels, SCREEN_WIDTH, SCREEN_HEIGHT,
                         LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(camera, LV_COLOR_BLACK);
    lv_obj_set_pos(camera, 0, 0);
    camera_counts = label(camera_page, 2, 2, "", true);
    camera_state = label(camera_page, 126, 2, "", true);
    if (counting_app_config()->axis == GREETER_X) {
        label(camera_page, (int)(counting_app_config()->line_a * 319.f) + 3, 25, "A", true);
        label(camera_page, (int)(counting_app_config()->line_b * 319.f) + 3, 25, "B", true);
    } else {
        label(camera_page, 3, (int)(counting_app_config()->line_a * 239.f) + 3, "A", true);
        label(camera_page, 3, (int)(counting_app_config()->line_b * 239.f) + 3, "B", true);
    }
    panel_button = button(camera_page, 242, 198, 76, "Panel", NULL);

    panel_page = lv_obj_create(lv_scr_act(), NULL);
    lv_obj_set_pos(panel_page, 0, 0);
    lv_obj_set_size(panel_page, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style(panel_page, &lv_style_scr);
    label(panel_page, 8, 6, "K210 AI GREETER / LVGL", false);
    counts = label(panel_page, 8, 32, "", false);
    state = label(panel_page, 8, 58, "", false);
    direction = label(panel_page, 8, 82, "", false);
    details = label(panel_page, 8, 106, "", false);
    notice = label(panel_page, 8, 168, "Touch Camera for live view", false);
    reset_button = button(panel_page, 2, 198, 76, "Reset", NULL);
    reverse_button = button(panel_page, 82, 198, 76, "Reverse", NULL);
    mute_button = button(panel_page, 162, 198, 76, "Mute", &mute_label);
    camera_button = button(panel_page, 242, 198, 76, "Camera", NULL);
    show_camera(true);
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
static void pixel(unsigned x, unsigned y, lv_color_t color)
{
    if (x < SCREEN_WIDTH && y < SCREEN_HEIGHT)
        camera_pixels[y * SCREEN_WIDTH + x] = color;
}

static void vertical(unsigned x, lv_color_t color)
{
    if (x >= SCREEN_WIDTH) return;
    for (unsigned y = 0; y < SCREEN_HEIGHT; ++y) {
        pixel(x, y, color);
        if (x + 1 < SCREEN_WIDTH) pixel(x + 1, y, color);
    }
}

static void horizontal(unsigned y, lv_color_t color)
{
    if (y >= SCREEN_HEIGHT) return;
    for (unsigned x = 0; x < SCREEN_WIDTH; ++x) {
        pixel(x, y, color);
        if (y + 1 < SCREEN_HEIGHT) pixel(x, y + 1, color);
    }
}

static void rectangle(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, lv_color_t color)
{
    if (x1 >= SCREEN_WIDTH || y1 >= SCREEN_HEIGHT) return;
    if (x2 >= SCREEN_WIDTH) x2 = SCREEN_WIDTH - 1;
    if (y2 >= SCREEN_HEIGHT) y2 = SCREEN_HEIGHT - 1;
    if (x1 > x2 || y1 > y2) return;
    for (uint32_t x = x1; x <= x2; ++x) {
        pixel(x, y1, color);
        pixel(x, y2, color);
    }
    for (uint32_t y = y1; y <= y2; ++y) {
        pixel(x1, y, color);
        pixel(x2, y, color);
    }
}

void ui_lvgl_update(const obj_info_t *people, uint64_t dt_ms,
                    const uint16_t *camera_rgb565)
{
    latest = *people;
    frame_dt = dt_ms;
    if (camera_rgb565)
        memcpy(camera_pixels, camera_rgb565, sizeof(camera_pixels));
    if (counting_app_config()->axis == GREETER_X) {
        vertical((unsigned)(counting_app_config()->line_a * 319.f), LV_COLOR_YELLOW);
        vertical((unsigned)(counting_app_config()->line_b * 319.f), LV_COLOR_LIME);
    } else {
        horizontal((unsigned)(counting_app_config()->line_a * 239.f), LV_COLOR_YELLOW);
        horizontal((unsigned)(counting_app_config()->line_b * 239.f), LV_COLOR_LIME);
    }
    for (uint32_t i = 0; i < latest.obj_number && i < 10; ++i)
        rectangle(latest.obj[i].x1, latest.obj[i].y1, latest.obj[i].x2,
                  latest.obj[i].y2, LV_COLOR_CYAN);
    lv_obj_invalidate(camera);
    label_text(notice, latest.obj_number > 1 || latest.truncated ?
               "Multiple people: counting paused" : "Live view: boxes and A/B gates");
    refresh();
}

