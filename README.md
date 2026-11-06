# K210 双向客流统计与语音迎宾系统

云端首轮可移植 C 代码已实现。主机检查不代表 K210 编译或实物通过。

- 协作基线：[接口约定 v0.2](docs/接口约定.md)。
- 开发范围：[两人开发计划](docs/两人开发计划.md)。
- 实际代码、构建、精确边界与板端接入：[云端代码接入说明](docs/云端代码接入说明.md)。
- 公开头文件：core/greeter.h、core/audio_queue.h；桌面审阅后按 SHA 冻结。
- 云端 cloud/core 负责 core、tests、tools/host；桌面 desktop/firmware 负责 firmware 与板端集成。

```sh
cmake -S . -B build/host
cmake --build build/host -j2
ctest --test-dir build/host --output-on-failure
python3 tools/host/replay.py tests/data/synthetic_roundtrip.jsonl
```

要求 C99 编译器、CMake >=3.16、Python3。core 不依赖 Python 或 SDK；Python 仅用于主机 JSONL 回放测试。示例数据为 synthetic。

首版支持单人依次通过、必须有中间观测的双向穿越、防停留重复、折返、短漏检、多人歧义处理与有界语音队列。实际模型、LCD、音频驱动、机位标定和固件发布由桌面负责。

历史《三小时双人开发计划》是早期工位占用选题，已被当前迎宾方案替代，不作为开发规范。
