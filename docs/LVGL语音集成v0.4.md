# LVGL 与语音集成 v0.4（待实板验收）

本轮交付 AI＋双向计数＋真实 LVGL 触摸界面＋TF 音频预载与 I2S 播放的集成代码。主机测试与交叉编译不代表实物已通过。此前用户验证的是计数版本，本版尚未烧录验证。

## 屏幕与操作

320×240 横屏使用课程 LVGL 6.1.1。主页显示 IN/OUT、检测状态、人数、进入方向、静音和播放器状态；Debug 页显示单人位置、帧间隔、状态机错误、触摸通信错误。按钮为英文，以复用课程内置字体，避免缺字。

- Reset：清零计数、清除半程轨迹和待播语音；事件编号继续递增。
- Reverse：切换进入方向、清除半程轨迹和待播语音；历史计数保留。
- Mute / Unmute：静音时清待播，已开始的一句允许播完；恢复不补播。
- Debug / Home：切换调试页和主页。

本版以仪表界面替换全屏摄像头预览，AI 仍使用完整 320×240 输入。旧版的图像与人员框预览可通过 `vision-check` 目标保留，用于机位调试。LVGL 独占正常运行时的 LCD 刷新；没有定时器中断调用 LVGL，也没有中断调用 core。

触摸按课程 FT6236 接线：IO9/10 → I2C0，地址 0x38；轮询读取，不使用触摸中断。坐标映射 x=原始第二坐标，y=239−原始第一坐标，拒绝越界和多点输入。每次总线事务最多等待 5 ms；上板需核对四个按钮与实际触点，若方向不符，只调整触摸变换，不改 AI 坐标。

## TF 卡与音频

使用 FAT32 TF 卡，在根目录放 `welcome.wav`（欢迎光临）与 `goodbye.wav`（谢谢光临，欢迎下次再来）。每段必须为 **PCM、单声道、16 kHz、16 bit、不超过 3 秒**。开机读取一次，转换为 I2S 双声道数据预载；修改文件后需重启。两段最多占用约 768 KB 音频缓冲，内存不足时禁用对应音频，不终止检测。

音频文件需录制并准备，本仓库不包含冒充真人录音的示例音频。可用本机 Python 3.12 转换已有 PCM WAV（超过 3 秒需先裁剪）：

```powershell
python tools/windows/prepare-audio.py welcome-original.wav welcome.wav
python tools/windows/prepare-audio.py goodbye-original.wav goodbye.wav
```

转换工具拒绝覆盖已有文件。播放器检查 RIFF 边界、chunk 长度、格式、数据长度、空音频及内存分配。缺卡、缺文件、格式错误会在 `audio_init` 日志中显示对应加载失败，界面提示 MISSING/BAD WAV。可加载的一段仍可播放；队列启动失败遵循 core 的最多两次尝试，不回滚计数。开机默认音频开启。

播放采用一次 DMA 发送整段短语，不在检测期间读 SD、不在中断中读文件。DMA 超时后停用该播放器直到重启，异步失败不重新排队。尚需实测音量、失真、左右声道和实际播放时长。

## 硬件资源

| 功能 | 分配 |
|---|---|
| 摄像头与 KPU | 原接线，KPU DMA5 |
| LCD | 原 SPI0 接线，DMA0，同步刷新 |
| FT6236 | I2C0，IO9/10，主循环轮询 |
| TF 卡 | SPI1，IO26/27/28，IO29 GPIOHS7 片选；开机用非 DMA 扇区读取 |
| 扬声器 | I2S2 channel1，IO30 WS、31 DATA、32 BCK，DMA3；PLL2 49.152 MHz |

原课程 SD 写入和 DMA 辅助函数保留来源，但本项目 FatFs 只读，实际读取不调用这些 DMA 函数。课程初始化重试计数下溢和写忙无限等待已在项目副本中修正。

## 构建与检查

```powershell
& ./tools/windows/test-counter.ps1
& ./tools/windows/build-vision.ps1 -Target vision-count
```

主机 31/31；板端适配 8/8（其中播放器测试涵盖 12 种输入/失败场景）。LVGL 测试使用真实库和应用层、模拟触点及 LCD，验证 Reset/Reverse/Mute/Unmute、页面切换，并生成 `build/firmware-tests/ui-*.ppm`。音频测试使用真实加载器与模拟 SD/DMA，不代表扬声器测试。

固件位于 `dist/vision-count/greeter-count-v0.4.bin`，同目录包含配置、SHA256 和来源 manifest。烧录沿用已验证的 K210 下载流程，模型已内置，不单独烧模型。串口 115200。最终构建记录见交接记录。

上板顺序：先不插卡，确认启动、AI 计数和按钮；再插入准备好的 TF 卡重启，确认两段 `audio_init` 为 true；分别进出并确认语音；播放时继续过人，检查串口帧序号持续增加；测试半程 Reset/Reverse、连续点击、静音恢复；最后按两人计划记录准确率和 10 分钟连续运行结果。

## 给同学 B 的交接

core 公共类型和源码未改。新增应用层音频回调位于 `counting_app.h`，B 无需实现硬件驱动。

控制日志格式如下，按日志行的发生顺序应用，不能按 frame 的观察时间重新排序（控制可能发生在 KPU 推理期间）：

```json
{"kind":"control","t_ms":1234,"action":"reset"}
{"kind":"control","t_ms":2345,"action":"reverse","value":true}
{"kind":"control","t_ms":3456,"action":"mute","value":true}
```

请 B 扩展 `tools/host/` 回放处理这些记录；当前回放工具尚不能完整复现含交互操作的日志，暂只用无控制操作的片段比较结果。日志提取工具会保留原始 control 行，不删除。
