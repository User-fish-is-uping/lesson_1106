#ifndef GREETER_H
#define GREETER_H
#include <stdbool.h>
#include <stdint.h>
#define GREETER_MAX_PERSONS 10
#define GREETER_INTERFACE_VERSION "0.2"
typedef enum { GREETER_X, GREETER_Y } greeter_axis_t;
typedef enum { GREETER_ENTER = 1, GREETER_EXIT = 2 } greeter_event_type_t;
typedef enum {
    GREETER_WAIT_ORIGIN,
    GREETER_FROM_A,
    GREETER_FROM_B,
    GREETER_LOST,
    GREETER_AMBIGUOUS
} greeter_state_t;
typedef enum {
    GREETER_OK,
    GREETER_BAD_CONFIG,
    GREETER_BAD_INPUT,
    GREETER_OLD_FRAME,
    GREETER_TIME_BACKWARD,
    GREETER_TRACK_JUMP,
    GREETER_TIMEOUT,
    GREETER_OVERFLOW
} greeter_error_t;
typedef struct {
    float x1, y1, x2, y2, score;
} greeter_person_t;
typedef struct {
    uint64_t sequence_id, now_ms;
    bool valid, truncated;
    uint32_t person_count;
    greeter_person_t persons[GREETER_MAX_PERSONS];
} greeter_frame_t;
typedef struct {
    uint64_t event_id, timestamp_ms;
    greeter_event_type_t type;
} greeter_event_t;
typedef struct {
    greeter_axis_t axis;
    float line_a, line_b, hysteresis, confidence_threshold;
    float association_tolerance, max_speed_per_s;
    uint64_t outer_stable_ms, lost_grace_ms, traversal_timeout_ms;
    bool reverse_direction;
} greeter_config_t;
typedef struct {
    greeter_state_t state;
    uint64_t in_count, out_count, last_event_id;
    int candidate_direction;
    greeter_error_t last_error;
} greeter_snapshot_t;
/* State is caller-owned; treat implementation fields as private. No heap/SDK/ISR calls. */
typedef struct {
    greeter_config_t config;
    greeter_snapshot_t snapshot;
    bool initialized, have_frame, have_point, middle, traversing, stable_active;
    uint64_t last_seq, last_now, last_seen, stable_since, traversal_since;
    float last_x, last_y;
    int origin, stable_side;
} greeter_t;
greeter_config_t greeter_default_config(void);
/* Finite floats; 0<a<b<1; h>=0; a-h>0; b+h<1; a+h<b-h.
 * Confidence/tolerance in [0,1], speed>=0; stable/grace>0; timeout>both. */
greeter_error_t greeter_validate_config(const greeter_config_t *config);
greeter_error_t greeter_init(greeter_t *self, const greeter_config_t *config);
/* Returns true only when out_event was written. Consult snapshot.last_error otherwise.
 * Sequence checked first. Old sequences do not advance time/track. A new sequence
 * with backward time is consumed, clears track, and preserves accepted time watermark.
 * No new frame/observation for >grace also breaks continuity. Never use fast-math. */
bool greeter_update(greeter_t *self, const greeter_frame_t *frame, greeter_event_t *out_event);
void greeter_get_snapshot(const greeter_t *self, greeter_snapshot_t *out);
void greeter_reset_counts(greeter_t *self);
void greeter_set_reverse(greeter_t *self, bool reverse);
#endif
