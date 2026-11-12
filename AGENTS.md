# Project collaboration

This is a two-student K210 shop entrance greeter project. Read README.md first. Read docs/接口约定.md before changing public interfaces and docs/开发与集成流程.md for integration procedures.

## Ownership

- Firmware maintainers own firmware/, tools/windows/, and release packaging. It integrates the supplied Kendryte SDK, person detector, LCD, SD card, and I2S playback.
- Core maintainers own core/, tests/, tools/host/, and the host CMakeLists.txt. The core C code must run on a host compiler and K210 without requiring SDK headers in core/.
- Each implementation change includes a concise handoff entry explaining files changed, checks run, and unresolved issues.
- Interface revision is recorded in docs/接口约定.md and shared before integration.

## Validation

- Distinguish host tests, K210 cross-compilation, and physical board verification in status reports.
- Use deterministic frame sequences for crossing, reversal, missed detections, ambiguous people, and playback queue tests.
- Preserve source attribution when adapting course code.
- Hardware thresholds are tunable starting values. Hardware observations determine final values.
- Keep original course materials intact. Implement the project in this directory and use staged SDK copies for firmware builds.

## Scope

The first release handles one person passing at a time. Multiple simultaneous people suspend automatic counting for that ambiguous passage. The system resumes after an unambiguous origin-side observation.

The course requires AI, an LVGL interaction interface, and an innovation point. Use the existing KPU person model plus directional state transitions and ordered voice feedback. Prioritize that complete flow before Wi-Fi, onboard recording, or UI frameworks beyond the required LVGL.
