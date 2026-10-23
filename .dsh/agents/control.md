> 返回 [AGENTS.md](../../AGENTS.md) ｜ 状态：【事实】+【设计】+【待验证】
> 本文件是 agent 摘要；控制律、消息序列和超时的权威定义在源码与 [docs/RUNTIME_SEQUENCE.md](../../docs/RUNTIME_SEQUENCE.md)，冲突时以源码为准。
> 相关：[运行顺序](../../docs/RUNTIME_SEQUENCE.md)、[硬件接口](../../docs/HARDWARE_IO.md)、[操作手册](../../docs/OPERATIONS.md)、[backlog](../../backlog.md)

# 控制子系统

## 1. 文件与职责

| 文件 | 职责 |
| --- | --- |
| `src/control/wide_search_control.cpp` | 串口、MAVLink、预检、50 Hz 循环、灯、收尾、CSV |
| `src/control/wide_search_logic.hpp` | 前视三段巡线、下视左弧 0.1 s 确认、固定 2 s 转弯、重新武装 |
| `src/control/gap_light_logic.hpp` | 断带事件→亮灯 0.5 s→超时/失联关灯 |

## 2. 控制边界

【事实】

- 只操作 CH10（鱼尾）和 CH12（继电器灯）。
- CH11 云台不读、不写、不输出；起飞前由人工设为 1800。
- 不解锁、不切模式、不发 RC override、不写航点/深度/航向。
- 飞控串口 115200、raw、独占（flock + TIOCEXCL）。
- 控制器以 `cwd=runtime` 启动，状态文件和恢复记录都在该目录。

## 3. 预检与收尾

【事实】预检顺序：

1. 等 ArduSub 心跳（最多 6 s），确认未解锁。
2. 校验 CH12 四个参数，初始关灯。
3. 校验 CH10 功能/范围/50 Hz/当前输出 0。
4. 写 `route_recovery.txt`（`O_CREAT` + `O_EXCL`，fsync）。
5. 必要时把 `SERVO10_FUNCTION` 改为 0，CH10 回 0 并遥测确认。
6. 静置 0.3 s 后进入 50 Hz 循环。

【事实】收尾顺序：CH10 回 0 → 关灯 → 恢复 `SERVO10_FUNCTION` → 只有 CH10=0 且功能恢复确认后才删除恢复记录 → 写 CSV。

【设计】任何时候都不要为了“让程序能启动”删掉 `route_recovery.txt`。先读操作手册，确认 CH10 已回 0、功能已恢复。

## 4. 两个状态机

### HorizontalLossRoute

【事实】

- stage=1：前视巡线；`x<0` 输出 0；左/中/右三段分别在 1300–1400、1500、1600–1700 附近叠加 5/4/5 Hz 正弦。
- 下视 `left` 连续 0.1 s：进入 stage=2，固定 2 s 转弯，5 Hz，中心 850–900 自适应，±100。
- stage=2 结束回 stage=1，需要先看到非左弧样本才重新武装。
- 前视状态超过 0.5 s 抛异常；stage=2 时下视超过 0.5 s 也抛异常。

### GapLamp

【事实】

- 视觉事件计数增加 → 亮灯 0.5 s。
- 灯亮期间数据不新鲜或 0.5 s 到期 → 关灯。
- 同一时刻最多一个继电器命令在途；运行中 ACK 超时 0.3 s。

## 5. MAVLink 交互

【事实】只使用以下交互：

| 方向 | 消息 | 用途 |
| --- | --- | --- |
| FC→控制器 | `HEARTBEAT` | 识别 ArduSub、未解锁、心跳看门狗 |
| 双向 | `PARAM_REQUEST_READ` / `PARAM_VALUE` | 预检参数 |
| 控制器→FC | `PARAM_SET` | 临时改 `SERVO10_FUNCTION`，随后恢复 |
| 控制器→FC | `COMMAND_LONG(DO_SET_RELAY)` | CH12 灯 0/1 |
| 控制器→FC | `COMMAND_LONG(DO_SET_SERVO)` | CH10 PWM |
| 控制器→FC | `COMMAND_LONG(REQUEST_MESSAGE)` | 请求 `SERVO_OUTPUT_RAW` |
| FC→控制器 | `COMMAND_ACK` | 命令确认/拒绝 |
| FC→控制器 | `SERVO_OUTPUT_RAW` | CH10 实际输出遥测 |

【事实】控制器自身 system/component 为 `245/190`；不发送解锁、模式、RC override 或云台命令。

## 6. 已知限制

【事实】`STATUSTEXT` 会解析入队，但主循环没有调用 `print_notices()`，所以飞控文本消息不会显示。详见 [backlog](../../backlog.md)。

【待验证】启动校验只覆盖前进段范围；左弧转弯低端 PWM 是否被 `SERVO10_MIN` 限幅未确认。详见 [backlog](../../backlog.md)。

【待验证】本版本没有在目标机运行控制器，MAVLink/ACK/50 Hz 时序未重新实测。详见 [backlog](../../backlog.md)。

## 7. 修改控制时的规则

【设计】

1. 任何控制律/阈值/时序变更默认视为冻结区变更，先获得人类明确需求。
2. 修改 `HorizontalLossRoute` / `GapLamp` 必须同步更新 `tests/cpp` 和 [架构详解](../../docs/ARCHITECTURE.md)。
3. 不得新增执行机构或消息类型（尤其是 arm/mode/RC/CH11）。
4. 不得绕过串口独占、心跳检查、未解锁检查、恢复记录或 300 s 上限。
5. 不得在未获授权时运行控制器、连接飞控或发送任何 MAVLink 消息；先读 [hardware-safety](hardware-safety.md)。
