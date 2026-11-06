/* Derived from Yahboom object_detection/main.c, Gengyue, 2020-05-27.
 * Copyright (C) 2016-2022 Shenzhen Yahboom Tech.
 * Project adaptation: person-only diagnostic, JSONL frames, bounded waits.
 * Optional counting calls the shared core; no second counting algorithm here.
 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "bsp.h"
#include "encoding.h"
#include "sysctl.h"
#include "uarths.h"
#include "plic.h"
#include "lcd.h"
#include "pin_config.h"
#include "dvp_cam.h"
#include "ov2640.h"
#include "ov9655.h"
#include "gc2145.h"
#include "region_layer.h"
#include "kpu.h"
#include "build_info.h"
#ifdef GREETER_ENABLE_COUNTING
#include "counting_app.h"
static bool counting_ready;
#endif
#define INCBIN_STYLE INCBIN_STYLE_SNAKE
#define INCBIN_PREFIX
#include "incbin.h"
INCBIN(model, "detect.kmodel");

static volatile uint32_t ai_done_flag;
static uint64_t boot_cycles, cpu_hz, sequence_id;
static kpu_model_context_t model_context;
static region_layer_t region;
static obj_info_t people;
/* Batch each text row into one LCD transfer instead of per-pixel SPI/DMA setup. */
static uint32_t text_row[320 * 16 / 2];
static void draw_text_row(uint16_t y, char *text, uint16_t color)
{
    size_t length = strlen(text);
    if (!length || length > 40) return;
    lcd_ram_draw_string(text, text_row, color, BLACK);
    lcd_draw_picture(0, y, (uint16_t)(length * 8), 16, text_row);
}
static float anchors[10] = {1.3221f,1.73145f,3.19275f,4.00944f,
    5.05587f,8.09892f,9.47112f,4.84053f,11.2364f,10.0071f};

static uint64_t now_ms(void)
{
    uint64_t cycles = read_cycle() - boot_cycles;
    return (cycles / cpu_hz) * 1000 + ((cycles % cpu_hz) * 1000) / cpu_hz;
}

static void on_ai_done(void *context)
{
    (void)context;
    ai_done_flag = 1;
}

static void fatal(const char *reason)
{
    printf("{\"kind\":\"error\",\"reason\":\"%s\",\"fatal\":true}\n", reason);
#ifdef GREETER_ENABLE_COUNTING
    if (counting_ready) {
        uint64_t failed_ms = now_ms();
        counting_app_process(NULL, ++sequence_id, failed_ms, failed_ms, 0);
    } else
#endif
    printf("{\"kind\":\"frame\",\"seq\":%" PRIu64 ",\"t_ms\":%" PRIu64
        ",\"valid\":false,\"truncated\":false,\"persons\":[]}\n", ++sequence_id, now_ms());
    lcd_clear(BLACK);
    lcd_draw_string(8, 40, "VISION ERROR - check serial", RED);
    lcd_draw_string(8, 64, (char *)reason, WHITE);
    /* A timed-out DMA must not be restarted while the old transfer is live. */
    while (1) { }
}

static void setup_pins(void)
{
    sysctl_set_power_mode(SYSCTL_POWER_BANK6, SYSCTL_POWER_V18);
    sysctl_set_power_mode(SYSCTL_POWER_BANK7, SYSCTL_POWER_V18);
    fpioa_set_function(PIN_LCD_CS, FUNC_LCD_CS);
    fpioa_set_function(PIN_LCD_RST, FUNC_LCD_RST);
    fpioa_set_function(PIN_LCD_RS, FUNC_LCD_RS);
    fpioa_set_function(PIN_LCD_WR, FUNC_LCD_WR);
    fpioa_set_function(PIN_DVP_RST, FUNC_CMOS_RST);
    fpioa_set_function(PIN_DVP_PWDN, FUNC_CMOS_PWDN);
    fpioa_set_function(PIN_DVP_XCLK, FUNC_CMOS_XCLK);
    fpioa_set_function(PIN_DVP_VSYNC, FUNC_CMOS_VSYNC);
    fpioa_set_function(PIN_DVP_HSYNC, FUNC_CMOS_HREF);
    fpioa_set_function(PIN_DVP_PCLK, FUNC_CMOS_PCLK);
    fpioa_set_function(PIN_DVP_SCL, FUNC_SCCB_SCLK);
    fpioa_set_function(PIN_DVP_SDA, FUNC_SCCB_SDA);
    sysctl_set_spi0_dvp_data(1);
}

#ifndef GREETER_ENABLE_COUNTING
static void log_frame(uint64_t timestamp, uint64_t elapsed)
{
    printf("{\"kind\":\"frame\",\"seq\":%" PRIu64 ",\"t_ms\":%" PRIu64
        ",\"valid\":true,\"truncated\":%s,\"persons\":[",
        ++sequence_id, timestamp, people.truncated ? "true" : "false");
    for (uint32_t i = 0; i < people.obj_number; ++i) {
        printf("%s{\"x1\":%.9g,\"y1\":%.9g,\"x2\":%.9g,\"y2\":%.9g,\"score\":%.9g}",
            i ? "," : "", people.obj[i].x1 / 319.0f, people.obj[i].y1 / 239.0f,
            people.obj[i].x2 / 319.0f, people.obj[i].y2 / 239.0f, people.obj[i].prob);
    }
    printf("],\"dt_ms\":%" PRIu64 "}\n", elapsed);
}
#endif

int main(void)
{
    sysctl_pll_set_freq(SYSCTL_PLL0, 800000000UL);
    sysctl_pll_set_freq(SYSCTL_PLL1, 400000000UL);
    sysctl_clock_enable(SYSCTL_CLOCK_AI);
    cpu_hz = sysctl_clock_get_freq(SYSCTL_CLOCK_CPU);
    boot_cycles = read_cycle();
    uarths_init();
    setup_pins();
    plic_init();
    lcd_init();
    lcd_clear(BLACK);
    lcd_draw_string(8, 40, FIRMWARE_VERSION, WHITE);
    printf("{\"kind\":\"boot\",\"firmware\":\"%s\",\"source_commit\":\"%s\","
        "\"source_dirty\":%s,\"core_commit\":\"%s\",\"baud\":115200,"
        "\"person_class\":14,\"width\":320,\"height\":240,\"threshold\":0.5,"
        "\"cpu_hz\":%" PRIu64 "}\n", FIRMWARE_VERSION, FIRMWARE_SOURCE_COMMIT,
        FIRMWARE_SOURCE_DIRTY ? "true" : "false", FIRMWARE_CORE_COMMIT, cpu_hz);
#ifdef GREETER_ENABLE_COUNTING
    if (!counting_app_init()) fatal("counter_init");
    counting_ready = true;
    counting_app_log_config();
#endif
    int camera = OVxxxx_read_id();
    if (!display_buf_addr || !g_ai_buf_in || !g_ai_od_buf) fatal("camera_allocation");
    if (camera == OV_9655) {
        ov9655_init();
        printf("{\"kind\":\"camera\",\"model\":\"OV9655\"}\n");
    } else if (camera == OV_2640) {
        ov2640_init();
        printf("{\"kind\":\"camera\",\"model\":\"OV2640\"}\n");
    } else {
        uint16_t id = 0;
        gc2145_read_id(&id);
        if (id != GC2145_ID) fatal("camera_id");
        gc2145_init();
        printf("{\"kind\":\"camera\",\"model\":\"GC2145\"}\n");
    }
    if (kpu_load_kmodel(&model_context, model_data)) fatal("model_load");
    region.anchor_number = 5;
    region.anchor = anchors;
    region.threshold = 0.5f;
#ifdef GREETER_ENABLE_COUNTING
    region.threshold = counting_app_config()->confidence_threshold;
#endif
    region.nms_value = 0.2f;
    if (region_layer_init(&region, 10, 8, 125, 320, 240)) fatal("region_allocation");
    printf("{\"kind\":\"model\",\"status\":\"ready\"}\n");
    sysctl_enable_irq();
    uint64_t previous_frame = now_ms();
    while (1) {
        uint64_t deadline_start = now_ms();
        g_dvp_finish_flag = 0;
        while (!g_dvp_finish_flag) {
            if (now_ms() - deadline_start > 2000) fatal("camera_timeout");
        }
        /* Capture finish is the observation time, not the later log output time. */
        uint64_t observed_ms = now_ms();
        memset(g_ai_od_buf, 127, 320 * 256 * 3);
        for (uint32_t channel = 0; channel < 3; ++channel)
            memcpy(g_ai_od_buf + 320 * (channel * 256 + 8),
                g_ai_buf_in + channel * 320 * 240, 320 * 240);
        ai_done_flag = 0;
        if (kpu_run_kmodel(&model_context, g_ai_od_buf, DMAC_CHANNEL5, on_ai_done, NULL))
            fatal("kpu_start");
        deadline_start = now_ms();
        while (!ai_done_flag) {
            if (now_ms() - deadline_start > 2000) fatal("kpu_timeout");
        }
        float *output = NULL;
        size_t output_size = 0;
        if (kpu_get_output(&model_context, 0, (uint8_t **)&output, &output_size)
            || !output || output_size < region.output_number * sizeof(float)) fatal("kpu_output");
        region.input = output;
        region_layer_run(&region, &people);
#ifdef GREETER_ENABLE_COUNTING
        counting_app_process(&people, ++sequence_id, observed_ms, now_ms(),
                             observed_ms - previous_frame);
#endif
        lcd_draw_picture(0, 0, 320, 240, (uint32_t *)(uintptr_t)display_buf_addr);
        for (uint32_t i = 0; i < people.obj_number; ++i)
            lcd_draw_rectangle(people.obj[i].x1, people.obj[i].y1,
                people.obj[i].x2, people.obj[i].y2, 1, CYAN);
        char status[40];
#ifdef GREETER_ENABLE_COUNTING
        const greeter_config_t *cfg = counting_app_config();
        const greeter_snapshot_t *current = counting_app_snapshot();
        uint16_t extent = cfg->axis == GREETER_X ? 319 : 239;
        uint16_t line_a = (uint16_t)(cfg->line_a * extent + .5f);
        uint16_t line_b = (uint16_t)(cfg->line_b * extent + .5f);
        if (cfg->axis == GREETER_X) {
            lcd_draw_rectangle(line_a, 64, line_a + 1, 238, 1, YELLOW);
            lcd_draw_rectangle(line_b, 64, line_b + 1, 238, 1, GREEN);
        } else {
            lcd_draw_rectangle(0, line_a, 318, line_a + 1, 1, YELLOW);
            lcd_draw_rectangle(0, line_b, 318, line_b + 1, 1, GREEN);
        }
        snprintf(status, sizeof(status), "IN:%" PRIu64 "  MUTED", current->in_count);
        draw_text_row(0, status, WHITE);
        snprintf(status, sizeof(status), "OUT:%" PRIu64, current->out_count);
        draw_text_row(16, status, WHITE);
        snprintf(status, sizeof(status), "%s N:%u E:%d", counting_app_state_name(current->state),
            (unsigned)people.obj_number, (int)current->last_error);
        draw_text_row(32, status, WHITE);
        uint64_t dt_ms = observed_ms - previous_frame;
        if (people.obj_number == 1) {
            float position = cfg->axis == GREETER_X
                ? (people.obj[0].x1 / 319.0f + people.obj[0].x2 / 319.0f) * .5f
                : people.obj[0].y2 / 239.0f;
            snprintf(status, sizeof(status), "P:%.1f%% DT:%" PRIu64 "ms",
                (double)(position * 100.f), dt_ms);
        } else {
            snprintf(status, sizeof(status), "P:-- DT:%" PRIu64 "ms", dt_ms);
        }
        draw_text_row(48, status, dt_ms > cfg->lost_grace_ms ? RED : YELLOW);
#else
        lcd_draw_rectangle(112, 20, 113, 238, 1, YELLOW);
        lcd_draw_rectangle(207, 20, 208, 238, 1, GREEN);
        snprintf(status, sizeof(status), "PERSON:%u%s  VISION ONLY", (unsigned)people.obj_number,
            people.truncated ? "+" : "");
        draw_text_row(0, status, WHITE);
        log_frame(observed_ms, observed_ms - previous_frame);
#endif
        previous_frame = observed_ms;
    }
}
