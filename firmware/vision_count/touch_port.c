/* FT6236 register/pin mapping adapted from Yahboom gui_lvgl (2020).
 * Polling only: no LVGL, I2C or application logic runs in an interrupt. */
#include "touch_port.h"
#include "i2c.h"
#include "fpioa.h"
#include "encoding.h"
#include "sysctl.h"
static uint64_t timeout_cycles;
static uint32_t errors;

static bool transfer(uint8_t reg, uint8_t *data, unsigned count, bool read)
{
    volatile i2c_t *p = i2c[I2C_DEVICE_0];
    uint64_t start = read_cycle();
    unsigned sent = 0, received = 0;
    (void)p->clr_tx_abrt;
    while (p->rxflr) (void)p->data_cmd;
    while (sent < count + 1 || (read && received < count) ||
           (p->status & I2C_STATUS_ACTIVITY) || !(p->status & I2C_STATUS_TFE)) {
        if (p->tx_abrt_source || read_cycle() - start > timeout_cycles) {
            ++errors;
            p->enable = 0;
            (void)p->clr_tx_abrt;
            p->enable = 1;
            return false;
        }
        if (sent < count + 1 && p->txflr < 8) {
            p->data_cmd = sent == 0 ? reg : (read ? I2C_DATA_CMD_CMD : data[sent - 1]);
            ++sent;
        }
        if (read && received < count && p->rxflr)
            data[received++] = (uint8_t)p->data_cmd;
    }
    return true;
}

void touch_port_init(void)
{
    fpioa_set_function(9, FUNC_I2C0_SCLK);
    fpioa_set_function(10, FUNC_I2C0_SDA);
    timeout_cycles = sysctl_clock_get_freq(SYSCTL_CLOCK_CPU) / 200; /* 5 ms */
    i2c_init(I2C_DEVICE_0, 0x38, 7, 100000);
    uint8_t value = 0;
    transfer(0x00, &value, 1, false);
    value = 0x12;
    transfer(0x80, &value, 1, false);
    value = 6;
    transfer(0x88, &value, 1, false);
}

bool touch_port_read(uint16_t *x, uint16_t *y)
{
    uint8_t data[5];
    if (!transfer(0x02, data, sizeof(data), true) || (data[0] & 15) != 1)
        return false;
    unsigned event = data[1] >> 6;
    unsigned raw_y = ((data[1] & 15) << 8) | data[2];
    unsigned raw_x = ((data[3] & 15) << 8) | data[4];
    if ((event != 0 && event != 2) || raw_x >= 320 || raw_y >= 240) return false;
    *x = (uint16_t)raw_x;
    *y = (uint16_t)(239 - raw_y);
    return true;
}
uint32_t touch_port_errors(void) { return errors; }
