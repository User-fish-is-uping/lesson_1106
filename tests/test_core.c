#include "audio_queue.h"
#include "greeter.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
static greeter_t g;
static greeter_event_t e;
static uint64_t seq;
static bool fired;
static greeter_frame_t frame(uint64_t t, float p, unsigned n) {
    greeter_frame_t f;
    unsigned i;
    memset(&f, 0, sizeof(f));
    f.now_ms = t;
    f.sequence_id = ++seq;
    f.valid = true;
    f.person_count = n;
    for (i = 0; i < n && i < 10; i++) {
        f.persons[i].x1 = p - .04f;
        f.persons[i].x2 = p + .04f;
        f.persons[i].y1 = .1f;
        f.persons[i].y2 = .8f;
        f.persons[i].score = .9f;
    }
    return f;
}
static void init(void) {
    greeter_config_t c = greeter_default_config();
    seq = 0;
    fired = false;
    CHECK(greeter_init(&g, &c) == GREETER_OK);
}
static void at(uint64_t t, float p, unsigned n) {
    greeter_frame_t f = frame(t, p, n);
    fired = greeter_update(&g, &f, &e);
}
static void origin(float a) {
    at(0, a, 1);
    CHECK(!fired);
    at(150, a, 1);
    CHECK(!fired);
}
static void cross(uint64_t t, float dest) {
    at(t, .5f, 1);
    CHECK(!fired);
    at(t + 150, dest, 1);
    CHECK(!fired);
    at(t + 300, dest, 1);
    CHECK(fired);
}
static void normal(bool reverse) {
    float a = reverse ? .8f : .2f, b = reverse ? .2f : .8f;
    init();
    origin(a);
    cross(300, b);
    CHECK(e.event_id == 1 && e.timestamp_ms == 600 &&
          e.type == (reverse ? GREETER_EXIT : GREETER_ENTER));
    at(750, b, 1);
    CHECK(!fired);
    cross(900, a);
    CHECK(e.event_id == 2 && g.snapshot.in_count == 1 && g.snapshot.out_count == 1);
}
static greeter_event_t event(uint64_t id, uint64_t t) {
    greeter_event_t v = {id, t, id % 2 ? GREETER_ENTER : GREETER_EXIT};
    return v;
}
static void audio(const char *name) {
    audio_queue_t q;
    audio_item_t item;
    greeter_event_t v;
    unsigned i;
    CHECK(audio_queue_init(&q, 5000));
    if (!strcmp(name, "audio_clock")) {
        v = event(1, 100);
        CHECK(audio_queue_push(&q, &v, 100) == AUDIO_OK);
        CHECK(audio_queue_report(&q, 1, AUDIO_FAILED, 99) == AUDIO_TIME_BACKWARD);
        CHECK(q.count == 1 && q.items[0].attempts == 0 && q.last_now == 100);
        v = event(2, 101);
        CHECK(audio_queue_push(&q, &v, 100) == AUDIO_BAD_INPUT);
        CHECK(q.watermark == 1 && q.stats.invalid == 1);
        v.timestamp_ms = 100;
        CHECK(audio_queue_push(&q, &v, 100) == AUDIO_OK);
        CHECK(audio_queue_report(&q, 1, AUDIO_STARTED, 100) == AUDIO_OK);
    } else if (!strcmp(name, "audio_fifo")) {
        for (i = 1; i <= 4; i++) {
            v = event(i, 0);
            CHECK(audio_queue_push(&q, &v, 0) == AUDIO_OK);
        }
        v = event(5, 0);
        CHECK(audio_queue_push(&q, &v, 0) == AUDIO_FULL);
        CHECK(q.stats.full == 1);
        for (i = 1; i <= 4; i++) {
            CHECK(audio_queue_peek(&q, i, &item) == AUDIO_OK && item.event.event_id == i);
            CHECK(audio_queue_report(&q, i, AUDIO_STARTED, i) == AUDIO_OK);
        }
        CHECK(q.count == 0 && q.stats.started == 4);
        CHECK(audio_queue_push(&q, &v, 4) == AUDIO_DUPLICATE);
    } else if (!strcmp(name, "audio_retry")) {
        v = event(1, 0);
        CHECK(audio_queue_push(&q, &v, 0) == AUDIO_OK);
        CHECK(audio_queue_report(&q, 1, AUDIO_BUSY, 1) == AUDIO_OK && q.items[0].attempts == 0);
        CHECK(audio_queue_report(&q, 1, AUDIO_FAILED, 2) == AUDIO_OK && q.count == 1);
        CHECK(audio_queue_report(&q, 1, AUDIO_FAILED, 3) == AUDIO_OK && q.count == 0 &&
              q.stats.failed_dropped == 1);
        v = event(2, 3);
        CHECK(audio_queue_push(&q, &v, 3) == AUDIO_OK);
        CHECK(audio_queue_report(&q, 2, AUDIO_STARTED, 4) == AUDIO_OK);
        audio_queue_report_async_failure(&q);
        CHECK(q.count == 0 && q.stats.async_failures == 1);
    } else if (!strcmp(name, "audio_expiry")) {
        v = event(1, 0);
        CHECK(audio_queue_push(&q, &v, 0) == AUDIO_OK);
        CHECK(audio_queue_peek(&q, 4999, &item) == AUDIO_OK);
        CHECK(audio_queue_peek(&q, 5000, &item) == AUDIO_EMPTY && q.stats.expired == 1);
        v = event(2, 0);
        CHECK(audio_queue_push(&q, &v, 5000) == AUDIO_EMPTY);
        CHECK(q.stats.expired == 2);
    } else if (!strcmp(name, "audio_mute")) {
        v = event(1, 0);
        CHECK(audio_queue_push(&q, &v, 0) == AUDIO_OK);
        audio_queue_set_muted(&q, true);
        CHECK(!q.count && q.stats.cleared == 1);
        v = event(2, 1);
        CHECK(audio_queue_push(&q, &v, 1) == AUDIO_MUTED);
        audio_queue_set_muted(&q, false);
        CHECK(audio_queue_push(&q, &v, 1) == AUDIO_DUPLICATE);
        CHECK(q.watermark == 2);
    } else if (!strcmp(name, "audio_simulation")) {
        uint64_t busy_until = 0, t, id = 0;
        for (t = 0; t <= 12000; t += 100) {
            if (t % 600 == 0) {
                v = event(++id, t);
                (void)audio_queue_push(&q, &v, t);
            }
            if (t >= busy_until && audio_queue_peek(&q, t, &item) == AUDIO_OK) {
                CHECK(audio_queue_report(&q, item.event.event_id, AUDIO_STARTED, t) == AUDIO_OK);
                busy_until = t + 1800;
            }
        }
        CHECK(q.stats.started == 7 && q.stats.full > 0 && q.stats.expired > 0);
    } else if (!strcmp(name, "audio_busy_fifo")) {
        for (i = 1; i <= 3; i++) {
            v = event(i, 0);
            CHECK(audio_queue_push(&q, &v, 0) == AUDIO_OK);
        }
        for (i = 1; i <= 8; i++) {
            CHECK(audio_queue_peek(&q, i, &item) == AUDIO_OK && item.event.event_id == 1);
            CHECK(audio_queue_report(&q, 1, AUDIO_BUSY, i) == AUDIO_OK);
            CHECK(q.items[0].attempts == 0 && q.count == 3);
        }
        CHECK(audio_queue_report(&q, 1, AUDIO_STARTED, 9) == AUDIO_OK);
        CHECK(audio_queue_peek(&q, 9, &item) == AUDIO_OK && item.event.event_id == 2);
        CHECK(q.stats.started == 1 && q.stats.start_failures == 0);
    } else if (!strcmp(name, "audio_fail_chain")) {
        v = event(1, 0);
        CHECK(audio_queue_push(&q, &v, 0) == AUDIO_OK);
        v = event(2, 0);
        CHECK(audio_queue_push(&q, &v, 0) == AUDIO_OK);
        CHECK(audio_queue_report(&q, 1, AUDIO_FAILED, 1) == AUDIO_OK);
        CHECK(q.count == 2 && q.items[0].attempts == 1);
        CHECK(audio_queue_report(&q, 1, AUDIO_FAILED, 2) == AUDIO_OK);
        CHECK(q.count == 1 && q.items[0].event.event_id == 2);
        CHECK(audio_queue_report(&q, 2, AUDIO_FAILED, 3) == AUDIO_OK);
        CHECK(audio_queue_report(&q, 2, AUDIO_STARTED, 4) == AUDIO_OK);
        CHECK(!q.count && q.stats.start_failures == 3 && q.stats.failed_dropped == 1 &&
              q.stats.started == 1);
    } else if (!strcmp(name, "audio_controls")) {
        v = event(1, 0);
        CHECK(audio_queue_push(&q, &v, 0) == AUDIO_OK);
        audio_queue_report_async_failure(&q);
        audio_queue_clear(&q);
        CHECK(!q.count && q.watermark == 1 && q.stats.cleared == 1 &&
              q.stats.async_failures == 1);
        CHECK(audio_queue_push(&q, &v, 1) == AUDIO_DUPLICATE);
        v = event(2, 1);
        CHECK(audio_queue_push(&q, &v, 1) == AUDIO_OK);
        audio_queue_set_muted(&q, true);
        CHECK(!q.count && q.stats.cleared == 2);
        v = event(3, 2);
        CHECK(audio_queue_push(&q, &v, 2) == AUDIO_MUTED);
        audio_queue_set_muted(&q, false);
        CHECK(audio_queue_push(&q, &v, 2) == AUDIO_DUPLICATE);
        v = event(4, 2);
        CHECK(audio_queue_push(&q, &v, 2) == AUDIO_OK);
        CHECK(q.count == 1 && q.watermark == 4 && q.stats.muted == 1 &&
              q.stats.async_failures == 1);
    } else if (!strcmp(name, "audio_boundaries")) {
        v = event(1, 100);
        CHECK(audio_queue_push(&q, &v, 99) == AUDIO_BAD_INPUT);
        CHECK(q.watermark == 0 && q.stats.invalid == 1);
        v.timestamp_ms = 0;
        CHECK(audio_queue_push(&q, &v, 4999) == AUDIO_OK);
        CHECK(audio_queue_peek(&q, 4998, &item) == AUDIO_TIME_BACKWARD);
        CHECK(q.count == 1 && q.items[0].attempts == 0 && q.last_now == 4999);
        CHECK(audio_queue_peek(&q, 5000, &item) == AUDIO_EMPTY);
        CHECK(q.stats.expired == 1 && q.watermark == 1);
    } else {
        CHECK(!audio_queue_init(NULL, 5));
        CHECK(!audio_queue_init(&q, 0));
        CHECK(audio_queue_init(&q, 5000));
        v = event(1, 10);
        CHECK(audio_queue_push(&q, &v, 0) == AUDIO_BAD_INPUT);
        v.timestamp_ms = 0;
        CHECK(audio_queue_push(&q, &v, 1) == AUDIO_OK);
        CHECK(audio_queue_peek(&q, 0, &item) == AUDIO_TIME_BACKWARD);
        CHECK(audio_queue_report(&q, 2, AUDIO_STARTED, 1) == AUDIO_ID_MISMATCH);
        CHECK(audio_queue_report(&q, 1, (audio_start_result_t)99, 1) == AUDIO_BAD_INPUT);
        CHECK(!strcmp(audio_queue_path(GREETER_EXIT), "0:/goodbye.wav"));
        CHECK(audio_queue_path((greeter_event_type_t)0) == NULL);
    }
}
int main(int argc, char **argv) {
    const char *name;
    greeter_frame_t f;
    greeter_config_t c;
    unsigned dir;
    CHECK(argc == 2);
    name = argv[1];
    if (!strncmp(name, "audio_", 6)) {
        audio(name);
        puts(name);
        return 0;
    }
    if (!strcmp(name, "normal")) {
        normal(false);
    } else if (!strcmp(name, "reverse")) {
        normal(true);
        init();
        greeter_set_reverse(&g, true);
        origin(.2f);
        cross(300, .8f);
        CHECK(e.type == GREETER_EXIT);
    } else if (!strcmp(name, "midpoint")) {
        init();
        at(0, .5f, 1);
        at(150, .5f, 1);
        at(300, .8f, 1);
        at(450, .8f, 1);
        CHECK(!fired && g.snapshot.last_event_id == 0);
        cross(600, .2f);
        CHECK(e.type == GREETER_EXIT);
    } else if (!strcmp(name, "config")) {
        c = greeter_default_config();
        CHECK(greeter_validate_config(&c) == GREETER_OK);
        c.max_speed_per_s = 0;
        CHECK(greeter_validate_config(&c) == GREETER_OK);
        c.max_speed_per_s = -1;
        CHECK(greeter_validate_config(&c) == GREETER_BAD_CONFIG);
        c = greeter_default_config();
        c.line_a = NAN;
        CHECK(greeter_validate_config(&c) == GREETER_BAD_CONFIG);
        c = greeter_default_config();
        c.hysteresis = .2f;
        CHECK(greeter_validate_config(&c) == GREETER_BAD_CONFIG);
        c = greeter_default_config();
        c.traversal_timeout_ms = 100;
        CHECK(greeter_validate_config(&c) == GREETER_BAD_CONFIG);
        CHECK(greeter_init(NULL, &c) == GREETER_BAD_INPUT);
    } else if (!strcmp(name, "axis")) {
        init();
        g.config.axis = GREETER_Y;
        f = frame(0, .5f, 1);
        f.persons[0].y2 = .2f;
        CHECK(!greeter_update(&g, &f, &e));
        f.sequence_id = ++seq;
        f.now_ms = 150;
        CHECK(!greeter_update(&g, &f, &e));
        f.sequence_id = ++seq;
        f.now_ms = 300;
        f.persons[0].y2 = .5f;
        CHECK(!greeter_update(&g, &f, &e));
        f.sequence_id = ++seq;
        f.now_ms = 450;
        f.persons[0].y2 = .8f;
        CHECK(!greeter_update(&g, &f, &e));
        f.sequence_id = ++seq;
        f.now_ms = 600;
        CHECK(greeter_update(&g, &f, &e) && e.type == GREETER_ENTER);
    } else
        for (dir = 0; dir < 2; dir++) {
            float a = dir ? .8f : .2f, b = dir ? .2f : .8f;
            init();
            origin(a);
            if (!strcmp(name, "direct_jump")) {
                g.config.max_speed_per_s = 10.0f;
                at(300, b, 1);
                at(450, b, 1);
                CHECK(!fired && g.snapshot.last_event_id == 0);
                cross(600, a);
            } else if (!strcmp(name, "foldback")) {
                at(300, .5f, 1);
                at(450, a, 1);
                at(600, a, 1);
                CHECK(!fired && !g.middle);
                at(750, b, 1);
                at(900, b, 1);
                CHECK(!fired);
            } else if (!strcmp(name, "jitter")) {
                unsigned j;
                for (j = 0; j < 15; j++) {
                    at(300 + j * 100, .35f + (j % 2 ? .01f : -.01f), 1);
                    CHECK(!fired);
                }
                CHECK(g.snapshot.last_event_id == 0);
            } else if (!strcmp(name, "dropout")) {
                at(300, .5f, 1);
                at(400, .5f, 0);
                at(500, b, 1);
                at(650, b, 1);
                CHECK(fired);
            } else if (!strcmp(name, "frame_gap")) {
                at(300, .5f, 1);
                at(651, b, 1);
                at(801, b, 1);
                CHECK(!fired && g.snapshot.last_event_id == 0);
            } else if (!strcmp(name, "loss")) {
                at(300, .5f, 1);
                at(651, .5f, 0);
                at(800, b, 1);
                at(950, b, 1);
                CHECK(!fired && g.snapshot.last_event_id == 0);
            } else if (!strcmp(name, "ambiguous")) {
                at(300, .5f, 1);
                at(400, .5f, 2);
                CHECK(g.snapshot.state == GREETER_AMBIGUOUS);
                at(500, b, 1);
                at(650, b, 1);
                CHECK(!fired);
                f = frame(700, b, 1);
                f.truncated = true;
                CHECK(!greeter_update(&g, &f, &e) && g.snapshot.state == GREETER_AMBIGUOUS);
            } else if (!strcmp(name, "invalid")) {
                at(300, .5f, 1);
                f = frame(400, b, 1);
                f.valid = false;
                CHECK(!greeter_update(&g, &f, &e));
                at(500, b, 1);
                at(650, b, 1);
                CHECK(!fired);
                f = frame(700, b, 11);
                CHECK(!greeter_update(&g, &f, &e) && g.snapshot.last_error == GREETER_BAD_INPUT);
                f = frame(800, b, 1);
                f.persons[0].x1 = NAN;
                CHECK(!greeter_update(&g, &f, &e));
                f = frame(900, b, 1);
                f.persons[0].score = .5f;
                CHECK(!greeter_update(&g, &f, &e) && !g.have_point);
            } else if (!strcmp(name, "chronology")) {
                f = frame(300, .5f, 1);
                CHECK(!greeter_update(&g, &f, &e));
                CHECK(!greeter_update(&g, &f, &e) && g.snapshot.last_error == GREETER_OLD_FRAME);
                f.sequence_id = ++seq;
                f.now_ms = 300;
                CHECK(!greeter_update(&g, &f, &e));
                f.sequence_id = ++seq;
                f.now_ms = 299;
                CHECK(!greeter_update(&g, &f, &e) &&
                      g.snapshot.last_error == GREETER_TIME_BACKWARD && !g.origin);
                CHECK(g.last_now == 300);
                at(200, a, 1);
                CHECK(g.snapshot.last_error == GREETER_TIME_BACKWARD && g.last_now == 300 &&
                      !g.have_point);
                at(299, a, 1);
                CHECK(!g.have_point);
                at(300, a, 1);
                CHECK(g.have_point && !g.origin);
                at(450, a, 1);
                CHECK(g.origin != 0);
            } else if (!strcmp(name, "controls")) {
                cross(300, b);
                greeter_reset_counts(&g);
                CHECK(g.snapshot.last_event_id == 1 && g.snapshot.in_count == 0 &&
                      g.snapshot.out_count == 0 && !g.origin);
                at(750, b, 1);
                at(900, b, 1);
                greeter_set_reverse(&g, true);
                CHECK(!g.origin);
                at(1050, b, 1);
                at(1200, b, 1);
                cross(1350, a);
                CHECK(e.event_id == 2);
            } else if (!strcmp(name, "control_reset")) {
                at(300, .5f, 1);
                greeter_reset_counts(&g);
                CHECK(!g.origin && !g.middle && !g.traversing &&
                      g.snapshot.last_event_id == 0);
                at(450, b, 1);
                at(600, b, 1);
                CHECK(!fired && g.origin == (dir ? -1 : 1));
                cross(750, a);
                CHECK(fired && e.event_id == 1 &&
                      e.type == (dir ? GREETER_ENTER : GREETER_EXIT));
                greeter_reset_counts(&g);
                CHECK(g.snapshot.last_event_id == 1 && g.snapshot.in_count == 0 &&
                      g.snapshot.out_count == 0);
                at(1200, a, 1);
                greeter_reset_counts(&g); /* reset an incomplete endpoint timer */
                at(1300, a, 1);
                at(1449, a, 1);
                CHECK(!g.origin);
                at(1450, a, 1);
                CHECK(g.origin == (dir ? 1 : -1));
                cross(1600, b);
                CHECK(e.event_id == 2);
            } else if (!strcmp(name, "control_reverse")) {
                cross(300, b);
                CHECK(g.snapshot.last_event_id == 1);
                at(750, .5f, 1);
                greeter_set_reverse(&g, true);
                CHECK(!g.origin && !g.middle && g.snapshot.last_event_id == 1 &&
                      g.snapshot.in_count + g.snapshot.out_count == 1);
                at(900, a, 1);
                at(1050, a, 1);
                CHECK(!fired);
                cross(1200, b);
                CHECK(e.event_id == 2 &&
                      e.type == (dir ? GREETER_ENTER : GREETER_EXIT));
                at(1650, .5f, 1);
                greeter_set_reverse(&g, true); /* same value must preserve candidate */
                CHECK(g.origin != 0 && g.middle);
                at(1800, a, 1);
                at(1950, a, 1);
                CHECK(fired && e.event_id == 3);
            } else if (!strcmp(name, "control_watermarks")) {
                greeter_frame_t old;
                at(300, .5f, 1);
                old = frame(450, b, 1);
                greeter_reset_counts(&g);
                CHECK(!greeter_update(&g, &old, &e));
                CHECK(!greeter_update(&g, &old, &e) &&
                      g.snapshot.last_error == GREETER_OLD_FRAME);
                greeter_set_reverse(&g, true);
                f = frame(449, b, 1);
                CHECK(!greeter_update(&g, &f, &e) &&
                      g.snapshot.last_error == GREETER_TIME_BACKWARD);
                CHECK(g.last_now == 450 && !g.origin);
                greeter_reset_counts(&g);
                greeter_set_reverse(&g, false);
                greeter_set_reverse(&g, true);
                at(450, b, 1);
                at(600, b, 1);
                CHECK(g.origin != 0 && g.snapshot.last_event_id == 0);
            } else if (!strcmp(name, "timeout")) {
                unsigned j;
                at(300, .5f, 1);
                for (j = 1; j <= 16; j++) {
                    at(300 + j * 300, .5f, 1);
                    CHECK(!fired);
                }
                at(5301, .5f, 1);
                CHECK(!fired && g.snapshot.last_error == GREETER_TIMEOUT);
            } else if (!strcmp(name, "association")) {
                at(151, b, 1);
                CHECK(!fired && g.snapshot.last_error == GREETER_TRACK_JUMP && !g.origin);
            } else if (!strcmp(name, "stable_gap")) {
                at(300, .5f, 1);
                at(450, b, 1);
                at(500, b, 0);
                at(550, b, 1);
                at(650, b, 1);
                CHECK(!fired);
                at(700, b, 1);
                CHECK(fired);
            } else if (!strcmp(name, "grace_boundary")) {
                at(300, .5f, 1);
                at(650, b, 1);
                CHECK(g.origin != 0 && !fired);
                at(800, b, 1);
                CHECK(fired);
            } else if (!strcmp(name, "evidence_jump")) {
                at(300, .5f, 1);
                at(301, b, 1);
                CHECK(!fired && g.snapshot.last_error == GREETER_TRACK_JUMP && !g.middle);
                at(450, b, 1);
                at(600, b, 1);
                CHECK(!fired);
            } else if (!strcmp(name, "stationary")) {
                unsigned j;
                for (j = 1; j <= 40; j++) {
                    at(150 + j * 150, a, 1);
                    CHECK(!fired);
                }
                cross(6300, b);
                CHECK(e.event_id == 1);
            } else if (!strcmp(name, "overflow")) {
                g.snapshot.last_event_id = UINT64_MAX;
                at(300, .5f, 1);
                at(450, b, 1);
                at(600, b, 1);
                CHECK(!fired && g.snapshot.last_error == GREETER_OVERFLOW);
            } else
                CHECK(false);
        }
    puts(name);
    return 0;
}
