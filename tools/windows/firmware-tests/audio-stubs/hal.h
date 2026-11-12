#ifndef AUDIO_TEST_HAL_H
#define AUDIO_TEST_HAL_H
#include <stdint.h>
#include <stdlib.h>
#define DMAC_CHANNEL3 3
#define FUNC_SPI1_D1 1
#define FUNC_SPI1_SCLK 2
#define FUNC_SPI1_D0 3
#define FUNC_GPIOHS7 4
#define FUNC_I2S2_WS 5
#define FUNC_I2S2_OUT_D1 6
#define FUNC_I2S2_SCLK 7
#define SYSCTL_PLL2 2
#define I2S_DEVICE_2 2
#define I2S_TRANSMITTER 0
#define I2S_CHANNEL_1 1
#define RESOLUTION_16_BIT 16
#define SCLK_CYCLES_32 32
#define TRIGGER_LEVEL_4 4
#define RIGHT_JUSTIFYING_MODE 2
void fpioa_set_function(int pin,int function);
void sysctl_pll_set_freq(int pll,unsigned long hz);
void i2s_init(int dev,int mode,int mask);
void i2s_tx_channel_config(int dev,int channel,int resolution,int cycles,int trigger,int mode);
void i2s_set_sample_rate(int dev,unsigned rate);
void i2s_send_data_dma(int dev,const void *buffer,size_t words,int channel);
int dmac_is_done(int channel);
void dmac_channel_disable(int channel);
void *iomem_malloc(size_t n);
void iomem_free(void *p);
#endif
