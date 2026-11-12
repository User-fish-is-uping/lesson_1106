# 摄像头与 LVGL 双页面 v0.5

v0.5 将原来只显示状态面板的计数固件改为两个可触摸切换的页面。该说明描述源码行为；主机模拟通过不代表实板显示方向、颜色和帧率已经验收。

## 摄像头页（默认）

- 显示完整 320×240 RGB565 摄像头帧。
- 黄色竖线为 A 线，绿色竖线为 B 线；位置来自实际 greeter 配置。
- 青色矩形为 KPU/region_layer 返回的检测框。
- 左上显示 IN/OUT，右上显示 core 状态和检测数量。
- 右下 `Panel` 切换到 LVGL 状态页。

界面把捕获完成的 DVP 显示帧复制到 153600 字节的稳定 Canvas 缓冲，再绘制线和框。下一帧采集可以覆盖原 DVP 缓冲，但不会撕裂 LVGL 当前帧。

## LVGL 状态页

- 显示 IN/OUT、WAIT_ORIGIN/FROM_A/FROM_B/LOST/AMBIGUOUS、检测人数和 ENTER 方向。
- 显示音频状态、人员位置 P、帧间隔 DT、core 错误 E 和触摸错误 T。
- `Reset`：清计数、半程候选和待播队列，事件 ID 保持连续。
- `Reverse`：切换进入方向，保留历史计数；实际换向清半程候选和待播队列。
- `Mute/Unmute`：静音时仍计数但不排队，恢复后不补播。
- `Camera`：返回实时摄像头页。

## 构建与烧录

在包含课程资料、K210 SDK、交叉工具链和 detect.kmodel 的 Windows 环境运行：

```powershell
& '.\tools\windows\test-counter.ps1'
& '.\tools\windows\build-vision.ps1' -Target vision-count
```

正式产物应为 `dist/vision-count/greeter-count-v0.5.bin`，并同时保存同目录的 manifest.json、SHA256SUMS.txt 和 config.json。只有 manifest 的 source_dirty=false 且 source_commit 对应本次提交时才用于烧录。

烧录后先检查摄像头页的方向、RGB565 颜色、A/B 线和人员框，再进入 Panel 测试四个按钮。串口为 115200、8N1、无流控；保存 boot/config/frame/state/control/event/audio 原始日志。
