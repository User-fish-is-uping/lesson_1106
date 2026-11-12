/* Real LVGL + app, with only physical display/touch/audio replaced. */
#include "ui_lvgl.h"
#include "counting_app.h"
#include "touch_port.h"
#include "audio_player.h"
#include "lcd.h"
#include "st7789.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint16_t screen[320*240];
static uint16_t camera_frame[320*240];
static unsigned x1,y1,x2,y2;
static bool pressed;
static uint16_t tx,ty;
static uint64_t time_ms;
void touch_port_init(void) {}
uint32_t touch_port_errors(void) { return 0; }
bool touch_port_read(uint16_t *x, uint16_t *y) { *x=tx; *y=ty; return pressed; }
const char *audio_player_status(void) { return "READY"; }
static audio_start_result_t start(greeter_event_type_t type, uint64_t now)
{ (void)type; (void)now; return AUDIO_STARTED; }
void lcd_set_area(uint16_t a,uint16_t b,uint16_t c,uint16_t d)
{ x1=a; y1=b; x2=c; y2=d; if(c>=320 || d>=240 || a>c || b>d) exit(20); }
void tft_write_half(uint16_t *pixels,uint32_t n)
{
    if(n != (x2-x1+1)*(y2-y1+1)) exit(21);
    unsigned i=0;
    for(unsigned y=y1;y<=y2;y++) for(unsigned x=x1;x<=x2;x++) screen[y*320+x]=pixels[i++];
}
static void pump(void)
{ for(unsigned i=0;i<30;i++) ui_lvgl_service(time_ms+=5); }
static void click(unsigned x)
{ tx=x; ty=210; pressed=true; pump(); pressed=false; pump(); }
static void save(const char *path)
{
    FILE *f=fopen(path,"wb"); if(!f) exit(22);
    fprintf(f,"P6\n320 240\n255\n");
    for(unsigned i=0;i<320*240;i++) {
        uint16_t p=screen[i]; unsigned char rgb[3]={(p>>11)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};
        fwrite(rgb,1,3,f);
    }
    fclose(f);
}
int main(void)
{
    if(!counting_app_init()) return 1;
    counting_app_attach_audio(start); counting_app_set_muted(false);
    ui_lvgl_init(0); pump(); save("ui-camera-empty.ppm");
    click(280); save("ui-panel.ppm");
    click(120); if(!counting_app_config()->reverse_direction) return 2;
    click(120); if(counting_app_config()->reverse_direction) return 3;
    click(200); if(!counting_app_muted()) return 4;
    click(200); if(counting_app_muted()) return 5;
    obj_info_t obj; memset(&obj,0,sizeof(obj)); obj.obj_number=1;
    obj.obj[0].class_id=14; obj.obj[0].prob=.9f; obj.obj[0].y1=30; obj.obj[0].y2=200;
    unsigned points[]={64,64,64,160,255,255,255};
    for(unsigned i=0;i<7;i++) {
        obj.obj[0].x1=points[i]-20; obj.obj[0].x2=points[i]+20;
        time_ms+=150; counting_app_process(&obj,i+1,time_ms,time_ms,150);
    }
    if(counting_app_snapshot()->in_count!=1) return 6;
    for(unsigned i=0;i<320*240;i++) camera_frame[i]=(uint16_t)(0x001f+(i&0x1f));
    ui_lvgl_update(&obj,150,camera_frame); pump();
    click(40);
    if(counting_app_snapshot()->in_count || counting_app_snapshot()->out_count ||
       counting_app_snapshot()->state!=GREETER_WAIT_ORIGIN) return 7;
    click(280); pump(); save("ui-camera-live.ppm");
    if(screen[100*320+100] != camera_frame[100*320+100]) return 8;
    if(screen[100*320+112] == camera_frame[100*320+112]) return 9;
    if(screen[100*320+235] == camera_frame[100*320+235]) return 10;
    click(280); pump(); save("ui-panel-after.ppm");
    return 0;
}
