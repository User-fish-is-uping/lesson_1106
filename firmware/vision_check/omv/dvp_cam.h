#ifndef _DVP_CAM_H_
#define _DVP_CAM_H_

#include <stdint.h>

#define CAM_WIDTH_PIXEL        (320)
#define CAM_HIGHT_PIXEL        (240)

#define OVXXXX_ADDR    0x60 
#define OV9655_PID_1   0x9657   
#define OV9655_PID_2   0x9656  
#define OV2640_PID     0x2642

enum OV_type
{
    OV_error = 0,
    OV_9655 = 1,
    OV_2640 = 2,
};


extern uint8_t *g_ai_buf_in, *g_ai_od_buf;
extern uint32_t g_ai_red_buf_addr, g_ai_green_buf_addr, g_ai_blue_buf_addr, g_ai_od_buf_addr;

extern uint32_t display_buf_addr;
extern volatile uint8_t g_dvp_finish_flag;


void dvp_cam_init(void);
void dvp_cam_set_irq(void);
int OVxxxx_read_id(void);

#endif /* _DVP_CAM_H_ */
