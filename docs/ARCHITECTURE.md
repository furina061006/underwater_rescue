# 架构说明

本文描述 underwater_rescue 的进程、模块、数据契约和救援控制状态机。运行顺序见
[RUNTIME_SEQUENCE.md](RUNTIME_SEQUENCE.md)，硬件参数见 [HARDWARE_IO.md](HARDWARE_IO.md)，
操作流程见 [OPERATIONS.md](OPERATIONS.md)。

## 1. 范围与约束

- 目标平台：Jetson ARM64 + ArduSub / Pixhawk6X。
- 控制对象：只有 CH10 鱼尾。CH11 云台不读不写；CH12 灯不使用。
- 视觉：只有前置摄像头，640×480；不启动下视相机。
- 不发送：解锁、模式切换、RC override、航点、深度/航向指令、继电器命令。
- 单次任务上限 300 s；启动器/控制器默认 290 s。
- 视觉状态过期、飞控解锁、心跳丢失、ACK 超时、调度超时都会中止 CH10 输出并收尾。
- 本版本未在目标机/水池验证；算法和默认参数只经过静态与纯逻辑测试。

## 2. 进程拓扑

~~~text
┌──────────────────────────────────────────────────────────────┐
│ run_rescue.py（前台启动器）                                  │
│  flock(runtime/rescue_launcher.lock)                          │
│  ├── 子进程 1：src/vision/rescue_vision.py                   │
│  │      ├── 前视相机采集                                     │
│  │      ├── OpenCV 检测 + 两画面 GUI                         │
│  │      ├── 滑条调参 -> runtime/rescue_tuning.txt            │
│  │      └── 状态发布 + 可选录像                              │
│  └── 子进程 2：bin/rescue_control --duration 290             │
│         ├── MAVLink 串口（独占）                              │
│         ├── 50 Hz 轻撞状态机                                  │
│         └── 日志/CSV                                          │
└──────────────────────────────────────────────────────────────┘
~~~

进程间不共享内存，只通过 runtime/ 文本状态文件交换数据。所有状态发布使用
“写 .tmp + rename”，消费者用 0.5 s 新鲜度窗口判断有效性。

| 进程 | 入口 | 主要职责 | 生命周期 |
| --- | --- | --- | --- |
| 启动器 | run_rescue.py | 单实例锁、环境准备、拉起/回收视觉与控制器、汇总日志 | 操作者 Ctrl+C 或控制器结束 |
| 视觉 | src/vision/rescue_vision.py | 前视采集、检测、GUI、调参、状态发布、录像 | 被启动器终止或 Q/Esc |
| 控制器 | src/control/rescue_control.cpp | 串口/MAVLink、预检、50 Hz 轻撞、事件判定、收尾、CSV | 时长到期、异常或 SIGINT |

## 3. 模块职责

| 文件 | 职责 |
| --- | --- |
| src/control/rescue_control.cpp | 串口与 MAVLink、CH10 预检、恢复记录、50 Hz 主循环、CSV、收尾 |
| src/control/pillar_ram_logic.hpp | 纯逻辑：搜索/对准/轻撞/冷却/事件判定/重试/避让 |
| src/vision/rescue_vision.py | 前视采集、检测消费、滑条 GUI、状态发布、录像主循环 |
| src/vision/pillar_detect.py | 黑色掩码、闭运算、minAreaRect、宽/长宽比/填充/倾角过滤、跟踪 |
| src/vision/pillar_recording.py | 两画面 AVI、逐帧 CSV、录像说明 |
| run_rescue.py | 默认预览；--run 时等待新鲜状态后启动控制器 |
| Makefile | 控制器构建、C++/Python 测试、SHA-256 清单 |

“纯逻辑”头文件不依赖 OpenCV、MAVLink 或硬件，因此可以在 x86 开发机上直接编译测试。

## 4. 数据契约

目录由 RESCUE_RUNTIME 决定，默认 repo/runtime。

| 文件 | 写入者 | 格式 | 语义 |
| --- | --- | --- | --- |
| rescue_state.txt | 视觉 | stamp found x width_frac aspect | stamp 为 time.monotonic() 秒；found=0/1；x∈[0,1]；width_frac=短边/画宽；aspect=长边/短边 |
| rescue_status.txt | 控制器 | stamp state pwm elapsed width_frac event | state 0 SEARCH/1 ALIGN/2 RAM/3 RECOVER/4 VERIFY/5 AVOID；event=1 表示最近一次撞后判定命中 |
| rescue_tuning.txt | 视觉 GUI | key=value | 控制器启动时读取；视觉滑条实时写 |
| rescue_recovery.txt | 控制器 | 三行文本 | SERVO10_FUNCTION、SERVO10_PWM、SERVO_RATE 的恢复快照 |
| rescue_launcher.lock | 启动器 | 空 | flock 单实例锁 |
| rescue_<ts>.csv | 控制器 | 见下 | 50 Hz 采样、状态、目标 |
| rescue_video_<ts>/ | 视觉 | AVI + frames.csv | 调参/复盘证据 |

新鲜度规则：

- 前视状态：0 ≤ now-stamp ≤ 0.5 s，found=1 时 x/width_frac/aspect 必须合法；否则控制器抛异常。
- 控制状态回显：视觉只在 0.5 s 内读取，否则显示 PREVIEW ONLY。
- 旧 route_recovery.txt 若存在，控制器拒绝启动，提示人工处理。

控制器 CSV 表头：

~~~text
time_s,lateness_ms,target_us,telemetry_us,telemetry_seq,state,x,width_frac,event
~~~

视觉 frames.csv 表头：

~~~text
frame,capture_monotonic,write_monotonic,found,x,width_px,width_frac,aspect,fill,angle,state,pwm,event
~~~

## 5. 视觉管线

1. cap.read() 之前先取 time.monotonic()，避免阻塞读取把旧帧误判为新鲜；读取失败直接终止。
2. BGR→HSV；黑色掩码 V≤Vmax、S≤Smax（Smax=255 时忽略饱和度）。
3. 椭圆核闭运算填黑色网格孔洞（默认 9×9，奇数），再做 3×3 开运算去噪。
4. findContours(RETR_EXTERNAL)；每个轮廓做 minAreaRect。
5. 取 W=短边、H=长边，计算 R=H/W、width_frac=W/画宽、x=中心x/画宽、fill=面积/(W·H)、tilt=长边相对竖直夹角。
6. 过滤：面积≥MinArea、W∈[Wmin,Wmax]、R∈[RatioMin,RatioMax]、fill≥FillMin、|tilt|≤TiltMax。
7. 多目标选 W 最大（最近）；同宽选离 AimX 最近。
8. 跟踪：连续 3 帧相邻才确认；目标丢失后保持 0.3 s 再 found=0；x/width_frac 做 EMA（α=0.5）。
9. 发布 rescue_state.txt；found=0 时 x=-1、width_frac=0、aspect=0。
10. 距离只做显示参考：Z≈f_px·80 mm/W；f_px 可填，不参与控制。

GUI：

- 左前视原图 + 右二值图，叠加绿色 minAreaRect、中心点、AimX 竖线、W%/R/fill/tilt、状态/PWM/事件/FPS。
- Vision 滑条实时生效；Control 滑条写入 rescue_tuning.txt，控制器启动时读取，重启 --run 才生效。
- 按 s 保存快照；q/Esc 退出。

## 6. 控制管线

### 6.1 串口与链路

控制器复用 ArduPilot MAVLink v2：

- open(O_RDWR|O_NOCTTY|O_NONBLOCK)；
- flock(LOCK_EX|LOCK_NB) + TIOCEXCL 独占；
- 保存/恢复 termios，115200 8N1 raw；
- 只接受 compid=1、autopilot=ArduPilotMega、type=Submarine 的心跳对应系统；
- 维护心跳、解锁、参数缓存、命令 ACK、SERVO_OUTPUT_RAW；
- require_idle()：心跳 <2.5 s 且未解锁，否则抛异常；
- parameter()：最多 3 次读取，每次 1 s；
- function()：最多 3 次 PARAM_SET，1 s 内等待回读一致；
- pwm()：DO_SET_SERVO 后 1.5 s 内等 ACK；
- verify_output()：请求 SERVO_OUTPUT_RAW，等待 port 0 的 servo10_raw 等于期望值，超时 1.5 s。

### 6.2 预检顺序

1. 打开串口并等待 ArduSub 心跳（最多 6 s）。
2. require_idle()：未解锁、心跳新鲜。
3. 若存在旧 route_recovery.txt，拒绝启动。
4. 读取 SERVO10_FUNCTION、MNT1_TYPE、SERVO_RATE、SERVO10_MIN/MAX。
5. 校验配置 pwm_min/pwm_max 在 SERVO10_MIN/MAX 范围内；SERVO_RATE=50；CH10 当前输出 0；功能值 0 或 7（7 时 MNT1_TYPE=0）。
6. 以 O_CREAT|O_EXCL 创建 rescue_recovery.txt 并 fsync，写入原始功能、PWM 0、SERVO_RATE。
7. 若原功能非 0，先 PARAM_SET SERVO10_FUNCTION=0 并回读确认。
8. DO_SET_SERVO(10,0) 并遥测确认 0；静置 0.3 s 后进入 50 Hz 主循环。

### 6.3 50 Hz 主循环

每个周期 20 ms：

1. 以 start + tick*0.02 计算绝对截止时间。
2. 等待期间 poll 串口（最多 2 ms），每次检查未解锁/心跳。
3. 检查命令拒绝与 ACK 停滞（0.3 s）。
4. 调度延迟 >60 ms 中止；错过 ≥20 ms 跳帧并重同步。
5. 读 rescue_state.txt，调用 PillarRamLogic::step()。
6. 写 rescue_status.txt（原子 rename）。
7. 发 DO_SET_SERVO(10,value)。
8. 记录 Sample（时间、延迟、目标 PWM、遥测 PWM、state/x/width/event）。
9. 每 100 ms 请求 SERVO_OUTPUT_RAW。
10. 时长到达后退出循环。

主循环不等待舵机 ACK 再发下一条；ACK 由串口泵异步收集并做超时/拒绝检查。

### 6.4 收尾顺序

1. DO_SET_SERVO(10,0) 并遥测确认 0。
2. 恢复 SERVO10_FUNCTION 原值并回读确认。
3. 只有“CH10=0”且“功能恢复确认”同时成立才删除 rescue_recovery.txt。
4. 写 CSV 并打印 SAMPLES / SKIPPED / MAX_LATENESS_MS / INTERVAL_* / LOG。
5. 返回 0（正常）或 1（异常）。

## 7. 救援状态机

详细参数和默认值见 [HARDWARE_IO.md](HARDWARE_IO.md) 与 src/control/pillar_ram_logic.hpp。

~~~text
SEARCH: 扫描正弦 + 巡航摆动；found 且 width_frac≥0.02 -> ALIGN
ALIGN : err=x-aim_x；|err|≤deadband 且 width_frac≥trigger 持续 0.1 s -> RAM
        目标丢失但 last_width≥trigger 且丢失≤0.2 s -> 直接 RAM
RAM   : 1500 ± ram_amp，ram_freq，持续 ram_burst；默认 ±100 us/5 Hz/0.6 s
RECOVER: 输出 0，持续 recover_s；默认 0.6 s
VERIFY: 输出 0，观察 verify_s；目标消失/宽度变化>30%/x 变化>15% -> 命中
        命中 -> SEARCH；未命中且 retries≤retry_max -> ALIGN；否则 AVOID
AVOID : 1500 + avoid_bias + 巡航摆动，持续 avoid_s -> SEARCH
~~~

任何时刻视觉状态超过 0.5 s 都抛异常；控制器按收尾流程回 0 并恢复功能。

## 8. 故障传播

| 故障 | 检测点 | 结果 |
| --- | --- | --- |
| 前视断流/相机崩溃 | PillarRamLogic::step 时间戳 | 中止并收尾 |
| 飞控解锁 | 每周期 require_idle | 中止并收尾 |
| 心跳丢失 >2.5 s | require_idle | 中止并收尾 |
| 串口断开 | poll 错误 | 异常退出；恢复记录可能保留 |
| 舵机命令拒绝/ACK 超时 | 拒绝计数/0.3 s 停滞 | 中止并收尾 |
| 调度延迟 >60 ms | 主循环 | 中止并收尾 |
| 控制器被 kill -9 / 掉电 | 无 | 无收尾；恢复记录保留，需人工恢复 |
| 启动器被 kill -9 | 无 | 控制器/视觉可能成为孤儿；靠 300 s 上限和人工检查 |
| 目标丢失 | 状态机 | 回 SEARCH 或重试，不视为故障 |

## 9. 设计不变量

1. 除 CH10 外，不向飞控写任何执行机构。
2. 改变 CH10 功能前必须存在 rescue_recovery.txt。
3. 恢复记录只在 CH10=0 且功能恢复确认后删除。
4. 控制器只接受新鲜的视觉状态；不拿旧帧继续输出。
5. 状态文件发布必须原子。
6. 时间上限、未解锁、串口独占三项保护不可绕过（除非改代码）。
7. CH11 完全由人工负责；CH12 不使用。

## 10. 扩展点

- 相机路径：RESCUE_FRONT_CAMERA（兼容 WIDE_SEARCH_FRONT_CAMERA）。
- 运行时目录：RESCUE_RUNTIME。
- 串口：直接运行控制器时用 --port。
- 时长：run_rescue.py --run --duration N（2–300）。
- 调参：GUI 滑条写 runtime/rescue_tuning.txt；控制参数重启生效。
- 控制律：src/control/pillar_ram_logic.hpp；同步补充 tests/cpp。
- 离线验证：控制器 --dry-run --duration N 不打开串口，只读状态并写 status/CSV。
