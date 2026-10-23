# 操作、排障与恢复

## 1. 最短操作清单

```bash
cd winner_fish

# 1) 目标机首次构建（需要 MAVLink 头文件）
make control

# 2) 只预览，确认相机、检测和显示
python3 run_wide_search.py
# Q 或 Esc 退出

# 3) 正式运行，默认 300 s
python3 run_wide_search.py --run

# 4) 需要短程验证时（2–300 s）
python3 run_wide_search.py --run --duration 30
```

运行前必须满足 [HARDWARE_IO.md](HARDWARE_IO.md) 中的参数与联锁条件；否则控制器会中止启动流程（不会接管 CH10）。

## 2. 环境检查

```bash
python3 - <<'PY'
import cv2, numpy
print('cv2', cv2.__version__)
print('numpy', numpy.__version__)
PY

echo "DISPLAY=$DISPLAY"
echo "XAUTHORITY=$XAUTHORITY"
ls -l /dev/v4l/by-id/ /dev/serial/by-id/
```

- OpenCV/NumPy 必须能在目标机的 `python3` 中导入；
- 视觉程序会创建窗口，无显示环境会报错；
- 启动器默认 `DISPLAY=:0`，并优先使用项目根目录的 `.preview_xauthority`，否则使用
  `/home/auv8/.Xauthority`；可通过环境变量覆盖。

## 3. 构建

```bash
make control
# MAVLink 头文件不在默认位置时：
make control MAVLINK_INC=/path/to/include
```

默认要求：
```text
/opt/ros/noetic/include/mavlink/v2.0/ardupilotmega/mavlink.h
```

产物：`bin/wide_search_control`。

仓库自带的 `bin/wide_search_control` 是 **ARM64** 二进制（aarch64），不能在 x86 开发机直接运行。
在目标机可以：
- 直接使用备份二进制；或
- `make control` 重新编译。

## 4. 预览

```bash
python3 run_wide_search.py
```

预览只启动视觉进程：

- 打开前视/下视相机；
- 创建四画面窗口；
- 写状态文件和录像；
- **不会打开飞控串口，不会动 CH10/CH12**。

按 Q/Esc 或 Ctrl+C 退出。退出后启动器会把 `runtime/wide_search_state.txt` 重置为
`0 -1 0 0`。

预览检查项：

- 前视二值图能稳定保留导引线；
- 底部红点跟着线走；
- 下视曲线/断口标记合理；
- 四画面没有明显卡顿；
- 状态文件持续更新。

## 5. 正式运行

```bash
python3 run_wide_search.py --run
```

启动器行为：

1. 获取 `runtime/route_launcher.lock`；
2. 启动视觉；
3. 等待前视状态新鲜（最多 10 s）；
4. 启动控制器，控制器完成预检后才会输出 CH10；
5. 控制器运行到时长结束或异常；
6. 回收视觉、重置状态、打印日志。

### 运行中观察

- 终端应持续出现控制器日志（不是每帧都打印）；
- 预览窗口显示 `Stage 1` / `Stage 2`、当前 PWM；
- 第一次进入转弯时日志出现 `DOWN LEFT ARC...`；
- 断带亮灯时日志出现 `CH12_LIGHT=1`，约 0.5 s 后 `CH12_LIGHT=0`；
- 不要同时用 QGC 修改 CH10/CH12 配置或解锁。

## 6. 停止

### 正常到期

300 s（或 `--duration`）到达后自动收尾：

```text
ROUTE_TIME_LIMIT ...
CH12_LIGHT=0 ...
LAMP_OFF_CONFIRMED
STOP=1 RESTORE_FUNCTION=<原值> confirmed=1
SAMPLES=... LOG=...
```

### 手动停止

在启动器终端按 **一次** `Ctrl+C`：

1. 启动器给控制器发 `SIGINT`；
2. 控制器在下一次 `require_idle` 时中止；
3. 控制器关灯、CH10 回 0、恢复功能、删恢复记录、写 CSV；
4. 启动器最多等 30 s 收尾；
5. 启动器终止视觉、重置状态、释放锁。

> 连续多次 Ctrl+C 可能打断启动器的等待，导致控制器被提前终止。收尾期间请耐心等待。

## 7. 输出文件

默认在 `runtime/`：

| 文件 | 内容 |
| --- | --- |
| `wide_search_<ts>.log` | 控制器 stdout/stderr 全文 |
| `wide_search_<ts>.csv` | 50 Hz 采样：时间、延迟、目标 PWM、遥测 PWM、seq |
| `route_video_<ts>/annotated.avi` | 四画面标注录像，标称 20 fps |
| `route_video_<ts>/frames.csv` | 每帧真实采集/写入时间、阶段、PWM、检测值 |
| `route_video_<ts>/down_curve.csv` | 下视曲线中间量 |
| `route_video_<ts>/turn_1/` | 首次观察到 stage=2 前后的关键帧 |
| `four_panel_verified.jpg` | 第 30 帧快照 |
| `latest_route_recording.txt` | 最近录像目录 |
| `route_recovery.txt` | 恢复记录（存在即代表上次未安全收尾） |

CSV 判读：

- `lateness_ms`：相对 20 ms 截止时间的延迟；
- `target_us`：控制器计算的目标 PWM；
- `telemetry_us`：`SERVO_OUTPUT_RAW` 中 CH10 的实际输出；
- `telemetry_seq`：遥测序号，用于判断是否收到新帧；
- `SKIPPED`：调度落后时跳过的周期数；
- `INTERVAL_P50/P99/MAX_MS`：采样间隔统计。

## 8. 恢复记录

存在 `runtime/route_recovery.txt` 时，控制器会在预检阶段中止，不会接管 CH10。处理步骤见
[RUNTIME_SEQUENCE.md](RUNTIME_SEQUENCE.md) 第 9.3 节。要点：

1. 机体固定、未解锁、人员远离；
2. 确认灯灭、CH10 输出 0；
3. 确认 `SERVO10_FUNCTION` 已恢复到记录值；
4. 以上都确认后才删除恢复记录；
5. 先预览，再短程运行。

**不要**为了启动程序直接删除记录。

## 9. 常见问题

| 现象 | 可能原因 | 处理 |
| --- | --- | --- |
| `missing executable .../bin/wide_search_control` | 未构建或路径不对 | 在目标机 `make control`；确认 ARM64 |
| `No fresh camera data within 10 s` | 前视相机未打开、状态文件不更新、DISPLAY 问题 | 先单独预览；检查 by-id 与环境变量 |
| `Camera unavailable` / 前视读取失败 | 相机被占用、设备名变化 | 检查 `/dev/v4l/by-id`，关闭其他占用进程 |
| 预览显示 `Down camera unavailable` | 下视相机路径错误/被占用 | 检查下视设备；下视缺失会导致转弯中止 |
| `Cannot open serial port` | 路径错、权限不足、被占用 | 检查 by-id、`dialout` 组、`lsof`/QGC/MAVProxy |
| `Heartbeat timeout (system=0...)` | 飞控未启动、波特率/线序错、串口不是 MAVLink | 检查供电、端口、QGC 连接 |
| `Flight controller is armed` | 飞控已解锁 | 立即上锁；确认安全后再运行 |
| `CH12 lamp configuration mismatch` | CH12/RELAY 参数不符 | 按 HARDWARE_IO 核对四个参数 |
| `CH10 not idle` | CH10 输出非 0、功能值不是 0/7、MNT1_TYPE 冲突 | 先回 0，核对功能和云台参数 |
| `Recovery record exists...` | 上次未安全收尾 | 按恢复流程处理，勿直接删 |
| `Front vision stale/invalid` | 前视断流/卡顿 >0.5 s | 查相机、USB、CPU 负载、录像写盘 |
| `Down camera stale during turn` | 下视断流 >0.5 s | 查下视相机/USB；程序拒绝盲转 |
| `SET_SERVO rejected` / `acknowledgement timeout` | 功能/范围/串口问题 | 查 CH10 参数、串口占用、飞控状态 |
| `ACK stalled` | 飞控停止回 ACK | 查串口/飞控负载 |
| `Scheduling stalled >60 ms` | CPU 过载、GUI/录像阻塞、系统卡顿 | 关闭无关程序，降低录像负担，重试 |
| `Cannot publish status` | runtime 不可写/磁盘满 | 检查权限和空间 |
| `No parameter reply` | 串口单向、飞控不响应参数请求 | 检查连接和端口 |
| `Another launcher is already running` | 已有启动器 | `pgrep -af run_wide_search`；等其退出或正常结束 |
| 退出后仍有进程 | 启动器被强杀/异常 | `pgrep -af wide_search`，人工确认后终止 |
| 灯一直亮 | 控制器被强杀/继电器命令失败 | 用地面站发 `DO_SET_RELAY(0,0)`，按恢复流程处理 |
| 预览无窗口 | DISPLAY/XAUTHORITY 无效 | 设置正确显示环境；无头环境不能运行 |

## 10. 测试

### 纯逻辑（任意 x86/ARM 主机）

```bash
make test-cpp
```

覆盖：

- `tests/cpp/test_center100.cpp`：中段输出 1500；
- `tests/cpp/test_zones45_55.cpp`：0.45/0.55 分区边界；
- `tests/cpp/test_fixed2s.cpp`：固定 2 s 转弯、重新武装；
- `tests/cpp/test_gap_lamp.cpp`：断带灯 0.1/0.5 s 状态机。

### 视觉算法（需要 OpenCV + NumPy）

```bash
make test-python
make test          # 两者都跑
```

覆盖：

- `tests/python/test_down_curve.py`：左/右/直曲线、直线、上视线；
- `tests/python/test_gap_light.py`：垂直/水平断口、边缘片段、事件去抖。

> 本次整理所在开发机没有可用的 OpenCV/NumPy 和 MAVLink 头文件，因此只执行了 C++ 逻辑测试、
> Python 语法检查和启动器参数检查；视觉测试与控制器的完整编译应在 Jetson 目标机执行。

## 11. 完整性校验

```bash
make checksums
sha256sum -c SHA256SUMS.txt
```

`SHA256SUMS.txt` 覆盖当前目录结构中的源码、测试、文档、Makefile、启动器和预编译二进制；
不包含 `runtime/`、`build/`、`__pycache__`。

冻结时的平面目录清单保留在 `docs/legacy/SHA256SUMS.flat.txt`，仅作追溯。

## 12. 修改设备路径

### 相机

```bash
export WIDE_SEARCH_FRONT_CAMERA=/dev/video0
export WIDE_SEARCH_DOWN_CAMERA=/dev/video2
python3 run_wide_search.py
```

### 运行时目录

```bash
export WIDE_SEARCH_RUNTIME=/data/winner_fish_runtime
python3 run_wide_search.py --run
```

启动器会把同一个 `WIDE_SEARCH_RUNTIME` 传给视觉和控制器。

### 串口

启动器当前不透传串口参数；直接运行控制器时：

```bash
cd runtime
../bin/wide_search_control --port /dev/serial/by-id/<your-port> --duration 30 --log manual.csv
```

注意：直接运行控制器不会自动启动视觉；必须已有另一个视觉进程在写状态文件。

## 13. 控制器独立参数

```text
--port <path>        串口路径
--state-file <path>  恢复记录路径（默认 route_recovery.txt，相对 cwd）
--log <path>         CSV 路径
--duration <sec>     2..300，默认 300
--dry-run            不打开串口，仅按 50 Hz 读状态并写 status/CSV
```

`--dry-run` 适用于状态文件/逻辑联调，但需要另一个进程持续刷新视觉状态；状态超过 0.5 s
仍会按 `Front vision stale/invalid` 中止。

## 14. 清理与归档

- 每次运行生成新的 `wide_search_<ts>.*` 和 `route_video_<ts>/`，不会覆盖旧记录；
- 归档/删除旧录像前确认日志和关键帧已备份；
- **不要**删除 `route_recovery.txt`，除非已完成第 8 节确认；
- `make clean` 只删除 `build/tests` 和 `bin/wide_search_control`，保留 `runtime/`；
- 更换飞控/相机后，先更新文档中的 by-id 路径，再重新生成校验和。

## 15. 本次结构化的变更范围

为保留冻结备份的行为，本次只做了：

1. 目录重构：`src/control`、`src/vision`、`tests`、`docs`、`bin`、`runtime`；
2. 启动器路径适配：运行时文件统一到 `runtime/`，控制器以 `cwd=runtime` 启动；
3. 视觉模块从 `WIDE_SEARCH_RUNTIME` 取状态目录，相机路径支持环境变量；
4. 修正控制器中与实际逻辑不符的阶段日志文案；
5. 删除未使用的 `ease()` 辅助函数、未使用的形态学核，整理头文件；
6. 增加 Makefile、测试路径引导、SHA-256 清单和文档；
7. 为所有源码文件与函数补充 Doxygen 风格注释；
8. **未改**控制律、阈值、时序、MAVLink 命令集与硬件参数校验逻辑。

原始平面目录说明与校验清单保存在 [docs/legacy/](legacy/)。
