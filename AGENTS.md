# AGENTS.md — underwater_rescue 项目工作索引

> 每次会话先读本文件；细节按“路由表”按需打开，不要一次加载全部文档。

## 1. 项目与责任边界

【事实】这是一个运行在 Jetson ARM64 上的水中救援程序，飞控为 ArduSub / Pixhawk6X。
【事实】运行时由启动器、Python 前视视觉进程、C++ CH10 控制器组成，通过 runtime/ 文本状态文件解耦。
【事实】控制输出只有 CH10 鱼尾；CH11 云台人工设置，CH12 灯在本项目不使用。
【事实】视觉只用前置摄像头，检测黑色笔筒支柱；不依赖下视、航向、深度或全局定位。
【边界】推进器、深度/航向闭环、压载、漏水、云台、飞控固件、供电和整机安全不在本仓库职责内。
【事实】当前是 git 仓库；工作区可能有未提交改动；分支/提交策略尚未与人类确认。

## 2. 红线（默认不碰硬件）

1. **不启动会碰硬件的程序。** 未经人类明确确认，不执行 python3 run_rescue.py（预览和 --run 都算）、make preview、make run、bin/rescue_control，也不运行任何打开相机、串口或 GUI 的脚本。--dry-run 虽不打开串口，仍是程序运行，默认也不跑。
2. **不发飞控/执行机构命令。** 不 arm/disarm、不改模式、不发 RC override、不调用 DO_SET_SERVO / DO_SET_RELAY / 参数写入，也不通过 QGC、MAVProxy 或 mavlink shell 手动操作。
3. **不碰恢复记录。** 不删除、不改写 runtime/rescue_recovery.txt；旧 runtime/route_recovery.txt 也按未安全收尾处理。存在时先读 operations 并询问人类。
4. **不改已验证的控制/检测参数。** CH10 通道、SERVO_RATE、轻撞档、检测阈值默认不改；必须修改时要有明确需求、影响分析和目标机验证计划。
5. **不删除运行数据。** runtime/ 下的日志、CSV、录像、恢复记录可能是在场证据；只归档、不清理。
6. **不伪造验证结论。** 本机通过只代表静态检查或纯逻辑测试；不得声称视觉、控制器或硬件已经验证。
7. **不自行操作 git。** 不 commit、push、merge、rebase、切分支；先获得人类明确同意。

> 更细的禁止清单、原因和授权后的最小流程：[.dsh/agents/hardware-safety.md](.dsh/agents/hardware-safety.md)。

## 3. 默认工作方式

1. 先读相关代码和文档，再改代码；不要凭记忆补项目事实。
2. 保持 CH10 轻撞行为可解释：不改通道、不碰 CH12、不绕过未解锁/心跳/恢复记录检查。
3. 任何“启动、连接、发送、烧录”的动作都先停下来问人类；默认只做静态编辑和阅读。
4. 需要跑测试时优先 make test-cpp（纯逻辑、不碰硬件）；视觉测试和控制器完整构建需目标机环境。
5. 改完源码后同步更新文档和 Doxygen 注释；SHA256SUMS.txt 只通过 make checksums 生成，不手工编辑。
6. 新增/修改 .md 后运行 python3 .dsh/tools/check_docs.py。

## 4. 路由表

| 我要做的事 | 先读什么 |
| --- | --- |
| 判断某个操作会不会碰硬件、能不能运行 | [.dsh/agents/hardware-safety.md](.dsh/agents/hardware-safety.md) |
| 理解进程、模块、状态文件和整体结构 | [.dsh/agents/architecture.md](.dsh/agents/architecture.md) |
| 修改前视检测、GUI、状态发布或调参 | [.dsh/agents/vision.md](.dsh/agents/vision.md) |
| 修改 CH10 轻撞、事件判定、收尾或 MAVLink | [.dsh/agents/control.md](.dsh/agents/control.md) |
| 构建、运行、停止、排障、恢复、环境与校验 | [.dsh/agents/operations.md](.dsh/agents/operations.md) |
| 查看完整运行与硬件交互顺序 | [docs/RUNTIME_SEQUENCE.md](docs/RUNTIME_SEQUENCE.md) |
| 查看硬件参数、通道和 PWM 语义 | [docs/HARDWARE_IO.md](docs/HARDWARE_IO.md) |
| 查看架构、视觉与控制状态机 | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| 查看比赛原始要求 | [docs/requirement.md](docs/requirement.md) |
| 查看仍未完成或待验证的事项 | [backlog.md](backlog.md) |
| 查看历史决策与踩坑记录 | [.dsh/logs/README.md](.dsh/logs/README.md) |
| 查看人类入口和项目介绍 | [README.md](README.md) |

## 5. 结构表

| 路径 | 职责 |
| --- | --- |
| AGENTS.md | 本索引：红线、路由、结构和知识约定 |
| README.md | 人类入口：项目介绍、结构、快速开始 |
| backlog.md | 已知但未完成/未验证的事项 |
| run_rescue.py | 唯一启动入口；预览和 --run |
| src/control/rescue_control.cpp | 串口/MAVLink/预检/50 Hz/收尾/CSV |
| src/control/pillar_ram_logic.hpp | 纯逻辑：搜索/对准/轻撞/事件/重试 |
| src/vision/rescue_vision.py | 前视采集、GUI、状态发布、录像 |
| src/vision/pillar_detect.py | 黑筒检测、过滤、跟踪 |
| src/vision/pillar_recording.py | 两画面 AVI + CSV |
| tests/cpp/ | 纯逻辑 C++ 测试 |
| tests/python/ | 依赖 OpenCV/NumPy 的视觉测试 |
| docs/ | 架构、时序、硬件、操作和比赛要求 |
| .dsh/agents/ | 按主题拆分的 agent 事实与约束 |
| .dsh/logs/ | 追加式过程记录，不覆盖历史 |
| runtime/ | 运行状态、日志、录像和恢复记录（现场数据） |
| bin/ | 目标机构建产物；仓库不保证自带二进制 |

## 6. 知识约定

- 技术事实的权威来源是 docs/ 和源码；.dsh/agents/ 只保留 agent 需要的路由、约束和程序，不复制完整数值表。
- 在 topic 文档里区分：【事实】（有来源）、【设计】（团队决定）、【推导】（计算结论）、【待验证】（假设，不能直接写进代码）。
- 先更新 .dsh/agents/ 的事实，再追加 .dsh/logs/；日志只追加，不改旧记录。
- backlog.md 只保留未完成项，完成即删；每条写明缺什么、谁能提供、阻塞什么。
- AGENTS.md 必须留在项目根目录并保持约 100 行以内；这是加载约定，不是个人偏好。
