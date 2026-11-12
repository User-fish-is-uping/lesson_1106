#ifndef GREETER_AUDIO_PLAYER_H
#define GREETER_AUDIO_PLAYER_H
#include "audio_queue.h"
/* Preload two short clips once at boot; no SD access during detection/playback. */
void audio_player_init(void);
audio_start_result_t audio_player_start(greeter_event_type_t type, uint64_t now_ms);
bool audio_player_service(uint64_t now_ms); /* true once on asynchronous failure */
const char *audio_player_status(void);
#endif
