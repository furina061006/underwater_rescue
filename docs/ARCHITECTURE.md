# 架构说明

本文描述 `winner_fish` 结构化版本的进程、模块、数据契约和控制状态机。运行顺序与硬件时序见
[RUNTIME_SEQUENCE.md](RUNTIME_SEQUENCE.md)。

## 1. 范围与约束

- 目标平台：Jetson ARM64 + ArduSub / Pixhawk6X 水下机器人。
- 控制对象：仅 CH10（鱼尾）与 CH12（继电器灯）。CH11 云台不读不写。
- 不发送：解锁、模式切换、RC override、航点、深度/航向指令。
- 单次任务上限 300 s；`--duration` 允许 2–300 s。
- 视觉状态过期、飞控解锁、心跳丢失、ACK 超时、调度超时都会中止 CH10 输出。
- 本版本未在硬件上重新验证；算法与阈值保持冻结备份原样。

## 2. 进程拓扑

```text
┌────────────────────────────────────────────────────────────────────┐
│ run_wide_search.py（前台启动器）                                    │
│  flock(runtime/route_launcher.lock)                                │
│  ├── 子进程 1：src/vision/wide_search_vision.py                    │
│  │      ├── 前视相机线程/主循环                                    │
│  │      ├── 下视相机读取线程                                       │
│  │      ├── OpenCV 四画面窗口                                      │
│  │      └── 录制器                                                  │
│  └── 子进程 2：bin/wide_search_control                              │
│         ├── MAVLink 串口（独占）                                    │
│         ├── 50 Hz 控制循环                                          │
│         └── 日志/CSV                                                │
└────────────────────────────────────────────────────────────────────┘
```

进程间不共享内存，只通过 `runtime/` 下的文本状态文件交换数据。所有状态发布使用
“写 `.tmp` + rename”，避免读到半行；消费者用 0.5 s 新鲜度窗口判断有效性。

| 进程 | 入口 | 主要职责 | 生命周期 |
| --- | --- | --- | --- |
| 启动器 | `run_wide_search.py` | 单实例锁、环境准备、拉起/回收视觉与控制器、汇总日志 | 操作者 Ctrl+C 或控制器结束 |
| 视觉进程 | `src/vision/wide_search_vision.py` | 相机采集、阈值、底线、上视线拟合、曲线、断口、状态发布、预览、录制 | 被启动器终止或 Q/Esc |
| 控制器 | `src/control/wide_search_control.cpp` | 串口/MAVLink、预检、50 Hz 控制、灯、收尾、日志 | 时长到期、异常或 SIGINT |

视觉进程内部还有两个执行流：

- 主线程：前视采集、阈值、状态发布、下视结果消费、绘制与录制；
- `DownCamera` 读取线程：只负责持续读取下视相机，保存“最新帧 + `time.monotonic()` 时间戳”。

## 3. 模块职责

| 文件 | 职责 |
| --- | --- |
| `src/control/wide_search_control.cpp` | 串口与 MAVLink、参数核验、恢复记录、50 Hz 主循环、灯控制、收尾、CSV |
| `src/control/wide_search_logic.hpp` | 纯逻辑：前视三段 PWM、下视左弧确认、固定 2 s 转弯状态机 |
| `src/control/gap_light_logic.hpp` | 纯逻辑：断带事件到 0.5 s 亮灯的边沿/超时状态机 |
| `src/vision/wide_search_vision.py` | 前视 ROI/阈值/底线/上视线，下视状态发布，四画面和录制主循环 |
| `src/vision/down_curve.py` | 下视二值图连通域、二次曲线拟合、三段航向角、左弧判定 |
| `src/vision/gap_light.py` | 断口条带配对、几何筛选、`GapEvent` 去抖 |
| `src/vision/route_recording.py` | `annotated.avi`、`frames.csv`、转弯关键帧、录像 README |
| `run_wide_search.py` | 默认预览；`--run` 时等待新鲜前视数据后启动控制器 |
| `Makefile` | 控制器构建、C++/Python 测试、SHA-256 清单 |

“纯逻辑”头文件不依赖 OpenCV、MAVLink 或硬件，因此可以在 x86 开发机上直接编译测试。

## 4. 数据契约

目录由 `WIDE_SEARCH_RUNTIME` 决定，默认 `<repo>/runtime`。

| 文件 | 写入者 | 格式 | 语义 |
| --- | --- | --- | --- |
| `wide_search_state.txt` | 视觉 | `stamp x horizontal any\n` | `stamp` 为 `time.monotonic()` 秒；`x∈[-1,1]`，-1 表示无底线；后两项为 0/1 |
| `down_curve_state.txt` | 视觉 | `stamp left strength\n` | `stamp=0` 表示下视不新鲜；`left` 0/1；`strength∈[0,1]` |
| `gap_light_state.txt` | 视觉 | `stamp event\n` | `stamp=0` 表示不新鲜；`event` 单调递增的断带确认计数 |
| `wide_search_status.txt` | 控制器 | `stamp stage pwm elapsed\n` | 供预览叠加阶段、PWM、已运行时间 |
| `route_recovery.txt` | 控制器 | 三行文本 | `SERVO10_FUNCTION`、`SERVO10_PWM`、`SERVO_RATE` 的恢复快照 |
| `route_launcher.lock` | 启动器 | 空 | `flock` 单实例锁 |
| `wide_search_<ts>.csv` | 控制器 | 见下 | 50 Hz 采样与延迟 |
| `route_video_<ts>/frames.csv` | 视觉 | 见下 | 每帧真实时间戳、阶段、PWM、检测值 |
| `route_video_<ts>/down_curve.csv` | 视觉 | 见下 | 下视拟合中间量 |

新鲜度规则：

- 前视状态：`0 ≤ now - stamp ≤ 0.5 s`，且 `x` 有限、在 [-1,1]；否则控制器抛出 `Front vision stale/invalid`。
- 下视状态：`stamp > 0`、`stamp ≤ now`、`now - stamp ≤ 0.5 s`、`strength∈[0,1]`；否则视为不新鲜。
- 灯事件：同样 0.5 s 新鲜窗口；不新鲜时 `GapLamp` 会关闭已亮的灯。
- 控制状态回显：视觉只在 0.5 s 内读取，否则显示 `PREVIEW ONLY`。

控制器 CSV 表头：

```text
time_s,lateness_ms,target_us,telemetry_us,telemetry_seq
```

视觉 `frames.csv` 表头：

```text
frame,capture_monotonic,write_monotonic,stage,pwm,route_elapsed_s,bottom_x_fraction,horizontal,angle_degrees
```

视觉 `down_curve.csv` 表头：

```text
front_stamp,down_stamp,left,strength,offset,curvature_per_px,heading_change_deg,near_deg,middle_deg,far_deg
```

## 5. 视觉管线

### 5.1 前视

1. `cap.read()` 之前先取 `time.monotonic()`，避免阻塞读取把旧帧误判为新鲜；读取失败直接终止。
2. 裁剪横向 20%–100% 作为 ROI；只使用 ROI 的下半部分做上视线拟合。
3. HSV 阈值：`S ≤ S_max`、`V ≥ V_min`（默认 60 / 220，滑条可调）。导引线是低饱和高亮目标。
4. 底线：最大连通域（面积 ≥ 40 px）的最底部 5 行，取这些像素 `x` 的中位数，除以 ROI 宽度得到 `x`；没有合格连通域时 `x=-1`。
5. 上视线（仅显示）：对 ROI 下半部轮廓做 Huber `fitLine`，要求 `|angle| ≤ 70°`、水平跨度 ≥ ROI 宽度 30%、长短边比 ≥ 3；选择跨度最大者。
6. `any_line`：任意面积 ≥ 40 且长边 ≥ 画面高度 20%、长短边比 ≥ 3 的轮廓。
7. 发布 `wide_search_state.txt`：前视时间戳、`x`、`horizontal`、`any`。
8. 控制逻辑实际只使用 `x` 与下视状态；`horizontal/any` 是保留的视觉线索。

### 5.2 下视曲线与左弧

`down_curve.py` 在 640×480 下视二值图上：

1. 选择面积 ≥ 80 px、高度 ≥ 画面 30% 的最大连通域。
2. 每 3 行取该行像素 `x` 中位数，得到 `(y, x)` 序列。
3. `z=(y_max-y)/(y_max-y_min)`：0 为画面底部，1 为顶部；二次拟合 `x(z)`。
4. 用残差中位数做一次鲁棒剔除并重拟合；RMS 残差需 ≤ `max(8 px, 2.5% 画宽)`。
5. 分近/中/远三段求 `dx/dz·span` 的航向角（度），`delta = far - near`。
6. `offset = (x(1)-x(0))/width`，`curvature` 为图像平面有符号曲率（`px⁻¹`，不是物理半径）。
7. 左弧条件：`delta ≤ -12°` 且 `offset ≤ -0.03` 且 `curvature < 0`。
8. `strength = clamp(-offset/0.5, 0, 1)`，仅在左弧成立时非零。
9. 发布 `<down_stamp> <left> <strength>`；下视不新鲜时时间戳写 0。

### 5.3 断口检测

`gap_light.py`：

1. 找到面积 ≥ 40 的轮廓；强条带要求长边 ≥ 画面高度 8%、长短边比 ≥ 3。
2. 允许触边短片段：触边且长边 ≥ 画面高度 2.5% 也算候选。
3. 每个候选拟合直线方向；任意两条候选若能配对：
   - 方向夹角 ≤ 15°；
   - 横向间距 ≤ `max(6 px, (w1+w2)/2)`；
   - 弱片段横向宽度与强条带相近；
   - 沿轴向一个在另一个之前，断口距离 8 px–45% 画面高度；
   - 断口采样点中至少 80% 位于背景。
4. 返回两个端点，供预览画线。
5. `GapEvent`：连续检测 0.1 s 后 `event++`；需要连续 0.5 s 未检测到才重新武装。事件计数通过文件传给控制器。

### 5.4 录制与预览

- 四画面：前视原图、前视二值图、下视原图、下视二值图。
- 叠加：绿色上视线、红色底点、蓝色 45%/55% 分界线、阶段/PWM/检测值。
- `annotated.avi` 按 20 fps 标称写入；真实帧时刻在 `frames.csv`。
- 首次观察到 stage=2 时创建 `turn_1/`，保留触发前 500/100 ms、触发帧、后 100/500 ms；这是“首次看到该阶段的相机帧”，不是舵机物理到位反馈。
- 第 30 帧写 `four_panel_verified.jpg`。
- 下视相机不可用时，预览显示占位图；下视状态时间戳为 0，控制器按“不新鲜”处理。

## 6. 控制管线

### 6.1 串口与链路

`Link` 负责：

- `open(O_RDWR|O_NOCTTY|O_NONBLOCK)`；
- `flock(LOCK_EX|LOCK_NB)` + `TIOCEXCL`，独占串口；
- 保存并恢复原 `termios`，配置为 115200 8N1 raw；
- 解析 MAVLink v2；仅接受 `compid=1` 且 `autopilot=ArduPilotMega`、`type=Submarine` 的心跳对应系统；
- 维护心跳时间、解锁标志、参数缓存/序号、命令 ACK、`SERVO_OUTPUT_RAW`、`STATUSTEXT`；`STATUSTEXT` 当前只入队，主循环未调用 `print_notices()`，因此不会显示；
- `require_idle()`：心跳 < 2.5 s 且未解锁，否则抛异常；
- `parameter()`：最多 3 次读取，每次 1 s；
- `function()`：最多 3 次 `PARAM_SET`，1 s 内等待回读一致；
- `pwm()`：`DO_SET_SERVO` 后 1.5 s 内等待 ACK 且结果为 `MAV_RESULT_ACCEPTED`；
- `verify_output()`：请求 `SERVO_OUTPUT_RAW`，等待 port 0 的 `servo10_raw` 等于期望值，超时 1.5 s。

### 6.2 预检顺序

控制器在改变任何执行机构之前依次完成：

1. 打开串口并等待 ArduSub 心跳（最多 6 s）；
2. `require_idle()`：未解锁、心跳新鲜；
3. 校验 CH12 参数：`SERVO12_FUNCTION=-1`、`RELAY1_FUNCTION=1`、`RELAY1_PIN=53`、`RELAY1_DEFAULT=0`；
4. 发 `DO_SET_RELAY(0,0)` 初始关灯，1 s 内等 ACK；
5. 读取当前 `gap_light_state.txt` 的 event，作为亮灯基线；
6. 读取 `SERVO10_FUNCTION`、`MNT1_TYPE`、`SERVO_RATE`；
7. 校验 `SERVO10_MIN ≤ 1200`、`SERVO10_MAX ≥ 1800`、`SERVO_RATE=50`；
8. 请求 `SERVO_OUTPUT_RAW`，确认 CH10 当前输出 0；功能值必须是 0 或 7；若为 7，`MNT1_TYPE` 必须为 0；
9. 以 `O_CREAT|O_EXCL` 创建 `route_recovery.txt` 并 `fsync`，写入原始功能值、PWM 0、`SERVO_RATE`；
10. 若原功能非 0，先 `PARAM_SET SERVO10_FUNCTION=0` 并回读确认；
11. `DO_SET_SERVO(10,0)` 并等待遥测确认 0；
12. 静置 0.3 s 后进入 50 Hz 主循环。

### 6.3 50 Hz 主循环

每个周期 20 ms：

1. 以 `start + tick*0.02` 计算绝对截止时间；
2. 在等待期间 `poll` 串口（最多 2 ms），处理 MAVLink；每次循环都 `require_idle()`；
3. 检查命令拒绝与 ACK 停滞（0.3 s）；
4. 判断调度延迟：>60 ms 直接中止；错过 ≥20 ms 则跳帧并重同步；
5. 读取前视/下视状态，调用 `HorizontalLossRoute::step()` 计算 PWM；
6. 写 `wide_search_status.txt`（原子 rename）；
7. 调用 `lamp_update()` 处理断带灯；
8. 发 `DO_SET_SERVO(10,value)`；
9. 记录 `Sample{time, lateness, target, telemetry, seq}`；
10. 每 100 ms 请求一次 `SERVO_OUTPUT_RAW`；
11. 时长到达后退出循环。

主循环**不等待**舵机 ACK 再发下一条；ACK 由串口泵异步收集并做超时/拒绝检查。

### 6.4 收尾顺序

无论正常到期还是异常：

1. 关灯 `DO_SET_RELAY(0,0)`，1 s 内等 ACK，打印 `LAMP_OFF_CONFIRMED`；
2. `DO_SET_SERVO(10,0)` 并校验遥测为 0；
3. 恢复 `SERVO10_FUNCTION` 原值并回读确认；
4. 只有“CH10 确认为 0”且“功能恢复确认”同时成立才删除 `route_recovery.txt`；
   关灯失败只把返回码置 1，不参与恢复记录的删除条件；
5. 写 CSV 并打印 `SAMPLES / SKIPPED / MAX_LATENESS_MS / INTERVAL_* / LOG`；
6. 返回 0（正常）或 1（异常）。

## 7. 状态机

### 7.1 前视/转弯：`HorizontalLossRoute`

```text
stage=1（巡线）
  ├── 前视 x<0            -> 输出 0
  ├── 0≤x<0.45            -> 1300..1400 + 100·sin(2π·5t)
  ├── 0.45≤x≤0.55         -> 1500 + 100·sin(2π·4t)
  ├── x>0.55              -> 1600..1700 + 100·sin(2π·5t)
  └── 下视 left 连续 0.1s -> stage=2，turns++，turn_until=now+2s
stage=2（固定 2 s 转弯）
  ├── now<turn_until      -> center 850..900 自适应 + 100·sin(2π·5(now-turn_start))
  └── now≥turn_until      -> stage=1，ready=false（等非左弧样本重新武装）
```

任何时刻前视状态超过 0.5 s 都抛异常；stage=2 时下视状态超过 0.5 s 也抛异常。

### 7.2 灯：`GapLamp`

```text
off --(event > seen 且状态新鲜)--> on，记录 until=now+0.5s
on  --(now≥until 或状态不新鲜)--> off，seen=event
off --(event < seen)-----------> seen=event（兼容事件计数回退）
```

### 7.3 控制器生命周期

```text
START -> LINK_WAIT(6s) -> PREFLIGHT -> RECOVERY_WRITE -> NEUTRALIZE
      -> RUN(≤300s) -> STOP -> CLEANUP -> REPORT -> EXIT
任何阶段异常 -> 已进入执行机构阶段则走 CLEANUP；未进入则打印 ERROR 退出
```

## 8. 时序、并发与实时性

- 控制器主循环目标 50 Hz（20 ms），使用单调时钟；实际采样间隔写入 CSV 的 `INTERVAL_*_MS`。
- 视觉主循环按相机帧率运行；下视线程持续覆盖“最新帧”，主循环只取最新快照。
- 状态文件为跨进程唯一通道；写入用临时文件 + `rename`，读取方不做锁。
- 前视/下视 0.5 s 新鲜窗口是控制安全的硬边界。
- 灯命令不使用队列；同一时刻最多一个继电器命令在途，0.3 s ACK 超时。
- 录制在主线程中完成，可能增加视觉帧耗时，但不直接阻塞控制器（控制器只看状态文件新鲜度）。

## 9. 故障传播

| 故障 | 检测点 | 结果 |
| --- | --- | --- |
| 前视断流/相机崩溃 | `route.step` 时间戳检查 | 控制器中止并收尾 |
| 下视断流（转弯中） | `route.step` | 立即中止；转弯中不允许盲转 |
| 下视断流（非转弯） | `fresh=false` | 取消候选、关灯、继续巡线 |
| 飞控解锁 | 每周期 `require_idle` | 中止并收尾 |
| 心跳丢失 >2.5 s | `require_idle` | 中止并收尾 |
| 串口断开 | `poll` 错误 | 异常退出；恢复记录可能保留 |
| 舵机命令拒绝/ACK 超时 | 拒绝计数/0.3 s 停滞 | 中止并收尾 |
| 调度延迟 >60 ms | 主循环 | 中止并收尾 |
| 继电器 ACK 超时 | `lamp_update` / `lamp_off` | 中止并收尾 |
| 控制器被 kill -9 / 掉电 | 无 | 无收尾；恢复记录保留，需人工恢复 |
| 启动器被 kill -9 | 无 | 控制器/视觉可能成为孤儿；靠控制器 300 s 上限和人工检查 |

## 10. 设计不变量

1. 除 CH10/CH12 外，不向飞控写任何执行机构。
2. 改变 CH10 功能前必须存在恢复记录。
3. 恢复记录只在 CH10=0 且功能恢复确认后删除；关灯失败会置返回码 1，但不阻止删除。
4. 控制器只接受新鲜的视觉状态；不拿旧帧继续输出。
5. 状态文件发布必须原子。
6. 时间上限、未解锁、串口独占三项保护不可绕过（除非改代码）。
7. 云台 CH11 完全由人工负责。

## 11. 扩展点

- **相机路径**：`WIDE_SEARCH_FRONT_CAMERA`、`WIDE_SEARCH_DOWN_CAMERA`。
- **运行时目录**：`WIDE_SEARCH_RUNTIME`。
- **串口**：直接运行控制器时用 `--port`。
- **时长**：`run_wide_search.py --run --duration N`（2–300）。
- **阈值**：前视/下视 HSV 滑条仅在当次运行生效，不持久化；其余几何阈值在 Python 源码中。
- **控制律**：在 `wide_search_logic.hpp` / `gap_light_logic.hpp` 中修改，并同步补充 `tests/cpp`。
- **离线验证**：控制器 `--dry-run --duration N` 不打开串口，只按 50 Hz 读取状态文件并写 `wide_search_status.txt` / CSV，可用于状态文件和逻辑联调（不会写恢复记录，也不会操作硬件）。
