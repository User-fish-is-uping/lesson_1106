#ifndef _GC2145_H
#define _GC2145_H

#include <stdint.h>
#include "dvp.h"
#include "dvp.h"

#define GC2145_ADDR         (0x78)
#define GC2145_ID           0x2145

#define DCMI_RESET_LOW()      dvp->cmos_cfg &= ~DVP_CMOS_RESET
#define DCMI_RESET_HIGH()     dvp->cmos_cfg |= DVP_CMOS_RESET
#define DCMI_PWDN_LOW()       dvp->cmos_cfg &= ~DVP_CMOS_POWER_DOWN
#define DCMI_PWDN_HIGH()      dvp->cmos_cfg |= DVP_CMOS_POWER_DOWN

int gc2145_init(void);
int gc2145_read_id( uint16_t *device_id);

#endif /* _GC2145_H */