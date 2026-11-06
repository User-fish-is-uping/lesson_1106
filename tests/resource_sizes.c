#include "audio_queue.h"
#include "greeter.h"
#include <stdio.h>
int main(void) {
    printf("host ABI bytes: greeter_t=%zu audio_queue_t=%zu greeter_frame_t=%zu "
           "greeter_config_t=%zu greeter_snapshot_t=%zu greeter_event_t=%zu\n",
           sizeof(greeter_t), sizeof(audio_queue_t), sizeof(greeter_frame_t),
           sizeof(greeter_config_t), sizeof(greeter_snapshot_t), sizeof(greeter_event_t));
    return 0;
}
