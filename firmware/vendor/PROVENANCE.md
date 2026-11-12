# Third-party provenance

- `lvgl/src`, `lvgl/lvgl.h`, `lvgl/README.md`, `lv_conf.h` copied from the supplied course `资料/高级实验例程1/2.lvgl图形化实验/程序源码/gui_lvgl`. Header version: LVGL 6.1.1. Source preserved unchanged.
- The course copy omitted the license file. `lvgl/LICENCE.txt` retrieved from https://raw.githubusercontent.com/lvgl/lvgl/v6.1.1/LICENCE.txt on 2026-11-12; MIT, Copyright (c) 2016 Gábor Kiss-Vámosi.
- `../audio_sd` copied from `资料/高级实验例程2/4.K210录音机/程序源码/record_play/bsp_sdcard`. FatFs copyright/license notices remain in source. Configuration changed to read-only FAT, short filenames; unused code-page source omitted. Disk reads use the polling sector API. SD retry counters and busy wait corrected in the project copy.
- `../vision_count/touch_port.c` uses the course FT6236 address, registers and coordinate mapping, with bounded polling transactions and corrected 239-y limit. `audio_player.c` uses the course Yahboom speaker pin and I2S channel setup; parsing/preloading and DMA lifecycle are new implementation.
- Original course materials are unchanged. No course demo main, timer-driven GUI handler, recording implementation or model binary is vendored here.
