/* Host-only adapter. Every frame is processed by the production C state machine. */
#include "greeter.h"
static greeter_t instance;
int replay_init(int reverse) {
    greeter_config_t c = greeter_default_config();
    c.reverse_direction = reverse != 0;
    return (int)greeter_init(&instance, &c);
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
    out[0] = e.event_id;
    out[1] = e.timestamp_ms;
    out[2] = (uint64_t)e.type;
    out[3] = instance.snapshot.in_count;
    out[4] = instance.snapshot.out_count;
    out[5] = (uint64_t)instance.snapshot.last_error;
    return has ? 1 : 0;
}
