# AGENTS.md — winner_fish 项目工作索引

> 每次会话先读本文件；细节按“路由表”按需打开，不要一次加载全部文档。

## 1. 项目与责任边界

【事实】这是一个运行在 Jetson ARM64 上的水下机器人视觉巡线/宽域搜索程序，飞控为 ArduSub / Pixhawk6X。
【事实】运行时由启动器、Python 视觉进程、C++ 控制进程组成，通过 `runtime/` 状态文件解耦。
【事实】控制输出仅涉及 CH10 鱼尾和 CH12 断带灯；CH11 云台由人工设置，程序不碰。
【事实】2025-10-22 冻结备份；本次结构化与 Doxygen 注释整理未改变控制律、阈值和时序。
【边界】推进器、深度/航向闭环、压载、漏水、云台、飞控固件、供电和整机安全不在本仓库职责内。
【事实】当前是 git 仓库（`main` 已有基线提交），工作区可能有未提交改动；分支/提交策略尚未与人类确认。

## 2. 红线（默认不碰硬件）

以下规则必须在不打开其他文件的情况下遵守：

1. **不启动会碰硬件的程序。** 未经人类明确确认，不执行 `python3 run_wide_search.py`（预览和 `--run` 都算）、`make preview`、`make run`、`bin/wide_search_control`，也不运行任何打开相机、串口或 GUI 的脚本。`--dry-run` 虽不打开串口，仍是程序运行，默认也不跑。
2. **不发飞控/执行机构命令。** 不 arm/disarm、不改模式、不发 RC override、不调用 `DO_SET_SERVO` / `DO_SET_RELAY` / 参数写入，也不通过 QGC、MAVProxy 或 mavlink shell 手动操作。
3. **不碰恢复记录。** 不删除、不改写 `runtime/route_recovery.txt`；它存在就表示上次未安全收尾，处理前先读 [operations](.dsh/agents/operations.md) 并询问人类。
4. **不改冻结的控制参数。** 控制律、阈值、时序、通道映射（CH10/CH12）、设备路径、`SERVO_*` / `RELAY*` 参数默认不改；必须修改时要有明确需求、影响分析和目标机验证计划。
5. **不删除运行数据。** `runtime/` 下的日志、CSV、录像、恢复记录可能是在场证据；只归档、不清理。
6. **不伪造验证结论。** 本机通过只代表静态检查或纯逻辑测试；不得声称视觉、控制器或硬件已经验证。
7. **不自行操作 git。** 不 commit、push、merge、rebase、切分支；先获得人类明确同意。

> 更细的禁止清单、原因和授权后的最小流程：[.dsh/agents/hardware-safety.md](.dsh/agents/hardware-safety.md)。

## 3. 默认工作方式

1. 先读相关代码和文档，再改代码；不要凭记忆补项目事实。
2. 保持冻结行为：不做与控制无关的大重构，不改已验证的常量和时序。
3. 任何“启动、连接、发送、烧录”的动作都先停下来问人类；默认只做静态编辑和阅读。
4. 需要跑测试时优先 `make test-cpp`（纯逻辑、不碰硬件）；视觉测试和控制器完整构建需目标机环境。
5. 改完源码后同步更新文档和 Doxygen 注释；`SHA256SUMS.txt` 只通过 `make checksums` 生成，不手工编辑。仅知识库/文档变更可直接重算；源码或二进制变更先说明范围并取得人类同意。

## 4. 路由表

| 我要做的事 | 先读什么 |
| --- | --- |
| 判断某个操作会不会碰硬件、能不能运行 | [.dsh/agents/hardware-safety.md](.dsh/agents/hardware-safety.md) |
| 理解进程、模块、状态文件和整体结构 | [.dsh/agents/architecture.md](.dsh/agents/architecture.md) |
| 修改前视、下视、断口、录制或视觉阈值 | [.dsh/agents/vision.md](.dsh/agents/vision.md) |
| 修改巡线、转弯、亮灯、MAVLink 或收尾逻辑 | [.dsh/agents/control.md](.dsh/agents/control.md) |
| 构建、运行、停止、排障、恢复、环境与校验 | [.dsh/agents/operations.md](.dsh/agents/operations.md) |
| 查看完整运行与硬件交互顺序 | [docs/RUNTIME_SEQUENCE.md](docs/RUNTIME_SEQUENCE.md) |
| 查看硬件参数、通道和 PWM 语义 | [docs/HARDWARE_IO.md](docs/HARDWARE_IO.md) |
| 查看仍未完成或待验证的事项 | [backlog.md](backlog.md) |
| 查看历史决策与踩坑记录 | [.dsh/logs/README.md](.dsh/logs/README.md) |
| 查看人类入口和项目介绍 | [README.md](README.md) |
| 查看原始冻结资料 | [docs/legacy/README.md](docs/legacy/README.md) |

## 5. 结构表

| 路径 | 职责 |
| --- | --- |
| `AGENTS.md` | 本索引：红线、路由、结构和知识约定 |
| `README.md` | 人类入口：项目介绍、结构、运行顺序摘要 |
| `backlog.md` | 已知但未完成/未验证的事项 |
| `.gitignore` | 明确哪些运行数据、构建产物和本机配置不进历史 |
| `.dsh/agents/` | 按主题拆分的事实、约束和操作程序 |
| `.dsh/logs/` | 追加式过程记录，不覆盖历史 |
| `.dsh/tools/` | 文档链接和知识库结构检查脚本 |
| `src/control/` | C++ 控制器、MAVLink 交互和控制状态机 |
| `src/vision/` | 相机、视觉算法、状态发布和录像 |
| `tests/cpp/` | 纯逻辑 C++ 测试 |
| `tests/python/` | 依赖 OpenCV/NumPy 的视觉测试 |
| `docs/` | 人类可读的架构、硬件、操作和时序文档 |
| `docs/legacy/` | 重构前的原始说明和校验清单 |
| `bin/` | 预编译控制器（ARM64） |
| `runtime/` | 运行状态、日志、录像和恢复记录（现场数据） |
| `build/` | 本机构建产物 |

## 6. 知识约定

- 技术事实的权威来源是 `docs/` 和源码；`.dsh/agents/` 只保留 agent 需要的路由、约束和程序，不复制完整数值表。
- 在 topic 文档里区分：`【事实】`（有来源）、`【设计】`（团队决定）、`【推导】`（计算结论）、`【待验证】`（假设，不能直接写进代码）。
- 先更新 `.dsh/agents/` 的事实，再追加 `.dsh/logs/`；日志只追加，不改旧记录。
- `backlog.md` 只保留未完成项，完成即删；每条写明缺什么、谁能提供、阻塞什么。
- 编辑知识文档后运行 `python3 .dsh/tools/check_docs.py` 检查链接和结构。
- `AGENTS.md` 必须留在项目根目录并保持约 100 行以内；这是加载约定，不是个人偏好。
