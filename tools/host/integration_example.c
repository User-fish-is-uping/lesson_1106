/* Compile-only SDK-free example. Desktop supplies real frame, player and controls. */
#include "audio_queue.h"
#include "greeter.h"
typedef struct {
    greeter_t greeter;
    audio_queue_t audio;
} app_t;
bool app_init(app_t *app) {
    greeter_config_t config = greeter_default_config();
    return greeter_init(&app->greeter, &config) == GREETER_OK &&
           audio_queue_init(&app->audio, 5000);
}
void app_controls(app_t *app, bool clear, bool reverse, bool mute) {
    if (clear) {
        greeter_reset_counts(&app->greeter);
        audio_queue_clear(&app->audio);
    }
    if (app->greeter.config.reverse_direction != reverse) {
        greeter_set_reverse(&app->greeter, reverse);
        audio_queue_clear(&app->audio);
    }
    audio_queue_set_muted(&app->audio, mute);
}
void app_frame(app_t *app, const greeter_frame_t *frame) {
    greeter_event_t event;
    if (greeter_update(&app->greeter, frame, &event))
        (void)audio_queue_push(&app->audio, &event, frame->now_ms);
}
/* Main loop: peek with current time; if player idle, try once and report once.
 * Return to the loop after FAILED. Do not retry in a tight loop.
 * Submit using the same sampled now_ms; async failures only increment stats.
 * STARTED removes the item immediately; do not report it again on completion.
 */
audio_result_t app_audio_result(app_t *app, const audio_item_t *item, audio_start_result_t result,
                                uint64_t now_ms) {
    return audio_queue_report(&app->audio, item->event.event_id, result, now_ms);
}
