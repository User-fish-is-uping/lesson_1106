#ifndef GREETER_TOUCH_PORT_H
#define GREETER_TOUCH_PORT_H
#include <stdbool.h>
#include <stdint.h>
void touch_port_init(void);
bool touch_port_read(uint16_t *x, uint16_t *y);
uint32_t touch_port_errors(void);
#endif
