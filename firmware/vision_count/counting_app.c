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
static counting_audio_start_t audio_start;

bool counting_app_init(void)
{
    config = greeter_default_config();
    if (greeter_init(&counter, &config) != GREETER_OK || !audio_queue_init(&audio, 5000))
        return false;
    audio_start = NULL;
    audio_queue_set_muted(&audio, true); /* Stay muted until a driver is attached. */
    greeter_get_snapshot(&counter, &snapshot);
    return true;
}

const greeter_config_t *counting_app_config(void) { return &config; }
const greeter_snapshot_t *counting_app_snapshot(void) { return &snapshot; }

void counting_app_attach_audio(counting_audio_start_t start) { audio_start = start; }
void counting_app_set_muted(bool muted) { audio_queue_set_muted(&audio, muted || !audio_start); }
bool counting_app_muted(void) { return audio.muted; }
void counting_app_service_audio(uint64_t now_ms, bool asynchronous_failure)
{
    if (asynchronous_failure) audio_queue_report_async_failure(&audio);
    audio_item_t item;
    if (!audio_start || audio_queue_peek(&audio, now_ms, &item) != AUDIO_OK) return;
    audio_start_result_t result = audio_start(item.event.type, now_ms);
    audio_queue_report(&audio, item.event.event_id, result, now_ms);
    if (result != AUDIO_BUSY)
        printf("{\"kind\":\"audio\",\"id\":%" PRIu64 ",\"t_ms\":%" PRIu64
               ",\"status\":\"%s\",\"file\":\"%s\"}\n", item.event.event_id, now_ms,
               result == AUDIO_STARTED ? "start" : "failed", audio_queue_path(item.event.type));
}

void counting_app_reset_counts(void)
{
    greeter_reset_counts(&counter);
    audio_queue_clear(&audio);
    greeter_get_snapshot(&counter, &snapshot);
}

void counting_app_set_reverse(bool reverse)
{
    if (config.reverse_direction == reverse) return;
    greeter_set_reverse(&counter, reverse);
    config.reverse_direction = reverse;
    audio_queue_clear(&audio);
    greeter_get_snapshot(&counter, &snapshot);
}

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
        "\"reverse_direction\":%s},\"audio\":{\"enabled\":%s,\"muted\":%s,"
        "\"capacity\":%u,\"max_wait_ms\":%" PRIu64 "},\"sizes\":{"
        "\"greeter\":%u,\"audio_queue\":%u,\"frame\":%u,\"snapshot\":%u}}\n",
        GREETER_INTERFACE_VERSION, config.axis == GREETER_X ? "X" : "Y",
        (double)config.line_a, (double)config.line_b, (double)config.hysteresis,
        (double)config.confidence_threshold, (double)config.association_tolerance,
        (double)config.max_speed_per_s, config.outer_stable_ms, config.lost_grace_ms,
        config.traversal_timeout_ms, config.reverse_direction ? "true" : "false",
        audio_start ? "true" : "false", audio.muted ? "true" : "false",
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
        queued == AUDIO_MUTED ? "muted" : (queued == AUDIO_OK ? "queued" : "queue_error"), (int)queued);
    }
    printf("{\"kind\":\"state\",\"seq\":%" PRIu64 ",\"t_ms\":%" PRIu64
        ",\"state\":\"%s\",\"error\":%d,\"in\":%" PRIu64 ",\"out\":%" PRIu64 "}\n",
        frame.sequence_id, frame.now_ms, counting_app_state_name(snapshot.state),
        (int)snapshot.last_error, snapshot.in_count, snapshot.out_count);
}
