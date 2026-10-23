# 2026-10-23 建立 AGENTS 知识体系

## 目标

让新会话在**不启动硬件**的前提下正确理解本项目，尤其是：

- 默认只做静态阅读/编辑；
- 不运行启动器、相机、控制器、QGC/MAVProxy；
- 不碰恢复记录和运行数据；
- 不擅自改控制参数或提交 git。

## 做了什么

1. 阅读项目入口 [README.md](../../README.md)、[docs/ARCHITECTURE.md](../../docs/ARCHITECTURE.md)、[docs/RUNTIME_SEQUENCE.md](../../docs/RUNTIME_SEQUENCE.md)、[docs/HARDWARE_IO.md](../../docs/HARDWARE_IO.md)、[docs/OPERATIONS.md](../../docs/OPERATIONS.md) 和核心源码。
2. 创建根索引 [AGENTS.md](../../AGENTS.md)，包含红线、默认工作方式、路由表、结构表和知识约定。
3. 创建主题文档：
   - [.dsh/agents/hardware-safety.md](../agents/hardware-safety.md)
   - [.dsh/agents/architecture.md](../agents/architecture.md)
   - [.dsh/agents/vision.md](../agents/vision.md)
   - [.dsh/agents/control.md](../agents/control.md)
   - [.dsh/agents/operations.md](../agents/operations.md)
4. 创建 [backlog.md](../../backlog.md)，记录硬件验证、目标机环境、`SERVO10_MIN`、`STATUSTEXT` 和 git 工作流等待办。
5. 创建 [.dsh/tools/check_docs.py](../tools/check_docs.py)，检查根文件、主题路由和相对链接。
6. 更新 [.gitignore](../../.gitignore)，补充 `AGENTS.local.md`、`CLAUDE.local.md`、编辑器配置和运行数据说明。
7. 在 [README.md](../../README.md) 增加 Agent 知识库入口；用 `make checksums` 重算 `SHA256SUMS.txt`（35 个文件）并校验。

## 证据与结构决策

- 三进程结构来自 `run_wide_search.py`、`src/vision/wide_search_vision.py` 和 `src/control/wide_search_control.cpp` 的实际启动关系；详细数据契约在 `docs/ARCHITECTURE.md`，主题文档只保留 agent 路由和约束，避免复制整张数值表。
- 红线来自代码实际行为：`Link` 独占串口、`require_idle()` 拒绝解锁、`route_recovery.txt` 阻止 CH10 接管、控制器只操作 CH10/CH12。
- 选择 `AGENTS.md` + `.dsh/` 的披露式结构，而不是把所有事实塞进一个文件；根索引约 80 行，便于每会话完整加载。
- 本会话**没有**运行启动器、视觉、控制器、QGC 或任何硬件；**没有**执行 git commit / push / 切分支。
- 本会话只做了文档和知识库结构写入；先前 Doxygen 注释轮次执行过 `make test-cpp` 和 Python AST 检查，未运行视觉测试。

## 当时发现的仓库状态

- `git` 仓库位于项目根目录，当前分支 `main`，基线提交 `25a7688`（"最开始的稳定版本"）。
- 工作区有未提交改动：Doxygen 注释、分析文档更新、`SHA256SUMS.txt` 等。
- 没有远端；分支/提交工作流尚未与人类确认，因此本会话不执行任何 git 写操作。
- 开发机默认 `python3` 缺 `cv2`，NumPy 不可用，MAVLink 头文件不存在；conda 环境 `aquacore` 未验证。

## 有意留到以后

- 在目标机上确认 `SERVO10_MIN/MAX` 与左弧转弯低端 PWM 是否限幅。
- 确认目标机相机/飞控 `by-id` 路径。
- 在 Jetson 上验证 OpenCV/NumPy/MAVLink 构建与视觉测试。
- 决定是否修复 `STATUSTEXT` 只入队不显示的问题。
- 与仓库所有者确认分支/提交工作流。
