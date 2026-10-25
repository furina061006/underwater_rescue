> 返回 [AGENTS.md](../../AGENTS.md) ｜ 状态：【事实】+【设计】+【待验证】
> 本文件是 agent 摘要；控制律、消息序列和超时的权威定义在源码与 [docs/RUNTIME_SEQUENCE.md](../../docs/RUNTIME_SEQUENCE.md)，冲突时以源码为准。
> 相关：[架构详解](../../docs/ARCHITECTURE.md)、[硬件接口](../../docs/HARDWARE_IO.md)、[操作手册](../../docs/OPERATIONS.md)、[backlog](../../backlog.md)

# 控制子系统

## 1. 文件与职责

| 文件 | 职责 |
| --- | --- |
| src/control/rescue_control.cpp | 串口、MAVLink、预检、50 Hz 循环、收尾、CSV |
| src/control/pillar_ram_logic.hpp | 搜索/对准/轻撞/冷却/事件/重试/避让 |

## 2. 控制边界

【事实】

- 只操作 CH10（鱼尾）；不操作 CH11 云台，不使用 CH12 灯。
- 不解锁、不切模式、不发 RC override、不写航点/深度/航向。
- 飞控串口 115200、raw、独占（flock + TIOCEXCL）。
- 控制器以 cwd=runtime 启动，状态文件和恢复记录都在该目录。

## 3. 预检与收尾

【事实】预检顺序：

1. 等 ArduSub 心跳（最多 6 s），确认未解锁。
2. 若存在旧 route_recovery.txt，拒绝启动。
3. 读取 CH10 功能/MNT1_TYPE/SERVO_RATE/MIN/MAX。
4. 校验范围覆盖调参文件 pwm_min/pwm_max、SERVO_RATE=50、当前输出 0、功能 0 或 7。
5. 写 rescue_recovery.txt（O_CREAT+O_EXCL，fsync）。
6. 必要时把 SERVO10_FUNCTION 改为 0，CH10 回 0 并遥测确认。
7. 静置 0.3 s 后进入 50 Hz 循环。

【事实】收尾顺序：CH10 回 0 -> 恢复 SERVO10_FUNCTION -> 只有 CH10=0 且功能恢复确认后才删除恢复记录 -> 写 CSV。

【设计】任何时候都不要为了“让程序能启动”删掉 rescue_recovery.txt 或旧 route_recovery.txt。先读操作手册，确认 CH10 已回 0、功能已恢复。

## 4. 救援状态机

【事实】

- SEARCH：扫描正弦 + 巡航摆动；found 且 width_frac≥0.02 -> ALIGN。
- ALIGN：按 x 误差转向；居中且宽度达 trigger 持续 0.1 s -> RAM；近距离丢目标但 last_width 达标可盲撞。
- RAM：默认 ±100 us、5 Hz、0.6 s；允许 found=0。
- RECOVER：输出 0，0.6 s。
- VERIFY：输出 0，0.5 s；目标消失/宽度变化>30%/x 变化>15% -> 命中；命中回 SEARCH，未命中重试，超过 retry_max 后 AVOID。
- AVOID：转向 1.5 s，再 SEARCH。
- 所有输出钳位到 pwm_min/pwm_max（默认 1200–1800）。

【待验证】0.5 kg 支柱在默认轻触下是否可撞倒；事件阈值是否适合真实水池。

## 5. MAVLink 交互

【事实】只使用以下交互：

| 方向 | 消息 | 用途 |
| --- | --- | --- |
| FC->控制器 | HEARTBEAT | 识别 ArduSub、未解锁、心跳看门狗 |
| 双向 | PARAM_REQUEST_READ / PARAM_VALUE | 预检参数 |
| 控制器->FC | PARAM_SET | 临时改 SERVO10_FUNCTION，随后恢复 |
| 控制器->FC | COMMAND_LONG(DO_SET_SERVO) | CH10 PWM |
| 控制器->FC | COMMAND_LONG(REQUEST_MESSAGE) | 请求 SERVO_OUTPUT_RAW |
| FC->控制器 | COMMAND_ACK | 命令确认/拒绝 |
| FC->控制器 | SERVO_OUTPUT_RAW | CH10 实际输出遥测 |

【事实】控制器自身 system/component 为 245/190；不发送 arm/mode/RC/继电器。

## 6. 已知限制

【事实】本版本删除了旧巡线的 CH12 灯、GapLamp、STATUSTEXT 显示和 route 状态机。

【待验证】启动校验依赖调参文件 pwm_min/pwm_max；若现场改窄，需要同步改 rescue_tuning.txt。

【待验证】本版本没有在目标机运行控制器，MAVLink/ACK/50 Hz 时序未重新实测。

## 7. 修改控制时的规则

【设计】

1. 任何控制律/阈值/时序变更默认视为冻结区变更，先获得人类明确需求。
2. 修改 PillarRamLogic 必须同步更新 tests/cpp 和 [架构详解](../../docs/ARCHITECTURE.md)。
3. 不得新增执行机构或消息类型（尤其是 arm/mode/RC/CH11/CH12）。
4. 不得绕过串口独占、心跳检查、未解锁检查、恢复记录或 300 s 上限。
5. 不得在未获授权时运行控制器、连接飞控或发送任何 MAVLink 消息；先读 [hardware-safety](hardware-safety.md)。
