/* Yahboom I2S2 pin/configuration and course FatFs/SD driver integration.
 * Bounded boot preload replaces the course ISR file reads and shared DMA5. */
#include "audio_player.h"
#include "ff.h"
#include "fpioa.h"
#include "i2s.h"
#include "dmac.h"
#include "iomem.h"
#include "sysctl.h"
#include <stdio.h>
#include <string.h>

#define AUDIO_DMA DMAC_CHANNEL3
#define SAMPLE_RATE 16000u
#define MAX_SAMPLES (SAMPLE_RATE * 3u)
typedef struct { uint32_t *words; unsigned samples; } clip_t;
static clip_t clips[2];
static FATFS fs;
static bool playing, fault;
static uint64_t deadline;
static uint32_t u32(const uint8_t *p)
{ return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static unsigned u16(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }
static bool read_exact(FIL *f, void *buffer, unsigned size)
{ UINT got = 0; return f_read(f, buffer, size, &got) == FR_OK && got == size; }

static bool load_clip(const char *path, clip_t *clip)
{
    FIL file;
    uint8_t header[16], pcm[512];
    bool ok = false, format = false;
    if (f_open(&file, path, FA_READ) != FR_OK) return false;
    if (!read_exact(&file, header, 12) || memcmp(header, "RIFF", 4) ||
        memcmp(header + 8, "WAVE", 4)) goto done;
    uint32_t riff = u32(header + 4);
    if (riff < 4 || (uint64_t)riff + 8 != f_size(&file)) goto done;
    /* Cap the chunk count as well as all offsets and sample allocation. */
    for (unsigned chunk = 0; chunk < 64 && f_tell(&file) + 8 <= f_size(&file); ++chunk) {
        if (!read_exact(&file, header, 8)) goto done;
        uint32_t size = u32(header + 4);
        uint64_t end = (uint64_t)f_tell(&file) + size;
        uint64_t next = end + (size & 1u);
        if (next > f_size(&file)) goto done;
        if (!memcmp(header, "fmt ", 4)) {
            if (size < 16 || !read_exact(&file, header, 16)) goto done;
            if (u16(header) != 1 || u16(header + 2) != 1 || u32(header + 4) != SAMPLE_RATE ||
                u32(header + 8) != SAMPLE_RATE * 2 || u16(header + 12) != 2 ||
                u16(header + 14) != 16) goto done;
            format = true;
        } else if (!memcmp(header, "data", 4)) {
            if (!format || !size || size % 2 || size / 2 > MAX_SAMPLES) goto done;
            clip->samples = size / 2;
            clip->words = iomem_malloc(clip->samples * 2 * sizeof(uint32_t));
            if (!clip->words) goto done;
            unsigned offset = 0;
            while (size) {
                unsigned n = size > sizeof(pcm) ? sizeof(pcm) : size;
                if (!read_exact(&file, pcm, n)) goto done;
                for (unsigned i = 0; i < n; i += 2) {
                    uint32_t sample = u16(pcm + i);
                    clip->words[offset++] = sample;
                    clip->words[offset++] = sample;
                }
                size -= n;
            }
            ok = true;
            goto done;
        }
        if (f_lseek(&file, (FSIZE_t)next) != FR_OK) goto done;
    }
done:
    f_close(&file);
    if (!ok && clip->words) { iomem_free(clip->words); clip->words = NULL; }
    return ok;
}

void audio_player_init(void)
{
    fpioa_set_function(26, FUNC_SPI1_D1);
    fpioa_set_function(27, FUNC_SPI1_SCLK);
    fpioa_set_function(28, FUNC_SPI1_D0);
    fpioa_set_function(29, FUNC_GPIOHS7);
    fpioa_set_function(30, FUNC_I2S2_WS);
    fpioa_set_function(31, FUNC_I2S2_OUT_D1);
    fpioa_set_function(32, FUNC_I2S2_SCLK);
    sysctl_pll_set_freq(SYSCTL_PLL2, 49152000UL);
    i2s_init(I2S_DEVICE_2, I2S_TRANSMITTER, 0xc);
    i2s_tx_channel_config(I2S_DEVICE_2, I2S_CHANNEL_1, RESOLUTION_16_BIT,
                         SCLK_CYCLES_32, TRIGGER_LEVEL_4, RIGHT_JUSTIFYING_MODE);
    i2s_set_sample_rate(I2S_DEVICE_2, SAMPLE_RATE);
    if (f_mount(&fs, "0:", 1) == FR_OK) {
        load_clip("0:/welcome.wav", &clips[0]);
        load_clip("0:/goodbye.wav", &clips[1]);
        f_mount(NULL, "0:", 0);
    }
    printf("{\"kind\":\"audio_init\",\"welcome\":%s,\"goodbye\":%s,\"rate\":16000,\"max_seconds\":3}\n",
           clips[0].words ? "true" : "false", clips[1].words ? "true" : "false");
}

audio_start_result_t audio_player_start(greeter_event_type_t type, uint64_t now_ms)
{
    if (playing) return AUDIO_BUSY;
    if (fault || (type != GREETER_ENTER && type != GREETER_EXIT)) return AUDIO_FAILED;
    clip_t *clip = &clips[type == GREETER_ENTER ? 0 : 1];
    if (!clip->words) return AUDIO_FAILED;
    playing = true;
    deadline = now_ms + clip->samples * 1000u / SAMPLE_RATE + 500;
    i2s_send_data_dma(I2S_DEVICE_2, clip->words, clip->samples * 2, AUDIO_DMA);
    return AUDIO_STARTED;
}
bool audio_player_service(uint64_t now_ms)
{
    if (!playing) return false;
    if (dmac_is_done(AUDIO_DMA)) {
        dmac_channel_disable(AUDIO_DMA);
        playing = false;
        puts("{\"kind\":\"audio_player\",\"status\":\"done\"}");
    } else if (now_ms > deadline) {
        dmac_channel_disable(AUDIO_DMA);
        playing = false;
        fault = true; /* Never restart a timed-out channel until reboot. */
        puts("{\"kind\":\"audio_player\",\"status\":\"timeout\"}");
        return true;
    }
    return false;
}
const char *audio_player_status(void)
{
    if (fault) return "DMA ERROR";
    if (playing) return "PLAYING";
    if (!clips[0].words || !clips[1].words) return "MISSING/BAD WAV";
    return "READY";
}
