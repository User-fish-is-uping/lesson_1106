#include "counting_app.h"
#include <string.h>
static unsigned calls;
static audio_start_result_t result;
static audio_start_result_t start(greeter_event_type_t type, uint64_t now)
{ (void)type; (void)now; ++calls; return result; }
static uint64_t now_ms, seq;
static void crossing(void)
{
    obj_info_t obj; memset(&obj,0,sizeof(obj)); obj.obj_number=1;
    obj.obj[0].class_id=14; obj.obj[0].prob=.9f;
    obj.obj[0].y1=30; obj.obj[0].y2=200;
    unsigned points[]={64,64,64,160,255,255,255};
    for(unsigned i=0;i<7;i++) {
        obj.obj[0].x1=points[i]-20; obj.obj[0].x2=points[i]+20;
        now_ms+=150; counting_app_process(&obj,++seq,now_ms,now_ms,150);
    }
}
int main(void) {
    if (!counting_app_init()) return 1;
    counting_app_set_reverse(true);
    if (!counting_app_config()->reverse_direction) return 2;
    counting_app_reset_counts();
    if (counting_app_snapshot()->in_count || counting_app_snapshot()->out_count) return 3;
    if (!counting_app_config()->reverse_direction) return 4;
    counting_app_set_reverse(false);
    if(counting_app_config()->reverse_direction) return 5;
    counting_app_attach_audio(start); counting_app_set_muted(false);
    crossing(); result=AUDIO_BUSY;
    counting_app_service_audio(++now_ms,false);
    if(calls!=1 || counting_app_snapshot()->in_count!=1) return 6;
    counting_app_set_muted(true); counting_app_set_muted(false);
    counting_app_service_audio(++now_ms,false);
    if(calls!=1) return 7; /* Muting discarded the queued event. */
    counting_app_reset_counts(); crossing(); result=AUDIO_FAILED;
    counting_app_service_audio(++now_ms,false);
    counting_app_service_audio(++now_ms,false);
    counting_app_service_audio(++now_ms,false);
    if(calls!=3 || counting_app_snapshot()->in_count!=1) return 8;
    counting_app_reset_counts(); crossing(); result=AUDIO_STARTED;
    counting_app_service_audio(++now_ms,false);
    counting_app_service_audio(++now_ms,true);
    if(calls!=4) return 9; /* Async failure must not replay consumed audio. */
    counting_app_reset_counts(); crossing(); counting_app_set_reverse(true);
    counting_app_service_audio(++now_ms,false);
    if(calls!=4 || counting_app_snapshot()->in_count!=1) return 10;
    return 0;
}
