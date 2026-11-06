/* K210 application adapter. The counting algorithm remains in cloud-owned core/. */
#include "counting_app.h"
#include "audio_queue.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static greeter_t counter;
static audio_queue_t audio;
static greeter_frame_t frame;
static greeter_snapshot_t snapshot;
static greeter_config_t config;

bool counting_app_init(void)
{
    config = greeter_default_config();
    if (greeter_init(&counter, &config) != GREETER_OK || !audio_queue_init(&audio, 5000))
        return false;
    audio_queue_set_muted(&audio, true); /* No I2S or SD driver in this release. */
    greeter_get_snapshot(&counter, &snapshot);
    return true;
}

const greeter_config_t *counting_app_config(void) { return &config; }
const greeter_snapshot_t *counting_app_snapshot(void) { return &snapshot; }

const char *counting_app_state_name(greeter_state_t state)
{
    switch (state) {
    case GREETER_WAIT_ORIGIN: return "WAIT_ORIGIN";
    case GREETER_FROM_A: return "FROM_A";
    case GREETER_FROM_B: return "FROM_B";
    case GREETER_LOST: return "LOST";
    case GREETER_AMBIGUOUS: return "AMBIGUOUS";
    default: return "UNKNOWN";
    }
}

void counting_app_log_config(void)
{
    /* These are the actual float values used by greeter_init, not rounded labels. */
    printf("{\"kind\":\"config\",\"interface\":\"%s\",\"greeter\":{"
        "\"axis\":\"%s\",\"line_a\":%.9g,\"line_b\":%.9g,\"hysteresis\":%.9g,"
        "\"confidence_threshold\":%.9g,\"association_tolerance\":%.9g,"
        "\"max_speed_per_s\":%.9g,\"outer_stable_ms\":%" PRIu64 ","
        "\"lost_grace_ms\":%" PRIu64 ",\"traversal_timeout_ms\":%" PRIu64 ","
        "\"reverse_direction\":%s},\"audio\":{\"enabled\":false,\"muted\":true,"
        "\"capacity\":%u,\"max_wait_ms\":%" PRIu64 "},\"sizes\":{"
        "\"greeter\":%u,\"audio_queue\":%u,\"frame\":%u,\"snapshot\":%u}}\n",
        GREETER_INTERFACE_VERSION, config.axis == GREETER_X ? "X" : "Y",
        (double)config.line_a, (double)config.line_b, (double)config.hysteresis,
        (double)config.confidence_threshold, (double)config.association_tolerance,
        (double)config.max_speed_per_s, config.outer_stable_ms, config.lost_grace_ms,
        config.traversal_timeout_ms, config.reverse_direction ? "true" : "false",
        (unsigned)AUDIO_QUEUE_CAPACITY, audio.max_wait_ms, (unsigned)sizeof(counter),
        (unsigned)sizeof(audio), (unsigned)sizeof(frame), (unsigned)sizeof(snapshot));
}

static void make_frame(const obj_info_t *detections, uint64_t seq, uint64_t observed_ms)
{
    memset(&frame, 0, sizeof(frame));
    frame.sequence_id = seq;
    frame.now_ms = observed_ms;
    if (!detections || detections->obj_number > GREETER_MAX_PERSONS) return;
    frame.valid = true;
    frame.truncated = detections->truncated != 0 || detections->obj_number == GREETER_MAX_PERSONS;
    for (uint32_t i = 0; i < detections->obj_number; ++i) {
        if (detections->obj[i].class_id != 14) continue;
        float score = detections->obj[i].prob;
        if (!isfinite(score) || score < 0 || score > 1 ||
            detections->obj[i].x2 > 319 || detections->obj[i].y2 > 239 ||
            detections->obj[i].x1 >= detections->obj[i].x2 ||
            detections->obj[i].y1 >= detections->obj[i].y2) {
            frame.valid = false;
            frame.person_count = 0;
            return; /* Never emit NaN/Inf as invalid JSON or read beyond capacity. */
        }
        if (score <= config.confidence_threshold) continue;
        greeter_person_t *person = &frame.persons[frame.person_count++];
        person->x1 = detections->obj[i].x1 / 319.0f;
        person->y1 = detections->obj[i].y1 / 239.0f;
        person->x2 = detections->obj[i].x2 / 319.0f;
        person->y2 = detections->obj[i].y2 / 239.0f;
        person->score = score;
    }
}

void counting_app_process(const obj_info_t *detections, uint64_t seq,
                          uint64_t observed_ms, uint64_t service_ms, uint64_t dt_ms)
{
    greeter_event_t event;
    make_frame(detections, seq, observed_ms);
    bool has_event = greeter_update(&counter, &frame, &event);
    greeter_get_snapshot(&counter, &snapshot);
    /* Log exactly the same frame structure passed to core, including its floats. */
    printf("{\"kind\":\"frame\",\"seq\":%" PRIu64 ",\"t_ms\":%" PRIu64
        ",\"valid\":%s,\"truncated\":%s,\"persons\":[", frame.sequence_id, frame.now_ms,
        frame.valid ? "true" : "false", frame.truncated ? "true" : "false");
    for (uint32_t i = 0; i < frame.person_count; ++i) {
        const greeter_person_t *p = &frame.persons[i];
        printf("%s{\"x1\":%.9g,\"y1\":%.9g,\"x2\":%.9g,\"y2\":%.9g,\"score\":%.9g}",
            i ? "," : "", (double)p->x1, (double)p->y1, (double)p->x2,
            (double)p->y2, (double)p->score);
    }
    printf("],\"dt_ms\":%" PRIu64 "}\n", dt_ms);
    if (has_event) {
        audio_result_t queued = audio_queue_push(&audio, &event, service_ms);
        printf("{\"kind\":\"event\",\"id\":%" PRIu64 ",\"t_ms\":%" PRIu64
            ",\"type\":\"%s\",\"in\":%" PRIu64 ",\"out\":%" PRIu64 "}\n",
            event.event_id, event.timestamp_ms, event.type == GREETER_ENTER ? "ENTER" : "EXIT",
            snapshot.in_count, snapshot.out_count);
        printf("{\"kind\":\"audio\",\"id\":%" PRIu64 ",\"t_ms\":%" PRIu64
            ",\"status\":\"%s\",\"result\":%d}\n", event.event_id, service_ms,
            queued == AUDIO_MUTED ? "muted" : "queue_error", (int)queued);
    }
    printf("{\"kind\":\"state\",\"seq\":%" PRIu64 ",\"t_ms\":%" PRIu64
        ",\"state\":\"%s\",\"error\":%d,\"in\":%" PRIu64 ",\"out\":%" PRIu64 "}\n",
        frame.sequence_id, frame.now_ms, counting_app_state_name(snapshot.state),
        (int)snapshot.last_error, snapshot.in_count, snapshot.out_count);
}
