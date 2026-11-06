#include "greeter.h"
#include <limits.h>
#include <math.h>
#include <string.h>
static void clear_track(greeter_t *g) {
    g->have_point = false;
    g->middle = false;
    g->traversing = false;
    g->stable_active = false;
    g->origin = 0;
    g->stable_side = 0;
    g->snapshot.state = GREETER_WAIT_ORIGIN;
    g->snapshot.candidate_direction = 0;
}
greeter_config_t greeter_default_config(void) {
    greeter_config_t c = {GREETER_X, .35f, .65f, .03f, .50f, .05f, 2.0f, 150, 350, 5000, false};
    return c;
}
greeter_error_t greeter_validate_config(const greeter_config_t *c) {
    if (!c || (c->axis != GREETER_X && c->axis != GREETER_Y) || !isfinite(c->line_a) ||
        !isfinite(c->line_b) || !isfinite(c->hysteresis) || !isfinite(c->confidence_threshold) ||
        !isfinite(c->association_tolerance) || !isfinite(c->max_speed_per_s) || c->line_a <= 0 ||
        c->line_b >= 1 || c->line_a >= c->line_b || c->hysteresis < 0 ||
        c->line_a - c->hysteresis <= 0 || c->line_b + c->hysteresis >= 1 ||
        c->line_a + c->hysteresis >= c->line_b - c->hysteresis || c->confidence_threshold < 0 ||
        c->confidence_threshold > 1 || c->association_tolerance < 0 ||
        c->association_tolerance > 1 || c->max_speed_per_s < 0 || !c->outer_stable_ms ||
        !c->lost_grace_ms || c->traversal_timeout_ms <= c->lost_grace_ms ||
        c->traversal_timeout_ms <= c->outer_stable_ms)
        return GREETER_BAD_CONFIG;
    return GREETER_OK;
}
greeter_error_t greeter_init(greeter_t *g, const greeter_config_t *c) {
    greeter_error_t e = greeter_validate_config(c);
    if (!g)
        return GREETER_BAD_INPUT;
    if (e != GREETER_OK)
        return e;
    memset(g, 0, sizeof(*g));
    g->config = *c;
    g->initialized = true;
    return GREETER_OK;
}
static bool bad(greeter_t *g, greeter_error_t e) {
    clear_track(g);
    g->snapshot.last_error = e;
    return false;
}
static bool valid_box(const greeter_person_t *p) {
    return isfinite(p->x1) && isfinite(p->y1) && isfinite(p->x2) && isfinite(p->y2) &&
           isfinite(p->score) && p->x1 >= 0 && p->y1 >= 0 && p->x2 <= 1 && p->y2 <= 1 &&
           p->x1 < p->x2 && p->y1 < p->y2 && p->score >= 0 && p->score <= 1;
}
static void stable(greeter_t *g, int side, uint64_t now) {
    if (!g->stable_active || g->stable_side != side) {
        g->stable_active = true;
        g->stable_side = side;
        g->stable_since = now;
    }
}
bool greeter_update(greeter_t *g, const greeter_frame_t *f, greeter_event_t *ev) {
    uint32_t i, n = 0;
    const greeter_person_t *p = NULL;
    float x, y, pos;
    int side;
    if (!g || !g->initialized)
        return false;
    if (!f || !ev)
        return bad(g, GREETER_BAD_INPUT);
    if (g->have_frame && f->sequence_id <= g->last_seq) {
        g->snapshot.last_error = GREETER_OLD_FRAME;
        return false;
    }
    if (g->have_frame && f->now_ms < g->last_now) {
        g->last_seq = f->sequence_id;
        /* Keep the last accepted time watermark until the clock catches up. */
        return bad(g, GREETER_TIME_BACKWARD);
    }
    g->have_frame = true;
    g->last_seq = f->sequence_id;
    g->last_now = f->now_ms;
    g->snapshot.last_error = GREETER_OK;
    if (!f->valid || f->person_count > GREETER_MAX_PERSONS)
        return bad(g, GREETER_BAD_INPUT);
    for (i = 0; i < f->person_count; i++) {
        if (!valid_box(&f->persons[i]))
            return bad(g, GREETER_BAD_INPUT);
        if (f->persons[i].score > g->config.confidence_threshold) {
            p = &f->persons[i];
            n++;
        }
    }
    if (f->truncated || n > 1) {
        clear_track(g);
        g->snapshot.state = GREETER_AMBIGUOUS;
        return false;
    }
    if (g->have_point && f->now_ms - g->last_seen > g->config.lost_grace_ms)
        clear_track(g);
    if (g->traversing && f->now_ms - g->traversal_since > g->config.traversal_timeout_ms)
        return bad(g, GREETER_TIMEOUT);
    if (!n) {
        g->stable_active = false;
        if (g->have_point)
            g->snapshot.state = GREETER_LOST;
        return false;
    }
    x = (p->x1 + p->x2) * .5f;
    y = p->y2;
    if (g->have_point) {
        double dx = x - g->last_x, dy = y - g->last_y;
        double limit = g->config.association_tolerance +
                       g->config.max_speed_per_s * ((double)(f->now_ms - g->last_seen) / 1000.0);
        if (dx * dx + dy * dy > limit * limit)
            return bad(g, GREETER_TRACK_JUMP);
    }
    g->have_point = true;
    g->last_x = x;
    g->last_y = y;
    g->last_seen = f->now_ms;
    pos = g->config.axis == GREETER_X ? x : y;
    side = pos < g->config.line_a - g->config.hysteresis
               ? -1
               : (pos > g->config.line_b + g->config.hysteresis ? 1 : 0);
    if (!g->origin) {
        if (!side) {
            g->stable_active = false;
            return false;
        }
        stable(g, side, f->now_ms);
        if (f->now_ms - g->stable_since >= g->config.outer_stable_ms) {
            g->origin = side;
            g->stable_active = false;
        }
    } else {
        if (side != g->origin && !g->traversing) {
            g->traversing = true;
            g->traversal_since = f->now_ms;
        }
        if (pos >= g->config.line_a + g->config.hysteresis &&
            pos <= g->config.line_b - g->config.hysteresis)
            g->middle = true;
        if (side == g->origin) {
            stable(g, side, f->now_ms);
            if (f->now_ms - g->stable_since >= g->config.outer_stable_ms) {
                g->middle = false;
                g->traversing = false;
            }
        } else if (!side)
            g->stable_active = false;
        else if (!g->middle) {
            clear_track(g);
            g->have_point = true;
            g->last_x = x;
            g->last_y = y;
            g->last_seen = f->now_ms;
            stable(g, side, f->now_ms);
        } else {
            stable(g, side, f->now_ms);
            if (f->now_ms - g->stable_since >= g->config.outer_stable_ms) {
                bool enter = (g->origin == -1) != g->config.reverse_direction;
                uint64_t *count = enter ? &g->snapshot.in_count : &g->snapshot.out_count;
                if (g->snapshot.last_event_id == UINT64_MAX || *count == UINT64_MAX)
                    return bad(g, GREETER_OVERFLOW);
                ++*count;
                ev->event_id = ++g->snapshot.last_event_id;
                ev->timestamp_ms = f->now_ms;
                ev->type = enter ? GREETER_ENTER : GREETER_EXIT;
                g->origin = side;
                g->middle = false;
                g->traversing = false;
                g->stable_active = false;
                g->snapshot.state = side == -1 ? GREETER_FROM_A : GREETER_FROM_B;
                g->snapshot.candidate_direction = side == -1 ? 1 : -1;
                return true;
            }
        }
    }
    g->snapshot.state =
        g->origin ? (g->origin == -1 ? GREETER_FROM_A : GREETER_FROM_B) : GREETER_WAIT_ORIGIN;
    g->snapshot.candidate_direction = g->origin ? -g->origin : 0;
    return false;
}
void greeter_get_snapshot(const greeter_t *g, greeter_snapshot_t *out) {
    if (g && out)
        *out = g->snapshot;
}
void greeter_reset_counts(greeter_t *g) {
    if (g && g->initialized) {
        g->snapshot.in_count = 0;
        g->snapshot.out_count = 0;
        clear_track(g);
    }
}
void greeter_set_reverse(greeter_t *g, bool reverse) {
    if (g && g->initialized && g->config.reverse_direction != reverse) {
        g->config.reverse_direction = reverse;
        clear_track(g);
    }
}
