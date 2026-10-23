# winner_fish

> 水下机器人视觉巡线 / 宽域搜索控制程序  
> 2025-10-22 冻结备份的结构化版本。本次整理主要改变目录布局、运行时路径和日志文案，另做了少量无行为影响的清理（未使用变量/函数、测试路径引导）；**控制算法、阈值与时序未改**。整理过程未启动硬件。

## 1. 项目简介

`winner_fish` 运行在 Jetson（ARM64）上，面向 ArduSub / Pixhawk6X 水下机器人：

- **前视相机**提取画面下方的导引线，按线在画面中的横向位置生成鱼尾摆动 PWM，经 MAVLink 以 **50 Hz** 发给 **CH10** 舵机。
- **下视相机**拟合导引线曲率；确认持续左弧后，执行一次 **固定 2 秒** 的 5 Hz 转弯搜索。
- 下视相机同时检测导引线**断口**；确认断带后通过 **CH12 继电器**点亮指示灯 **0.5 秒**。
- 全程只发 `DO_SET_SERVO`、`DO_SET_RELAY`、参数读写和遥测请求；**不解锁、不切模式、不发 RC override**。

数据流：

```text
前视相机 ──> wide_search_vision.py ──> wide_search_state.txt ──┐
下视相机 ──> down_curve.py / gap_light.py ──> 状态文件 ─────────┼──> wide_search_control (50 Hz)
                                                               │          │
                                                               │          ├── MAVLink ──> Pixhawk6X ──> CH10 鱼尾舵机
                                                               │          └── MAVLink ──> Pixhawk6X ──> CH12 继电器灯
                                                               └── wide_search_status.txt <── 控制器状态回显
```

## 2. 功能一览

| 功能 | 行为 |
| --- | --- |
| 前视巡线 | 取前视画面右侧 80%（横向 20%–100%），按底部线的横向位置分三段输出 PWM，叠加 ±100 us 正弦摆动 |
| 前视角度拟合 | 在 ROI 下半画面拟合线段，仅用于预览显示，不参与控制判定 |
| 下视左弧转弯 | 负曲率、方向变化 ≤ -12°、左偏超过画宽 3%，连续 0.1 s 确认；随后固定 2 s、5 Hz 转弯 |
| 断带亮灯 | 配对断口两侧条带，断口 8 px 至画面高度 45%；确认 0.1 s 后亮灯 0.5 s；断口消失 0.5 s 后才允许再次触发 |
| 四画面预览 | 前视原图 / 前视二值图 / 下视原图 / 下视二值图，叠加检测结果、阶段和 PWM |
| 录制 | 录制带标注的四画面 AVI、逐帧 CSV、下视曲线 CSV，并保留每次转弯前后的关键帧 |
| 安全联锁 | 串口独占、心跳看门狗、只允许未解锁、恢复记录、断带灯超时关闭、300 s 硬上限 |

## 3. 硬件接口

| 硬件 | 设备 / 参数 | 程序行为 |
| --- | --- | --- |
| 主控 | Jetson ARM64，Python 3 + OpenCV + NumPy | 运行视觉进程和启动器；控制器为 C++11 |
| 前视相机 | `/dev/v4l/by-id/usb-H264_USB_Camera_H264_USB_Camera_2020032801-video-index0` | V4L2，640×480，只读 |
| 下视相机 | `/dev/v4l/by-id/usb-HD_Camera_Manufacturer_USB_2.0_Camera-video-index0` | V4L2 + MJPG，640×480，只读 |
| 飞控 | `/dev/serial/by-id/usb-ArduPilot_Pixhawk6X-bdshot_20001F001551333234343538-if00`，115200 | 独占串口，MAVLink v2，ArduSub |
| CH10 鱼尾 | `SERVO10_FUNCTION` 为 0（或 7 且 `MNT1_TYPE=0`）；`SERVO_RATE=50`；范围需覆盖 1200–1800 us，转弯还会到 750–1000 us | 怠速 0，运行中 50 Hz 输出 |
| CH11 云台 | 起飞前由人工设为 1800 | **程序不读取、不修改、不输出** |
| CH12 灯 | `SERVO12_FUNCTION=-1`，`RELAY1_FUNCTION=1`，`RELAY1_PIN=53`，`RELAY1_DEFAULT=0` | `DO_SET_RELAY` 实例 0，值 0/1 |

相机设备路径可用环境变量覆盖：

```bash
export WIDE_SEARCH_FRONT_CAMERA=/dev/video0
export WIDE_SEARCH_DOWN_CAMERA=/dev/video2
export WIDE_SEARCH_RUNTIME=/path/to/runtime   # 默认 <repo>/runtime
```

串口路径默认写死在控制器中；直接运行控制器时可用 `--port` 覆盖。

更完整的接线、参数和 PWM 语义见 [docs/HARDWARE_IO.md](docs/HARDWARE_IO.md)。

## 4. 目录结构

```text
winner_fish/
├── README.md                     # 本文件
├── Makefile                      # 构建、逻辑测试、校验和
├── run_wide_search.py            # 唯一启动入口
├── SHA256SUMS.txt                # 当前目录结构的完整性清单（make checksums）
├── src/
│   ├── control/                  # C++ 控制器与纯逻辑头文件
│   │   ├── wide_search_control.cpp
│   │   ├── wide_search_logic.hpp # 前视巡线 + 下视左弧转弯状态机
│   │   └── gap_light_logic.hpp   # 断带亮灯状态机
│   └── vision/                   # Python 视觉管线
│       ├── wide_search_vision.py # 主循环、四画面、状态发布
│       ├── route_recording.py    # AVI / CSV / 关键帧录制
│       ├── down_curve.py         # 下视曲线拟合与左弧判定
│       └── gap_light.py          # 断口配对与事件去抖
├── tests/
│   ├── cpp/                      # 不依赖硬件的 C++ 逻辑测试
│   └── python/                   # 依赖 OpenCV/NumPy 的视觉算法测试
├── bin/
│   └── wide_search_control       # 备份时的 ARM64 预编译控制器
├── runtime/                      # 运行时状态、日志、录像、恢复记录（生成）
├── docs/
│   ├── ARCHITECTURE.md           # 架构与模块职责
│   ├── RUNTIME_SEQUENCE.md       # 程序运行与硬件交互全顺序
│   ├── HARDWARE_IO.md            # 硬件接口与参数
│   ├── OPERATIONS.md             # 操作、排障、恢复流程
│   └── legacy/                   # 重构前原始说明与校验清单
└── build/                        # 编译中间产物（生成）
```

运行时目录（默认 `runtime/`）中会生成：

| 文件 | 生产者 → 消费者 | 内容 |
| --- | --- | --- |
| `wide_search_state.txt` | 视觉 → 控制器 | `front_stamp bottom_x horizontal any` |
| `down_curve_state.txt` | 视觉 → 控制器 | `down_stamp left strength` |
| `gap_light_state.txt` | 视觉 → 控制器 | `down_stamp event_counter` |
| `wide_search_status.txt` | 控制器 → 视觉 | `stamp stage pwm elapsed` |
| `route_recovery.txt` | 控制器 | 恢复记录；存在时预检中止，不接管 CH10 |
| `route_launcher.lock` | 启动器 | 单实例文件锁 |
| `wide_search_<时间>.log` / `.csv` | 控制器 | 控制台日志 / 50 Hz 采样 |
| `route_video_<时间>/` | 视觉 | `annotated.avi`、`frames.csv`、`down_curve.csv`、转弯关键帧 |

所有状态文件都通过“写临时文件 + rename”原子发布；新鲜度窗口为 0.5 s。

## 5. 控制逻辑速览

### 5.1 前视巡线（CH10）

设 `x` 为前视 ROI 内底部线中心的横向比例（0=左，1=右）：

| 条件 | 输出 |
| --- | --- |
| `x < 0`（无底部线） | `0` |
| `0 ≤ x < 0.45` | `round(1300 + 100·x/0.45 + 100·sin(2π·5·t))` |
| `0.45 ≤ x ≤ 0.55` | `round(1500 + 100·sin(2π·4·t))` |
| `x > 0.55` | `round(1600 + 100·(x-0.55)/0.45 + 100·sin(2π·5·t))` |

其中 `t` 为本次运行已进行的秒数。输出再经飞控映射到 CH10。

### 5.2 下视左弧转弯

同时满足以下条件，且持续 **0.1 s**，进入转弯：

- 远端与近端航向变化 `delta ≤ -12°`；
- 曲线横向偏移 `offset ≤ -0.03`（左偏超过画宽 3%）；
- 曲率 `curvature < 0`。

转弯固定 **2 s**、**5 Hz**，中心在 850–900 us 之间按左弧强度自适应（最大变化率 600 us/s），叠加 ±100 us。转弯期间不依赖前视 `x`；下视数据一旦超过 0.5 s 不新鲜即中止。转弯结束后，需要先看到非左弧样本才会重新武装，避免同一段持续左弧重复触发。

### 5.3 断带亮灯（CH12）

- 对下视二值图中的条带做两两配对，允许画面边缘的短片段；
- 断口长度必须在 **8 px 至画面高度 45%** 之间；
- 连续确认 **0.1 s** 后产生一次灯事件，亮灯 **0.5 s**；
- 断口消失并保持 **0.5 s** 后才允许下一次触发；
- 下视数据超过 0.5 s 不新鲜时，控制器会主动关灯。

## 6. 程序运行顺序（摘要）

完整的分阶段说明、MAVLink 消息和异常路径见
**[docs/RUNTIME_SEQUENCE.md](docs/RUNTIME_SEQUENCE.md)**。摘要如下：

1. **人工准备**：上电、确认未解锁、CH10/CH12 参数正确、云台 1800、灯灭、设备节点存在、无恢复记录。
2. **启动器**：获取 `runtime/route_launcher.lock`，状态归零，启动视觉进程。
3. **视觉**：打开前视/下视相机，创建四画面和录像，持续发布前视、下视、断带状态。
4. **等待新鲜前视**：启动器确认状态年龄 <0.5 s（最多 10 s），再启动控制器。
5. **建链**：控制器独占飞控串口（115200），等待 ArduSub 心跳（最多 6 s），确认未解锁。
6. **预检**：校验 CH12 四个参数并初始关灯；校验 CH10 功能/范围/50 Hz/当前输出 0。
7. **接管 CH10**：写 `route_recovery.txt` → `SERVO10_FUNCTION=0` → `DO_SET_SERVO(10,0)` → 静置 0.3 s。
8. **50 Hz 主循环**：每 20 ms 读状态 → 巡线/2 s 左弧转弯/断带灯 → `DO_SET_SERVO(10,PWM)`；每 100 ms 请求遥测。
9. **正常收尾**：CH10 回 0 → 关灯 → 恢复 `SERVO10_FUNCTION` → 仅在 CH10=0 且功能恢复确认后删除恢复记录 → 写 CSV。
10. **回收**：启动器终止视觉、重置状态、释放锁、打印日志；人工确认后断电。

## 7. 快速开始

### 依赖

- 目标机：Jetson ARM64，Ubuntu（ROS Noetic 环境），Python 3、OpenCV、NumPy；
- 编译控制器：`g++`、C++11、ArduPilot MAVLink v2 头文件（通常来自 `/opt/ros/noetic/include`）；
- 显示：有效的 `DISPLAY` 与 `XAUTHORITY`（视觉程序会创建 OpenCV 窗口）。

### 构建控制器

```bash
cd winner_fish
make control
# 如果 MAVLink 头文件不在 /opt/ros/noetic/include：
make control MAVLINK_INC=/path/to/include
```

仓库内自带的 `bin/wide_search_control` 是备份时的 **ARM64** 二进制，可在目标机直接使用；在 x86 开发机上不能运行，需要交叉编译或在目标机重建。若从压缩包解压后执行位丢失：

```bash
chmod +x run_wide_search.py bin/wide_search_control
```

### 只预览（不下发控制）

```bash
python3 run_wide_search.py
```

- 显示四画面预览；
- 按 `Q` 或 `Esc` 退出；
- 不会启动控制器，不会操作舵机或灯。

### 正式运行（硬件在线）

```bash
python3 run_wide_search.py --run
# 可选：缩短时长用于台架验证（2..300 秒）
python3 run_wide_search.py --run --duration 30
```

启动器会先等待前视状态新鲜，再启动控制器；控制器完成预检后才使能 CH10。默认运行 300 秒。

### 停止

- 在启动器终端按 `Ctrl+C`：启动器给控制器发 `SIGINT`，控制器执行关灯、CH10 回 0、恢复 `SERVO10_FUNCTION`，然后退出；
- 控制器自身也响应 `SIGINT` / `SIGTERM` / `SIGHUP`；
- 正常结束或异常中止后，都要检查 `STOP=` / `RESTORE_FUNCTION=` / `LAMP_OFF_CONFIRMED` 日志。

### 输出

- `runtime/wide_search_<时间>.log`：控制器完整控制台输出；
- `runtime/wide_search_<时间>.csv`：每个 20 ms 周期的目标 PWM、遥测 PWM、延迟；
- `runtime/route_video_<时间>/annotated.avi`：带标注的四画面录像（标称 20 fps，真实时间戳在 `frames.csv`）；
- `runtime/latest_route_recording.txt`：最近一次录像目录；
- `runtime/four_panel_verified.jpg`：第 30 帧的四画面快照。

## 8. 安全机制

1. **只控制 CH10 + CH12**：不切模式、不解锁、不发 RC override。
2. **未解锁才运行**：每个控制周期检查心跳；一旦检测到解锁、心跳超过 2.5 s 未更新，立即中止并进入收尾。
3. **串口独占**：`flock` + `TIOCEXCL`，防止其他程序同时占用飞控串口。
4. **恢复记录**：改变 CH10 功能前先写 `route_recovery.txt`；只有确认 CH10 回 0 且功能恢复后才删除。文件存在时预检阶段中止，不会接管 CH10。
5. **失联保护**：视觉状态超过 0.5 s 不新鲜、转弯中下视失联、命令 ACK 超时、调度延迟超过 60 ms，都会中止控制。
6. **时长上限**：单次运行最多 300 s，`--duration` 也只能在 2–300 s 内。
7. **退出收尾**：关灯 → CH10 回 0 并校验 → 恢复 `SERVO10_FUNCTION` → 两项都成立才删除恢复记录 → 写采样报告。
8. **云台隔离**：CH11 仅由人工在起飞前设为 1800，程序不碰。

> 这是实验室/比赛用的半自主控制程序，不能替代驾驶员和安全观察员。任何参数变更后都应先 `preview`，再做短时长台架/水池验证。

## 9. 测试与完整性

纯逻辑测试（无需硬件、无需 OpenCV）：

```bash
make test-cpp
```

视觉算法测试（需要 OpenCV + NumPy）：

```bash
make test-python
make test          # C++ + Python
```

重新生成当前目录结构的校验和：

```bash
make checksums
sha256sum -c SHA256SUMS.txt
```

重构前的平面目录清单保存在 [docs/legacy/SHA256SUMS.flat.txt](docs/legacy/SHA256SUMS.flat.txt)，仅作留档，路径已不适用。

## 10. 文档索引

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)：进程、模块、数据契约、状态机。
- [docs/RUNTIME_SEQUENCE.md](docs/RUNTIME_SEQUENCE.md)：**从启动到断电的完整运行与硬件交互顺序**。
- [docs/HARDWARE_IO.md](docs/HARDWARE_IO.md)：设备、通道、参数、PWM 与控制律。
- [docs/OPERATIONS.md](docs/OPERATIONS.md)：操作清单、排障、断链恢复。
- [docs/legacy/使用说明.txt](docs/legacy/使用说明.txt)：冻结时的原始说明。

## 11. 已知边界

- 相机和飞控都按 `by-id` 路径绑定；设备名变化时需要改代码或设置相机环境变量。
- 控制器只支持单次 300 s 任务，不包含航点、深度/航向闭环或自动上浮。
- 舵机是开环 PWM，没有物理角度反馈；录像中的阶段变化是“检测到并已下发”的帧级对齐，不代表舵机真实到位。
- 下视曲率单位是 `px⁻¹`，不是物理半径。
- 启动校验只要求 `SERVO10_MIN≤1200`、`SERVO10_MAX≥1800`，但转弯输出理论最低 **750 us**；首次验证需确认 `SERVO10_MIN` 不会对 750–1000 us 区间限幅。
- 视觉程序依赖 OpenCV 窗口；无显示环境无法运行（会直接报错）。
- 本整理版本未在硬件上启动验证，只完成静态结构、语法、纯逻辑测试与完整性清单更新。

## 12. Agent 知识库

新会话或自动化代理先读 [AGENTS.md](AGENTS.md)。它包含硬件安全红线、按任务路由到 [.dsh/agents/](.dsh/agents/) 主题文档、目录职责和知识维护约定。

- 默认**不启动硬件**：预览、`--run`、控制器和 QGC/MAVProxy 都需要人类明确授权。
- 未完成和待验证事项集中在 [backlog.md](backlog.md)。
- 历史决策记录在 [.dsh/logs/README.md](.dsh/logs/README.md)。
- 修改知识文档后运行 `python3 .dsh/tools/check_docs.py` 检查链接和结构。
