#include "counting_app.h"
int main(void) {
    if (!counting_app_init()) return 1;
    counting_app_set_reverse(true);
    if (!counting_app_config()->reverse_direction) return 2;
    counting_app_reset_counts();
    if (counting_app_snapshot()->in_count || counting_app_snapshot()->out_count) return 3;
    if (!counting_app_config()->reverse_direction) return 4;
    counting_app_set_reverse(false);
    return counting_app_config()->reverse_direction ? 5 : 0;
}
