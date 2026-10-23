# 验证记录 / Validation

- 原生 C++17，LLVM-MinGW 20261020，静态链接；EXE 273408 bytes，小于 1 MB。
- 原生命令接口 `--set` / `--status`，无 JSON 配置和 JSON 解析依赖。
- 中英文界面，默认随系统；日期显示使用系统本地时区。
- 日期、命令更新、状态序列化、大小限制、链接校验与双语倒计时测试通过；AddressSanitizer / UndefinedBehaviorSanitizer 检查通过。
- 原布局保留：296 × 32 / 圆角 13 / 展开高度 270 / 260ms 动画。
- Windows 界面与注册表状态保存尚待实机验证。

Native C++17; static runtime linking. Direct `--set` / `--status` commands replace JSON configuration. Chinese and English interfaces follow the system language by default; dates use the local time zone. Core tests pass with ASan/UBSan. Windows rendering and registry persistence still require device testing.
