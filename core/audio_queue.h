#ifndef AUDIO_QUEUE_H
#define AUDIO_QUEUE_H
#include "greeter.h"
#define AUDIO_QUEUE_CAPACITY 4
typedef enum { AUDIO_STARTED, AUDIO_BUSY, AUDIO_FAILED } audio_start_result_t;
typedef enum {
    AUDIO_OK,
    AUDIO_EMPTY,
    AUDIO_DUPLICATE,
    AUDIO_FULL,
    AUDIO_MUTED,
    AUDIO_BAD_INPUT,
    AUDIO_TIME_BACKWARD,
    AUDIO_ID_MISMATCH
} audio_result_t;
typedef struct {
    greeter_event_t event;
    unsigned attempts;
} audio_item_t;
typedef struct {
    uint64_t duplicate, full, expired, muted, cleared, start_failures, failed_dropped, started,
        async_failures, invalid;
} audio_stats_t;
typedef struct {
    audio_item_t items[AUDIO_QUEUE_CAPACITY];
    unsigned count;
    bool muted, have_time;
    uint64_t watermark, last_now, max_wait_ms;
    audio_stats_t stats;
} audio_queue_t;
bool audio_queue_init(audio_queue_t *q, uint64_t max_wait_ms);
audio_result_t audio_queue_push(audio_queue_t *q, const greeter_event_t *event, uint64_t now_ms);
audio_result_t audio_queue_peek(audio_queue_t *q, uint64_t now_ms, audio_item_t *out);
/* One result submission per main-loop service; retry FAILED only on a later service. */
audio_result_t audio_queue_report(audio_queue_t *q, uint64_t event_id, audio_start_result_t result,
                                  uint64_t now_ms);
void audio_queue_clear(audio_queue_t *q);
void audio_queue_set_muted(audio_queue_t *q, bool muted);
void audio_queue_report_async_failure(audio_queue_t *q);
const char *audio_queue_path(greeter_event_type_t type);
#endif
