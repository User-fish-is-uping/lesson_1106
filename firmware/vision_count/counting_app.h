#ifndef COUNTING_APP_H
#define COUNTING_APP_H
#include "greeter.h"
#include "region_layer.h"

/* Board integration only: one static instance, called by the main loop. */
bool counting_app_init(void);
/* Main-loop controls, before the next frame/audio service; never call from ISR. */
void counting_app_reset_counts(void);
void counting_app_set_reverse(bool reverse);
const greeter_config_t *counting_app_config(void);
const greeter_snapshot_t *counting_app_snapshot(void);
const char *counting_app_state_name(greeter_state_t state);
void counting_app_log_config(void);
/* NULL detections means an invalid vision frame, not an empty scene.
 * observed_ms timestamps capture; service_ms is a fresh sample of the same clock. */
void counting_app_process(const obj_info_t *detections, uint64_t seq,
                          uint64_t observed_ms, uint64_t service_ms, uint64_t dt_ms);
#endif
