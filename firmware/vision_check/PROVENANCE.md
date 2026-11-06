# Source provenance

Base: course materials 高级实验例程2/7.物体检测/代码/object_detection.
Original main: Copyright (C) 2016–2022 Shenzhen Yahboom Tech, Gengyue.
LCD and camera sources retain their existing author/license headers.

Project changes: new diagnostic main; bounded person result extraction with
float clipping before integer conversion; JSONL output; monotonic timestamps;
camera/KPU timeout diagnostics; idempotent camera-buffer allocation.

The original course directory is never edited. detect.kmodel is taken from
the original course directory during local staging, not committed to Git.
This target deliberately contains no counting state machine or voice playback.
