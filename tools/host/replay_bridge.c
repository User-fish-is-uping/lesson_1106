/* Host-only adapter. Every frame is processed by the production C state machine. */
#include "audio_queue.h"
#include "greeter.h"
static greeter_t instance;
static audio_queue_t audio;

static void replay_state(uint64_t *out) {
    out[3] = instance.snapshot.in_count;
    out[4] = instance.snapshot.out_count;
    out[5] = (uint64_t)instance.snapshot.last_error;
    out[6] = instance.snapshot.last_event_id;
    out[7] = instance.config.reverse_direction ? 1u : 0u;
    out[8] = audio.count;
    out[9] = audio.muted ? 1u : 0u;
    out[10] = audio.stats.muted;
    out[11] = audio.stats.cleared;
    out[12] = audio.stats.full;
    out[13] = audio.stats.expired;
}

int replay_init(int reverse) {
    greeter_config_t c = greeter_default_config();
    c.reverse_direction = reverse != 0;
    if (greeter_init(&instance, &c) != GREETER_OK || !audio_queue_init(&audio, 5000))
        return 1;
    return 0;
}
int replay_frame(uint64_t seq, uint64_t time, int valid, int truncated, unsigned count,
                 const float *boxes, uint64_t *out) {
    greeter_frame_t f = {0};
    greeter_event_t e = {0};
    unsigned i;
    bool has;
    f.sequence_id = seq;
    f.now_ms = time;
    f.valid = valid != 0;
    f.truncated = truncated != 0;
    f.person_count = count;
    for (i = 0; i < count && i < GREETER_MAX_PERSONS; i++) {
        f.persons[i].x1 = boxes[i * 5];
        f.persons[i].y1 = boxes[i * 5 + 1];
        f.persons[i].x2 = boxes[i * 5 + 2];
        f.persons[i].y2 = boxes[i * 5 + 3];
        f.persons[i].score = boxes[i * 5 + 4];
    }
    has = greeter_update(&instance, &f, &e);
    if (has)
        (void)audio_queue_push(&audio, &e, f.now_ms);
    out[0] = e.event_id;
    out[1] = e.timestamp_ms;
    out[2] = (uint64_t)e.type;
    replay_state(out);
    return has ? 1 : 0;
}

/* Control records are applied in file order. Their timestamp is metadata only:
 * controls must not impersonate frames or advance the vision time watermark. */
int replay_control(unsigned action, int value, uint64_t time, uint64_t *out) {
    (void)time;
    if (action == 0) {
        greeter_reset_counts(&instance);
        audio_queue_clear(&audio);
    } else if (action == 1) {
        bool reverse = value != 0;
        if (instance.config.reverse_direction != reverse) {
            greeter_set_reverse(&instance, reverse);
            audio_queue_clear(&audio);
        }
    } else if (action == 2) {
        audio_queue_set_muted(&audio, value != 0);
    } else
        return 1;
    replay_state(out);
    return 0;
}

void replay_get_state(uint64_t *out) { replay_state(out); }
/* Host config bridge avoids exposing the native struct layout to Python. */
int replay_configure(unsigned axis, const float *values, const uint64_t *times, int reverse) {
    greeter_config_t c = greeter_default_config();
    c.axis = (greeter_axis_t)axis;
    c.line_a = values[0];
    c.line_b = values[1];
    c.hysteresis = values[2];
    c.confidence_threshold = values[3];
    c.association_tolerance = values[4];
    c.max_speed_per_s = values[5];
    c.outer_stable_ms = times[0];
    c.lost_grace_ms = times[1];
    c.traversal_timeout_ms = times[2];
    c.reverse_direction = reverse != 0;
    if (greeter_init(&instance, &c) != GREETER_OK || !audio_queue_init(&audio, 5000))
        return 1;
    return 0;
}
