# 操作、排障与恢复

## 1. 最短操作清单

~~~bash
cd underwater_rescue

# 1) 目标机首次构建（需要 MAVLink 头文件）
make control

# 2) 只预览，确认相机、检测和 GUI
python3 run_rescue.py
# Q 或 Esc 退出

# 3) 正式运行，默认 290 s
python3 run_rescue.py --run

# 4) 短程验证（2–300 s）
python3 run_rescue.py --run --duration 30
~~~

运行前必须满足 [HARDWARE_IO.md](HARDWARE_IO.md) 中的参数与联锁条件；否则控制器会中止启动流程（不会接管 CH10）。

## 2. 环境检查

~~~bash
python3 - <<'PY'
import cv2, numpy
print('cv2', cv2.__version__)
print('numpy', numpy.__version__)
PY

echo "DISPLAY=$DISPLAY"
echo "XAUTHORITY=$XAUTHORITY"
ls -l /dev/v4l/by-id/ /dev/serial/by-id/
~~~

- OpenCV/NumPy 必须能在目标机的 python3 中导入；
- 视觉程序会创建窗口，无显示环境会报错；
- 启动器默认 DISPLAY=:0，并优先使用项目根目录的 .preview_xauthority，否则使用 /home/auv8/.Xauthority；可通过环境变量覆盖。

## 3. 构建

~~~bash
make control
# MAVLink 头文件不在默认位置时：
make control MAVLINK_INC=/path/to/include
~~~

默认要求：
~~~text
/opt/ros/noetic/include/mavlink/v2.0/ardupilotmega/mavlink.h
~~~

产物：bin/rescue_control。

本仓库不再自带 ARM64 控制器二进制；开发机没有 MAVLink 头文件时不能构建。
在目标机可以：

- make control 生成 bin/rescue_control；或
- make control-check 生成 build/rescue_control，再用 RESCUE_CONTROL 指过去验证。

## 4. 预览

~~~bash
python3 run_rescue.py
~~~

预览只启动视觉进程：

- 打开前视相机；
- 创建前视原图+二值图和两个调参窗口；
- 写 rescue_state.txt、rescue_tuning.txt 和可选录像；
- 不会打开飞控串口，不会动 CH10。

按 Q/Esc 或 Ctrl+C 退出。退出后启动器会把 rescue_state.txt 重置为 0 0 -1 0 0。

预览检查项：

- 前视二值图能稳定保留黑色笔筒；
- 长宽比/宽度/填充率读数合理；
- 目标在画面中移动时 x 跟手；
- 开水面没有明显误检；
- 窗口不卡顿。

## 5. 正式运行

~~~bash
python3 run_rescue.py --run
~~~

启动器行为：

1. 获取 runtime/rescue_launcher.lock；
2. 启动视觉；
3. 等待 rescue_state.txt 新鲜（最多 10 s）；
4. 启动控制器，控制器完成预检后才会输出 CH10；
5. 控制器运行到时长结束或异常；
6. 回收视觉、重置状态、打印日志。

### 运行中观察

- 终端应出现 RESCUE_TUNING 和 PILLAR 状态日志；
- GUI 显示 STATE/PWM/EVENT（没有控制器时显示 PREVIEW ONLY）；
- 不要同时用 QGC 修改 CH10 配置或解锁；
- 轻撞档默认 ±100 us；需要升档时先改 rescue_tuning.txt 并重启 --run。

## 6. 停止

### 正常到期

290 s（或 --duration）到达后自动收尾：

~~~text
RESCUE_TIME_LIMIT ...
STOP=1 RESTORE_FUNCTION=<原值> confirmed=1
SAMPLES=... LOG=...
~~~

### 手动停止

在启动器终端按一次 Ctrl+C：

1. 启动器给控制器发 SIGINT；
2. 控制器在下一次 require_idle 时中止；
3. 控制器 CH10 回 0、恢复功能、删恢复记录、写 CSV；
4. 启动器最多等 30 s 收尾；
5. 启动器终止视觉、重置状态、释放锁。

> 连续多次 Ctrl+C 可能打断启动器的等待，导致控制器被提前终止。收尾期间请耐心等待。

## 7. 现场调参

1. 把 0.5 kg 支柱放在 2 m/1 m/0.5 m，启动预览；
2. 调 Vmax/Smax，使黑筒在二值图里连续；调 Close 填网格孔洞；
3. 调 MinArea/Wmin/Wmax，只保留支柱；
4. 调 RatioMin/RatioMax，先放宽到 1.6–6.0，再按实际读数收紧；
5. 调 FillMin/TiltMax，排除反光/横向阴影；
6. 在 0.5 m 左右读出 W%，把 Trigger 设到该值附近；
7. 把支柱放画面中心，读 x，把 AimX 设到该 x；
8. 调 SteerGain/CruiseAmp/Deadband，看对准是否稳定；
9. 轻撞先用 ±50/±100，观察 VERIFY 的 event；不够再升 RamAmp；
10. 控制滑条写入 rescue_tuning.txt 后，重启 --run 才生效。

## 8. 恢复记录

存在 runtime/rescue_recovery.txt 时，控制器会在预检阶段中止，不会接管 CH10。
处理步骤见 [RUNTIME_SEQUENCE.md](RUNTIME_SEQUENCE.md) 第 8.3 节。要点：

1. 机体固定、未解锁、人员远离；
2. 确认 CH10 输出 0；
3. 确认 SERVO10_FUNCTION 已恢复到记录值；
4. 以上都确认后才删除恢复记录；
5. 先预览，再短程运行。

旧 runtime/route_recovery.txt 也按未安全收尾处理；新控制器会拒绝启动直到它被人工确认并移除。
**不要**为了启动程序直接删除记录。

## 9. 常见问题

| 现象 | 可能原因 | 处理 |
| --- | --- | --- |
| missing executable .../bin/rescue_control | 未构建或路径不对 | 在目标机 make control；确认 ARM64 |
| No fresh camera data within 10 s | 前视相机未打开、状态文件不更新、DISPLAY 问题 | 先单独预览；检查 by-id 与环境变量 |
| Camera unavailable / 前视读取失败 | 相机被占用、设备名变化 | 检查 /dev/v4l/by-id，关闭其他占用进程 |
| Cannot open serial port | 路径错、权限不足、被占用 | 检查 by-id、dialout 组、lsof/QGC/MAVProxy |
| Heartbeat timeout | 飞控未启动、波特率/线序错、串口不是 MAVLink | 检查供电、端口、QGC 连接 |
| Flight controller is armed | 飞控已解锁 | 立即上锁；确认安全后再运行 |
| CH10 not idle | CH10 输出非 0、功能值不是 0/7、MNT1_TYPE 冲突 | 先回 0，核对功能和云台参数 |
| SERVO10 range excludes configured rescue PWM | SERVO10_MIN/MAX 比调参文件窄 | 调整参数或 rescue_tuning.txt 的 pwm_min/pwm_max |
| Recovery record exists... | 上次未安全收尾 | 按恢复流程处理，勿直接删 |
| Old route_recovery.txt exists... | 旧巡线项目留下的恢复记录 | 人工确认 CH10 已回 0、功能恢复后再移除 |
| Rescue vision stale/invalid | 前视断流/卡顿 >0.5 s | 查相机、USB、CPU 负载、录像写盘 |
| Rescue target invalid | found=1 但 x/width/aspect 非法 | 查视觉发布、跟踪状态 |
| SET_SERVO rejected / acknowledgement timeout | 功能/范围/串口问题 | 查 CH10 参数、串口占用、飞控状态 |
| ACK stalled | 飞控停止回 ACK | 查串口/飞控负载 |
| Scheduling stalled >60 ms | CPU 过载、GUI/录像阻塞、系统卡顿 | 关闭无关程序，降低录像负担，重试 |
| Cannot publish rescue status | runtime 不可写/磁盘满 | 检查权限和空间 |
| No parameter reply | 串口单向、飞控不响应参数请求 | 检查连接和端口 |
| Another rescue launcher is already running | 已有启动器 | pgrep -af run_rescue；等其退出或正常结束 |
| 退出后仍有进程 | 启动器被强杀/异常 | pgrep -af rescue，人工确认后终止 |
| 预览无窗口 | DISPLAY/XAUTHORITY 无效 | 设置正确显示环境；无头环境不能运行 |

## 10. 测试

### 纯逻辑（任意 x86/ARM 主机）

~~~bash
make test-cpp
~~~

覆盖 tests/cpp/test_pillar_ram.cpp：

- SEARCH 无目标输出范围；
- ALIGN 到 RAM 的 0.1 s 确认；
- 轻撞 0.6 s、RECOVER 0、VERIFY 事件命中；
- 未命中重试、retry_max 后 AVOID；
- 近距离丢目标盲撞；
- 陈旧 stamp/非法目标抛异常；
- ram_amp=50/100/300 的输出钳位。

### 视觉算法（需要 OpenCV + NumPy）

~~~bash
make test-python
make test          # 两者都跑
~~~

覆盖 tests/python/test_pillar_detect.py：

- 合成蓝底+黑色网格矩形检测；
- 长宽比/宽度过滤；
- 空水面不误检；
- 跟踪确认和丢失保持。

> 本开发机没有可用的 OpenCV/NumPy 和 MAVLink 头文件，因此只执行了 C++ 逻辑测试、
> Python 语法检查和文档链接检查；视觉测试与控制器的完整编译应在 Jetson 目标机执行。

## 11. 完整性校验

~~~bash
make checksums
sha256sum -c SHA256SUMS.txt
~~~

SHA256SUMS.txt 覆盖当前目录结构中的源码、测试、文档、Makefile 和启动器；
不包含 runtime/、build/、__pycache__。

## 12. 修改设备路径

### 相机

~~~bash
export RESCUE_FRONT_CAMERA=/dev/video0
python3 run_rescue.py
~~~

### 运行时目录

~~~bash
export RESCUE_RUNTIME=/data/underwater_rescue_runtime
python3 run_rescue.py --run
~~~

启动器会把同一个 RESCUE_RUNTIME 传给视觉和控制器。

### 控制器路径

~~~bash
export RESCUE_CONTROL=/path/to/build/rescue_control
python3 run_rescue.py --run
~~~

### 串口

启动器当前不透传串口参数；直接运行控制器时：

~~~bash
cd runtime
../bin/rescue_control --port /dev/serial/by-id/<your-port> --duration 30 --log manual.csv
~~~

注意：直接运行控制器不会自动启动视觉；必须已有另一个视觉进程在写 rescue_state.txt。

## 13. 控制器独立参数

~~~text
--port <path>          串口路径
--recovery-file <path> 恢复记录路径（默认 rescue_recovery.txt，相对 cwd）
--log <path>           CSV 路径
--duration <sec>       2..300，默认 290
--dry-run              不打开串口，仅按 50 Hz 读状态并写 status/CSV
~~~

--dry-run 适用于状态文件/逻辑联调，但需要另一个进程持续刷新 rescue_state.txt；
状态超过 0.5 s 仍会按 Rescue vision stale/invalid 中止。

## 14. 清理与归档

- 每次运行生成新的 rescue_<ts>.* 和 rescue_video_<ts>/，不会覆盖旧记录；
- 归档/删除旧录像前确认日志和关键帧已备份；
- 不要删除 rescue_recovery.txt，除非已完成第 8 节确认；
- make clean 只删除 build/tests、build/rescue_control 和 bin/rescue_control，保留 runtime/；
- 更换飞控/相机后，先更新文档中的 by-id 路径，再重新生成校验和。
