# underwater_rescue

> 水中救援科目：前视黑筒检测 + GUI 调参 + CH10 鱼尾轻撞。
> 本仓库由原 winner_fish 巡线参考项目改造为救援专用；旧巡线实现已删除。

## 1. 项目简介

机器鱼从水池右侧池壁中心出发，用前置摄像头寻找黑色笔筒支柱，对准后通过 CH10 鱼尾轻触支柱，让顶部盒子落水。程序只使用前置摄像头，不依赖下视相机、航向/深度闭环或全局定位。

核心行为：

- 前视 HSV 黑色掩码 + 形态学闭运算填网格孔洞 + minAreaRect 宽/高/长宽比过滤；
- OpenCV GUI 显示前视原图 + 二值图，滑条实时调阈值、长宽比、最小/最大宽度、填充率、倾角、触发宽度、轻撞幅度/频率等；
- 控制器按视觉中心做横向对准，达到设定投影宽度后轻触 CH10；
- 撞后 VERIFY 观察目标是否消失/宽度突变/位置突变，命中则继续搜索，未命中则重试，超过上限则转向离开；
- 单次运行默认 290 s，硬上限 300 s；只允许未解锁、CH10 独占、写恢复记录后接管。

## 2. 与原参考项目的区别

| 项目 | 原巡线参考 | 本救援项目 |
| --- | --- | --- |
| 相机 | 前视 + 下视 | 只用前视 |
| 视觉 | 底线/曲线/断口 | 黑色笔筒宽高/长宽比 |
| 控制 | 巡线 + 固定 2 s 转弯 | 对准 + 轻撞 + 事件判定 + 重试 |
| CH12 | 断带灯 | 不使用 |
| 模式 | route | 只有救援模式 |
| 旧代码 | wide_search_* | 已删除，见 git 历史 |

## 3. 硬件接口

| 硬件 | 默认/要求 | 程序行为 |
| --- | --- | --- |
| 主控 | Jetson ARM64，Python 3 + OpenCV + NumPy | 视觉 + 控制器 |
| 前视相机 | /dev/v4l/by-id/usb-H264_USB_Camera_H264_USB_Camera_2020032801-video-index0，640×480 | 只读 |
| 飞控 | /dev/serial/by-id/usb-ArduPilot_Pixhawk6X-bdshot_20001F001551333234343538-if00，115200 | 独占串口，MAVLink v2 |
| CH10 鱼尾 | SERVO10_FUNCTION 为 0，或 7 且 MNT1_TYPE=0；SERVO_RATE=50；范围覆盖配置 pwm_min/pwm_max | 轻撞输出，默认 ±100 us |
| CH11 云台 | 人工设置 | 程序不读不写 |
| CH12 灯 | 不使用 | 不读参数、不发继电器命令 |

环境变量：

~~~bash
export RESCUE_RUNTIME=/path/to/runtime
export RESCUE_FRONT_CAMERA=/dev/video0
export RESCUE_CONTROL=/path/to/rescue_control
~~~

兼容旧变量 WIDE_SEARCH_RUNTIME / WIDE_SEARCH_FRONT_CAMERA，但新名字优先。

## 4. 数据流

~~~text
前视相机
  -> src/vision/rescue_vision.py
       -> src/vision/pillar_detect.py
       -> runtime/rescue_state.txt
       -> runtime/rescue_tuning.txt（GUI 写）
       -> runtime/rescue_video_<ts>/（可选录像）
  -> bin/rescue_control（cwd=runtime）
       -> src/control/pillar_ram_logic.hpp
       -> runtime/rescue_status.txt
       -> MAVLink -> Pixhawk6X -> CH10 鱼尾
~~~

## 5. 目录结构

~~~text
underwater_rescue/
├── run_rescue.py
├── Makefile
├── README.md
├── AGENTS.md
├── backlog.md
├── bin/rescue_control              # 目标机 make control 生成
├── src/control/
│   ├── rescue_control.cpp
│   └── pillar_ram_logic.hpp
├── src/vision/
│   ├── rescue_vision.py
│   ├── pillar_detect.py
│   └── pillar_recording.py
├── tests/cpp/test_pillar_ram.cpp
├── tests/python/test_pillar_detect.py
├── docs/ARCHITECTURE.md
├── docs/RUNTIME_SEQUENCE.md
├── docs/HARDWARE_IO.md
├── docs/OPERATIONS.md
├── docs/requirement.md
├── docs/images/
├── .dsh/agents/
├── .dsh/logs/
└── runtime/                        # 运行数据，gitignore
~~~

## 6. 控制状态

| 状态 | 行为 |
| --- | --- |
| SEARCH | 扫描正弦 + 巡航摆动；发现目标转 ALIGN |
| ALIGN | 按 x 误差转向；居中且宽度达触发值持续 0.1 s 转 RAM |
| RAM | 轻触 0.6 s，默认 ±100 us / 5 Hz；允许近距离丢目标 |
| RECOVER | 输出 0，滑行 0.6 s |
| VERIFY | 观察 0.5 s；目标消失/宽度变化>30%/x 变化>15% 视为命中 |
| AVOID | 未命中重试超过上限后转向 1.5 s，再搜索 |

所有输出钳位到 pwm_min/pwm_max（默认 1200–1800 us）。

## 7. 快速开始

目标机依赖：Jetson ARM64、Python 3、OpenCV、NumPy、g++、ArduPilot MAVLink 头文件（通常 /opt/ros/noetic/include）。

~~~bash
# 1) 构建控制器
make control

# 2) 只预览 + 调参，不碰飞控
python3 run_rescue.py
# 或 make preview

# 3) 正式运行，默认 290 s
python3 run_rescue.py --run
# 或 make run

# 4) 短程验证
python3 run_rescue.py --run --duration 30
~~~

预览只打开前视相机、创建 GUI、写 runtime；不会打开飞控串口。
正式运行会独占串口并在预检后接管 CH10。

## 8. GUI 调参

- 前视窗口：左原图 + 右二值图，绿色矩形、中心点、AimX 竖线、W%/R/fill/tilt、状态/PWM/事件/FPS。
- Vision 滑条：Vmax、Smax、Close、MinArea、RatioMin/Max、Wmin/Wmax、FillMin、TiltMax、AimX、TrackLost。
- Control 滑条：Trigger、Deadband、SteerGain、CruiseAmp、ScanBias、RamAmp、RamFreq、RamBurst、Recover、Verify、HitW、HitX、RetryMax。
- 视觉滑条实时生效；控制滑条写入 runtime/rescue_tuning.txt，控制器启动时读取，重启 --run 才生效。
- 现场步骤：把 0.5 kg 支柱放在 2 m/1 m/0.5 m，先调 Vmax/Smax/Close，再调 Ratio/Wmin/Wmax/Fill/Tilt；记下 W% 设 Trigger；把支柱放画面中心读 x 设 AimX。

## 9. 安全机制

1. 只操作 CH10；不碰 CH11/CH12，不 arm、不切模式、不发 RC override。
2. 每 20 ms 检查未解锁和心跳；解锁或心跳超过 2.5 s 立即中止收尾。
3. 串口 flock + TIOCEXCL 独占，115200 raw。
4. 改变 CH10 功能前先写 runtime/rescue_recovery.txt；只有 CH10=0 且功能恢复确认后才删除。
5. 检测到旧 runtime/route_recovery.txt 时拒绝启动，提示人工处理。
6. 视觉状态超过 0.5 s 不新鲜、命令拒绝、ACK 停滞 0.3 s、调度延迟 60 ms 都会中止。
7. 单次运行 2–300 s，默认 290 s。
8. 退出：CH10 回 0 并遥测确认 -> 恢复 SERVO10_FUNCTION -> 两项都成功才删恢复记录 -> 写 CSV。

## 10. 测试

~~~bash
make test-cpp        # 纯逻辑，无需硬件/OpenCV
make test-python     # 需要 OpenCV + NumPy，目标机
make test            # 两者
make checksums       # 重算 SHA256SUMS.txt
~~~

本机只做 C++ 逻辑测试、Python 语法检查、文档链接检查；不运行相机/GUI/控制器/飞控。

## 11. 已知边界

- 轻撞可能多次都撞不倒 0.5 kg 支柱；程序会重试并在上限后离开。
- 事件判定用目标消失/宽度/位置变化做代理，不是盒子落水的直接确认。
- 只有前视、无航向/全局定位，不保证完整 5 支柱路线；首版优先中线 4→5→2。
- 单尾没有倒车；撞后可能贴住支柱或池壁。
- 水面反光、网格孔洞、透明水杯、真鱼颜色会影响检测，需要现场调参。
- 仓库不再自带 ARM64 控制器二进制；目标机必须先 make control 生成 bin/rescue_control。
- 未在目标机/水池重新验证；不得把静态检查当成硬件验证。

## 12. 文档索引

- [AGENTS.md](AGENTS.md)：agent 红线、路由、结构约定。
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)：进程、模块、数据契约、状态机。
- [docs/RUNTIME_SEQUENCE.md](docs/RUNTIME_SEQUENCE.md)：启动到收尾的完整顺序。
- [docs/HARDWARE_IO.md](docs/HARDWARE_IO.md)：设备、参数、CH10、场地和联锁。
- [docs/OPERATIONS.md](docs/OPERATIONS.md)：构建、预览、运行、调参、排障、恢复。
- [docs/requirement.md](docs/requirement.md)：比赛原始要求。
- [backlog.md](backlog.md)：未完成/待验证事项。
- [.dsh/logs/README.md](.dsh/logs/README.md)：历史记录约定。
