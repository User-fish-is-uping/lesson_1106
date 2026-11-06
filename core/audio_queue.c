#include "audio_queue.h"
#include <limits.h>
#include <string.h>
static void inc(uint64_t *n) {
    if (*n < UINT64_MAX)
        ++*n;
}
static void pop(audio_queue_t *q) {
    if (q->count) {
        --q->count;
        memmove(q->items, q->items + 1, q->count * sizeof(q->items[0]));
    }
}
static audio_result_t tick(audio_queue_t *q, uint64_t now) {
    unsigned i = 0;
    if (!q || !q->max_wait_ms)
        return AUDIO_BAD_INPUT;
    if (q->have_time && now < q->last_now)
        return AUDIO_TIME_BACKWARD;
    q->have_time = true;
    q->last_now = now;
    while (i < q->count) {
        if (now >= q->items[i].event.timestamp_ms &&
            now - q->items[i].event.timestamp_ms >= q->max_wait_ms) {
            --q->count;
            memmove(q->items + i, q->items + i + 1, (q->count - i) * sizeof(q->items[0]));
            inc(&q->stats.expired);
        } else
            ++i;
    }
    return AUDIO_OK;
}
bool audio_queue_init(audio_queue_t *q, uint64_t wait) {
    if (!q || !wait)
        return false;
    memset(q, 0, sizeof(*q));
    q->max_wait_ms = wait;
    return true;
}
audio_result_t audio_queue_push(audio_queue_t *q, const greeter_event_t *e, uint64_t now) {
    audio_result_t r = tick(q, now);
    if (r != AUDIO_OK)
        return r;
    if (!e || !e->event_id || (e->type != GREETER_ENTER && e->type != GREETER_EXIT) ||
        e->timestamp_ms > now) {
        inc(&q->stats.invalid);
        return AUDIO_BAD_INPUT;
    }
    if (e->event_id <= q->watermark) {
        inc(&q->stats.duplicate);
        return AUDIO_DUPLICATE;
    }
    q->watermark = e->event_id;
    if (q->muted) {
        inc(&q->stats.muted);
        return AUDIO_MUTED;
    }
    if (now - e->timestamp_ms >= q->max_wait_ms) {
        inc(&q->stats.expired);
        return AUDIO_EMPTY;
    }
    if (q->count == AUDIO_QUEUE_CAPACITY) {
        inc(&q->stats.full);
        return AUDIO_FULL;
    }
    q->items[q->count].event = *e;
    q->items[q->count].attempts = 0;
    ++q->count;
    return AUDIO_OK;
}
audio_result_t audio_queue_peek(audio_queue_t *q, uint64_t now, audio_item_t *out) {
    audio_result_t r = tick(q, now);
    if (r != AUDIO_OK)
        return r;
    if (!out)
        return AUDIO_BAD_INPUT;
    if (!q->count)
        return AUDIO_EMPTY;
    *out = q->items[0];
    return AUDIO_OK;
}
audio_result_t audio_queue_report(audio_queue_t *q, uint64_t id, audio_start_result_t result,
                                  uint64_t now) {
    audio_result_t r = tick(q, now);
    if (r != AUDIO_OK)
        return r;
    if (result != AUDIO_STARTED && result != AUDIO_BUSY && result != AUDIO_FAILED)
        return AUDIO_BAD_INPUT;
    if (!q->count || q->items[0].event.event_id != id)
        return AUDIO_ID_MISMATCH;
    if (result == AUDIO_STARTED) {
        inc(&q->stats.started);
        pop(q);
    } else if (result == AUDIO_FAILED) {
        inc(&q->stats.start_failures);
        if (++q->items[0].attempts >= 2) {
            inc(&q->stats.failed_dropped);
            pop(q);
        }
    }
    return AUDIO_OK;
}
void audio_queue_clear(audio_queue_t *q) {
    if (q) {
        unsigned i;
        for (i = 0; i < q->count; i++)
            inc(&q->stats.cleared);
        q->count = 0;
    }
}
void audio_queue_set_muted(audio_queue_t *q, bool muted) {
    if (q) {
        if (muted && !q->muted)
            audio_queue_clear(q);
        q->muted = muted;
    }
}
void audio_queue_report_async_failure(audio_queue_t *q) {
    if (q)
        inc(&q->stats.async_failures);
}
const char *audio_queue_path(greeter_event_type_t t) {
    return t == GREETER_ENTER ? "0:/welcome.wav" : t == GREETER_EXIT ? "0:/goodbye.wav" : NULL;
}
