# winner_fish backlog

> 只列未完成/未知事项；完成即删除。每条写清楚：缺什么、谁能提供、阻塞什么。
> 相关规则：[AGENTS.md](AGENTS.md) ｜ 硬件安全：[.dsh/agents/hardware-safety.md](.dsh/agents/hardware-safety.md)

## 阻塞“首次上机 / 下水”

### 1. 实际 `SERVO10_MIN` / `SERVO10_MAX` 与转弯低端 PWM 未确认

- **缺什么**：目标机实际的 CH10 输出范围，以及 750–1000 us 的左弧转弯输出是否会被飞控限幅。
- **谁能提供**：能连接目标机 QGC/参数页的现场负责人。
- **阻塞什么**：所有依赖左弧转弯的下水验证；在确认前不能声称转弯输出安全。
- **参考**：[docs/HARDWARE_IO.md](docs/HARDWARE_IO.md)、[.dsh/agents/control.md](.dsh/agents/control.md)。

### 2. 目标机相机/飞控 `by-id` 路径未确认

- **缺什么**：两台相机和 Pixhawk6X 在当前 Jetson 上的实际 `/dev/v4l/by-id/`、`/dev/serial/by-id/` 路径。
- **谁能提供**：能登录目标机执行 `ls -l /dev/v4l/by-id/ /dev/serial/by-id/` 的现场人员。
- **阻塞什么**：启动器/视觉/控制器的默认设备路径是否可用；不确认就只能停留在静态检查。
- **参考**：[.dsh/agents/operations.md](.dsh/agents/operations.md)。

### 3. 全链路硬件验证未执行

- **缺什么**：在目标机上完成一次受控预览、短时长 `--run`、CH10/CH12 响应和收尾验证。
- **谁能提供**：现场操作者 + 安全观察员，按整机 SOP 执行。
- **阻塞什么**：任何“本版本已验证”的结论；当前只能确认静态结构和纯逻辑测试。
- **参考**：[.dsh/agents/hardware-safety.md](.dsh/agents/hardware-safety.md)。

## 阻塞“修改后的验证”

### 4. 目标机视觉/控制构建环境未验证

- **缺什么**：Jetson 上的 OpenCV/NumPy 可用性、MAVLink 头文件位置、`make control` 和 `make test` 的实际结果。
- **谁能提供**：目标机开发者；开发机的 `aquacore` 只作为候选环境，未验证完整性。
- **阻塞什么**：视觉算法改动、控制器重新编译和任何依赖运行结果的修改。
- **参考**：[.dsh/agents/operations.md](.dsh/agents/operations.md)。

### 5. `STATUSTEXT` 只解析不显示

- **缺什么**：决定是否在主循环中调用 `print_notices()`，以及调用位置/频率。
- **谁能提供**：控制子系统负责人；需要评估日志噪声和实时性影响。
- **阻塞什么**：依赖飞控文本消息的现场排障；目前只能靠心跳/ACK/日志判断。
- **参考**：[.dsh/agents/control.md](.dsh/agents/control.md)、[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

## 阻塞“版本管理”

### 6. 分支/提交工作流尚未决定

- **缺什么**：是否采用工作分支、何时合并、谁批准提交。
- **谁能提供**：仓库所有者（人类）。
- **阻塞什么**：安全地提交本次 AGENTS/.dsh 变更，以及在共享 checkout 中避免误改 `main`。
- **参考**：[AGENTS.md](AGENTS.md) 第 2 节第 7 条。
