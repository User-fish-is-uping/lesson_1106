#ifndef GREETER_UI_LVGL_H
#define GREETER_UI_LVGL_H
#include "region_layer.h"
#include <stdint.h>
void ui_lvgl_init(uint64_t now_ms);
void ui_lvgl_service(uint64_t now_ms);
/* camera_rgb565 is the completed 320x240 DVP display frame. The UI copies it
 * before the next capture, then overlays gates and detections in LVGL. */
void ui_lvgl_update(const obj_info_t *people, uint64_t dt_ms,
                    const uint16_t *camera_rgb565);
#endif
