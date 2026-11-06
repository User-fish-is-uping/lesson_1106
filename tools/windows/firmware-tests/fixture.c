/* Synthetic pixels exercise the actual board adapter, not a duplicate state machine. */
#include "counting_app.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static obj_info_t objects;
static uint64_t seq;
static void observe(unsigned x, uint64_t at)
{
    memset(&objects, 0, sizeof(objects));
    objects.obj_number = 1;
    objects.obj[0].class_id = 14;
    objects.obj[0].x1 = x - 20;
    objects.obj[0].x2 = x + 20;
    objects.obj[0].y1 = 50;
    objects.obj[0].y2 = 200;
    /* Five decimals would round this to .5 and silently lose the box on replay. */
    objects.obj[0].prob = nextafterf(.5f, 1.f);
    ++seq;
    counting_app_process(&objects, seq, at, at + 20, seq == 1 ? 0 : 150);
}

int main(int argc, char **argv)
{
    if (argc != 2 || !counting_app_init()) return 1;
    puts("{\"kind\":\"fixture\",\"source\":\"synthetic_pixels_not_hardware\"}");
    counting_app_log_config();
    unsigned expected = 0;
    if (!strcmp(argv[1], "roundtrip")) {
        const unsigned points[] = {64,64,64,160,255,255,255,160,64,64,64};
        for (unsigned i = 0; i < sizeof(points)/sizeof(points[0]); ++i) observe(points[i], i * 150);
        expected = 1;
    } else if (!strcmp(argv[1], "dwell")) {
        for (unsigned i = 0; i < 20; ++i) observe(64 + i % 3, i * 150);
    } else if (!strcmp(argv[1], "foldback")) {
        const unsigned points[] = {64,64,64,160,64,64,64};
        for (unsigned i = 0; i < sizeof(points)/sizeof(points[0]); ++i) observe(points[i], i * 150);
    } else if (!strcmp(argv[1], "invalid")) {
        observe(64, 0);
        objects.obj[0].prob = NAN;
        counting_app_process(&objects, ++seq, 150, 170, 150);
        if (counting_app_snapshot()->last_error != GREETER_BAD_INPUT) return 2;
        counting_app_process(NULL, ++seq, 300, 320, 150);
        if (counting_app_snapshot()->last_error != GREETER_BAD_INPUT) return 3;
    } else if (!strcmp(argv[1], "capacity")) {
        observe(64, 0);
        objects.obj_number = 11; /* Reject before accessing the fixed 10-element array. */
        counting_app_process(&objects, ++seq, 150, 170, 150);
        if (counting_app_snapshot()->last_error != GREETER_BAD_INPUT) return 4;
        objects.obj_number = 10;
        for (unsigned i = 1; i < 10; ++i) objects.obj[i] = objects.obj[0];
        counting_app_process(&objects, ++seq, 300, 320, 150);
        if (counting_app_snapshot()->state != GREETER_AMBIGUOUS) return 5;
    } else return 6;
    const greeter_snapshot_t *s = counting_app_snapshot();
    return s->in_count == expected && s->out_count == expected ? 0 : 7;
}
