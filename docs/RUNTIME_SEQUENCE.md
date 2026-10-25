# 程序运行与硬件交互全顺序

本文按时间顺序描述救援模式的启动、前视预览、控制器预检、50 Hz 轻撞循环、
收尾和恢复。架构见 [ARCHITECTURE.md](ARCHITECTURE.md)，硬件参数见
[HARDWARE_IO.md](HARDWARE_IO.md)，操作见 [OPERATIONS.md](OPERATIONS.md)。

约定：

- 控制器指 bin/rescue_control；
- 视觉指 src/vision/rescue_vision.py；
- 启动器指 run_rescue.py；
- 时间戳来自 time.monotonic() / C++ steady_clock，单位秒；
- 超时与频率来自当前源码；本版本未在目标机重新实测。

## 0. 一页总览

~~~text
[人工] 上电、检查设备/参数、云台固定、确认未解锁
   │
   ▼
[B] python3 run_rescue.py --run
   │  flock + 状态归零
   ├──► [C] 视觉启动：前视相机、检测、GUI、调参、录像
   │        └── 持续写 rescue_state.txt / rescue_tuning.txt
   │
   ├── 等待“rescue_state.txt 年龄 < 0.5 s”（最多 10 s）
   │
   ▼
[D] 控制器启动：独占串口 -> 等心跳 -> 未解锁
   │  校验 CH10 参数/范围/50 Hz/输出 0
   │  写 rescue_recovery.txt -> SERVO10_FUNCTION=0 -> CH10=0
   ▼
[E] 290 s / 50 Hz 主循环
   │  读状态 -> SEARCH/ALIGN/RAM/RECOVER/VERIFY/AVOID -> CH10
   │  每 100 ms 请求 SERVO_OUTPUT_RAW
   ▼
[F] 到期：CH10=0 -> 恢复 SERVO10_FUNCTION -> 删恢复记录 -> 写 CSV
   │
   ▼
[G] 启动器回收视觉 -> 状态归零 -> 释放锁 -> 打印日志
   │
   ▼
[I] 人工确认 CH10=0、无残留进程、恢复记录不存在 -> 断电
~~~

## 1. 阶段 A：上电前的人工准备

| 步骤 | 检查项 | 要求 |
| --- | --- | --- |
| A1 | 整机供电 | 按整机 SOP 上电；鱼尾周围无人员与异物 |
| A2 | 飞控状态 | 未解锁；遥测/地面站可用；ArduSub 心跳正常 |
| A3 | CH10 | SERVO10_FUNCTION 为 0，或 7 且 MNT1_TYPE=0；SERVO10_MIN≤配置 pwm_min、SERVO10_MAX≥配置 pwm_max；SERVO_RATE=50；当前输出 0 |
| A4 | CH11 云台 | 人工设置；程序不碰 |
| A5 | CH12 | 本项目不使用；灯若亮着人工关闭 |
| A6 | 前视相机 | /dev/v4l/by-id 节点存在，画面清晰 |
| A7 | 串口 | 没有 QGC、MAVProxy、其他控制器占用 |
| A8 | 恢复记录 | runtime/rescue_recovery.txt 不存在；旧 runtime/route_recovery.txt 也不存在 |
| A9 | 显示 | DISPLAY/XAUTHORITY 有效，能看到 OpenCV 窗口 |
| A10 | 目录 | runtime/ 可写；磁盘有空间放录像 |

检查命令示例：

~~~bash
ls -l /dev/v4l/by-id/
ls -l /dev/serial/by-id/
ls -l runtime/rescue_recovery.txt 2>/dev/null || echo "no rescue recovery record"
ls -l runtime/route_recovery.txt 2>/dev/null || echo "no old recovery record"
~~~

## 2. 阶段 B：启动器

命令：

~~~bash
python3 run_rescue.py --run
# 可选：python3 run_rescue.py --run --duration 30
~~~

| 步骤 | 启动器动作 | 效果 |
| --- | --- | --- |
| B1 | 解析参数 | --run 缺省为纯预览；--duration 2–300，默认 290 |
| B2 | 解析根目录 | ROOT=repo；RUNTIME=RESCUE_RUNTIME 或 repo/runtime |
| B3 | 设置显示环境 | DISPLAY 默认 :0；XAUTHORITY 优先 repo/.preview_xauthority，否则 /home/auv8/.Xauthority |
| B4 | 获取单实例锁 | flock(runtime/rescue_launcher.lock)；失败退出 1 |
| B5 | 状态归零 | rescue_state.txt 写 0 0 -1 0 0；删除旧 rescue_status.txt |
| B6 | 启动视觉 | python3 -u src/vision/rescue_vision.py；可选 --no-record |
| B7 | 预览分支 | 没有 --run 时等待视觉退出，不启动控制器 |
| B8 | 等待新鲜状态 | 每 50 ms 读 rescue_state.txt 第一列；要求 0≤now-stamp<0.5；最多 10 s |
| B9 | 启动控制器 | bin/rescue_control --duration N --log runtime/rescue_<时间>.csv；cwd=runtime |
| B10 | 等待控制器 | 结束后打印完整日志；返回控制器退出码 |

## 3. 阶段 C：视觉进程

| 步骤 | 动作 | 说明 |
| --- | --- | --- |
| C1 | 注册 SIGTERM/SIGHUP | 转成 KeyboardInterrupt，进入 finally |
| C2 | 创建 runtime | 发布 rescue_state.txt=0 0 -1 0 0 |
| C3 | 打开前视相机 | RESCUE_FRONT_CAMERA 或默认 H264 by-id；V4L2 640×480；失败直接抛错 |
| C4 | 创建录像 | runtime/rescue_video_<时间>/；annotated.avi、frames.csv；更新 latest_rescue_recording.txt |
| C5 | 创建窗口 | 前视原图+二值图；Vision/Control 滑条窗口 |
| C6 | 进入帧循环 | 先取时间戳 -> cap.read() -> 检测 -> 跟踪 -> 发布 |
| C7 | 写调参 | 滑条变化时最多 5 Hz 原子写 rescue_tuning.txt |
| C8 | 叠加与录制 | 画矩形/中心/AimX/状态；写 AVI/CSV；imshow |
| C9 | 退出 | Q/Esc、启动器终止或异常；finally 发布 0 0 -1 0 0、关录像、释放相机、销毁窗口 |

视觉每帧发布：

~~~text
<front_stamp> <found 0/1> <x> <width_frac> <aspect>
~~~

found=0 时 x=-1、width_frac=0、aspect=0。控制器只接受 0.5 s 内的时间戳。

## 4. 阶段 D：控制器建链与预检

| 步骤 | 控制器动作 | 飞控/硬件响应 | 失败行为 |
| --- | --- | --- | --- |
| D1 | 打开飞控串口 | open+flock+TIOCEXCL；115200 raw 8N1 | 立即退出，不操作硬件 |
| D2 | 等待 ArduSub 心跳 | compid=1、ArduPilotMega、Submarine | 最多 6 s；超时退出 |
| D3 | 未解锁检查 | base_mode SAFETY_ARMED=0 | 已解锁退出 |
| D4 | 旧恢复记录检查 | runtime/route_recovery.txt 必须不存在 | 存在则退出，提示人工处理 |
| D5 | 读取 CH10 参数 | SERVO10_FUNCTION、MNT1_TYPE、SERVO_RATE、SERVO10_MIN/MAX | 每个最多 3×1 s |
| D6 | 校验 CH10 | 范围覆盖 pwm_min/pwm_max；SERVO_RATE=50；当前输出 0；功能 0 或 7 且 MNT1_TYPE=0 | 失败退出，不写恢复记录 |
| D7 | 写恢复记录 | O_CREAT+O_EXCL、0600、fsync；写功能值/PWM 0/SERVO_RATE | 已存在或写失败退出 |
| D8 | 禁用原功能 | 若功能非 0，PARAM_SET SERVO10_FUNCTION=0 | 3×1 s；失败进入收尾 |
| D9 | CH10 回 0 | DO_SET_SERVO(10,0)；ACK + 遥测确认 | 1.5 s；失败进入收尾 |
| D10 | 静置 | 0.3 s 内 pump + require_idle | 异常进入收尾 |

D6 通过后会打印 RESCUE_TUNING 摘要（在加载调参时打印）。

## 5. 阶段 E：290 s / 50 Hz 主循环

每 20 ms：

~~~text
deadline = start + tick × 0.02
  ├─ 1. deadline 前 poll 串口（单次最多 2 ms），每轮 require_idle()
  ├─ 2. 检查命令拒绝、ACK 停滞（>0.30 s）
  ├─ 3. pump(0) 再处理已到达数据
  ├─ 4. require_idle()（未解锁 + 心跳 <2.5 s）
  ├─ 5. now-deadline > 60 ms ? -> 中止
  ├─ 6. 落后 ≥20 ms 跳 tick 并重同步
  ├─ 7. elapsed >= duration ? -> 退出
  ├─ 8. target_rescue(elapsed)：读 rescue_state.txt -> 状态机 -> 写 rescue_status.txt
  ├─ 9. 发 DO_SET_SERVO(10, value)
  ├─10. 记录 Sample（时间、延迟、目标、遥测、state/x/width/event）
  ├─11. 每 100 ms 请求 SERVO_OUTPUT_RAW
  └─12. tick++
~~~

状态机摘要：

| 状态 | 条件/行为 |
| --- | --- |
| SEARCH | 扫描正弦 + 巡航摆动；发现目标 -> ALIGN |
| ALIGN | 按 x 误差转向；居中且宽度达触发值持续 0.1 s -> RAM |
| RAM | 1500 ± ram_amp，ram_freq，持续 ram_burst；默认 ±100 us/5 Hz/0.6 s |
| RECOVER | 输出 0，持续 recover_s；默认 0.6 s |
| VERIFY | 输出 0，观察 verify_s；目标消失/宽度变化>30%/x 变化>15% -> 命中 |
| AVOID | 未命中重试超过上限后转向 avoid_s，再 SEARCH |

注意：

- 主循环不等待 DO_SET_SERVO 的 ACK 再发下一条；ACK 由第 1 步异步处理。
- 前视状态超过 0.5 s 不新鲜会抛异常并收尾。
- 近距离目标被画面裁掉时，ALIGN 允许用 last_width 盲撞。
- 冲撞档默认轻触；现场可按调参文件升到 ±200/±300。

## 6. 阶段 F：正常收尾

| 步骤 | 动作 | 输出/校验 |
| --- | --- | --- |
| F1 | 跳出主循环 | — |
| F2 | DO_SET_SERVO(10,0) | 1.5 s ACK + 遥测确认 |
| F3 | 静置 0.2 s | pump + require_idle |
| F4 | 打印摘要 | RESCUE_TIME_LIMIT duration=... state=... hits=... PWM_RATE=50Hz |
| F5 | 再次回 0 | DO_SET_SERVO(10,0) + 遥测确认 |
| F6 | 恢复功能 | PARAM_SET SERVO10_FUNCTION=原值并回读 |
| F7 | 打印 | STOP=1 RESTORE_FUNCTION=... confirmed=1 |
| F8 | 删除恢复记录 | 仅当 F2 与 F6 都成功 |
| F9 | 写 CSV | SAMPLES/SKIPPED/MAX_LATENESS/INTERVAL/LOG |
| F10 | 退出 | 0 正常；任一安全校验失败 1 |

### 6.1 正常日志示例

~~~text
Started 290-second rescue run. Log: /.../runtime/rescue_20261025_230000.log
RESCUE_TUNING ram_amp=100 freq=5 burst=0.6 trigger=0.2 pwm=1200..1800
PILLAR ALIGN time=1.234 x=0.512 width=0.08 pwm=1530 hits=0 retries=0 event=0
PILLAR RAM time=2.345 x=0.501 width=0.21 pwm=1500 hits=1 retries=1 event=0
PILLAR RECOVER time=2.945 x=0.501 width=0.21 pwm=0 hits=1 retries=1 event=0
PILLAR VERIFY time=3.545 x=0.501 width=0.21 pwm=0 hits=1 retries=1 event=0
PILLAR SEARCH time=4.045 x=0.501 width=0.21 pwm=0 hits=1 retries=0 event=1
RESCUE_TIME_LIMIT duration=290s state=0 hits=1 PWM_RATE=50Hz
STOP=1 RESTORE_FUNCTION=0 confirmed=1
SAMPLES=14500 SKIPPED=0 MAX_LATENESS_MS=3.2 INTERVAL_P50_MS=20.0 P99_MS=20.4 MAX_MS=21.0 LOG=...
~~~

## 7. 阶段 G：启动器回收

| 步骤 | 启动器动作 |
| --- | --- |
| G1 | 控制器正常返回：打印日志全文，返回控制器退出码 |
| G2 | 控制器仍在运行（Ctrl+C/异常）：发 SIGINT，最多等 30 s 收尾 |
| G3 | 30 s 未退出：terminate，2 s 后 kill |
| G4 | 视觉仍在运行：terminate，最多 3 s，必要时 kill |
| G5 | rescue_state.txt 重置为 0 0 -1 0 0 |
| G6 | 释放 rescue_launcher.lock |
| G7 | 若 rescue_recovery.txt 或旧 route_recovery.txt 仍存在，打印 WARNING |

## 8. 异常与人工恢复

### 8.1 Ctrl+C

~~~text
操作者 Ctrl+C
  -> 启动器 KeyboardInterrupt
  -> finally: 给控制器 SIGINT
  -> 控制器置 interrupted=1
  -> 下一次 require_idle() 抛 Interrupted
  -> 内层 catch 打印 TEST_STOPPED
  -> CH10 回 0 -> 恢复 SERVO10_FUNCTION -> 两项成功才删恢复记录
  -> 写 CSV -> 退出码 1
  -> 启动器等待 ≤30 s，终止视觉，状态归零，释放锁
~~~

建议只按一次 Ctrl+C。

### 8.2 运行中异常

| 异常 | 检测点 | 控制器行为 | 恢复记录 |
| --- | --- | --- | --- |
| 运行中被解锁 | 每周期 require_idle | 中止 + 收尾 | 收尾成功则删除 |
| 心跳 >2.5 s | 每周期 require_idle | 中止 + 收尾 | 收尾成功则删除 |
| 前视状态 >0.5 s | PillarRamLogic::step | 中止 + 收尾 | 收尾成功则删除 |
| 舵机命令被拒 | rejected 计数 | 中止 + 收尾 | 收尾成功则删除 |
| ACK 停滞 >0.3 s | 主循环 | 中止 + 收尾 | 收尾成功则删除 |
| 调度延迟 >60 ms | 主循环 | 中止 + 收尾 | 收尾成功则删除 |
| 串口断开 | poll | 抛异常；可能无法收尾 | 可能保留 |
| 视觉进程崩溃 | 状态过期 | 中止 + 收尾 | 收尾成功则删除 |
| 控制器被 kill -9 / 掉电 | 无 | 无收尾 | 保留 |
| 启动器被 kill -9 | 无 | 控制器可能继续到 300 s 上限 | 取决于控制器是否正常收尾 |

### 8.3 人工恢复流程

当 runtime/rescue_recovery.txt 存在时：

1. 确认机体固定、周围安全、人员远离鱼尾；
2. 用地面站确认飞控未解锁；
3. 读取恢复记录：
   ~~~text
   SERVO10_FUNCTION=<原值>
   SERVO10_PWM=0
   SERVO_RATE=<值>
   ~~~
4. 用 QGC/MAVProxy 确认 CH10 输出为 0；若不为 0，先发 DO_SET_SERVO(10,0)；
5. 确认 SERVO10_FUNCTION 已恢复到记录值；若没有，先 PARAM_SET 恢复并回读；
6. 以上全部确认后，才删除 rescue_recovery.txt；
7. 查看最近的 .log/.csv，查明中止原因；
8. 先跑预览，确认视觉正常，再考虑短时长 --run。

旧 runtime/route_recovery.txt 也按同样原则处理；新控制器会拒绝启动直到它被人工确认并移除。

## 9. 时间预算与实时性

| 项目 | 数值 | 说明 |
| --- | --- | --- |
| 启动器等待前视新鲜 | 最多 10 s | 每 50 ms 检查，年龄 <0.5 s |
| 控制器等待心跳 | 最多 6 s | 识别 ArduSub |
| 心跳新鲜窗口 | 2.5 s | 超过即中止 |
| 视觉状态新鲜窗口 | 0.5 s | 前视状态 |
| 参数读取 | 每次最多 1 s，3 次 | 单个参数 |
| 舵机 ACK | 1.5 s | 逐命令确认 |
| 遥测确认 | 1.5 s | verify_output |
| 主循环 | 20 ms（50 Hz） | 绝对截止时间 |
| 调度容差 | 60 ms | 超过即中止 |
| 目标确认 | 0.1 s | ALIGN 到 RAM |
| 轻撞 | 0.6 s | 默认 |
| 冷却 | 0.6 s | 默认 |
| 撞后观察 | 0.5 s | 默认 |
| 任务上限 | 300 s | 硬限制；默认 290 s |
| 中断后等待收尾 | 最多 30 s | 启动器 |
| 视觉终止等待 | 最多 3 s | 启动器 |

## 10. 屏幕与日志判读

预览叠加：

- FOUND/HELD/NO TARGET：当前稳定目标；
- x/aim/W%/R/fill/tilt：检测量；
- STATE/PWM/EVENT：控制器回显；没有控制器时显示 PREVIEW ONLY；
- 绿色矩形：minAreaRect；红点：目标中心；蓝色竖线：AimX；
- fps：视觉帧率。

控制器日志关键字：

| 关键字 | 含义 |
| --- | --- |
| RESCUE_TUNING | 启动时生效的轻撞档/触发宽度/范围 |
| PILLAR SEARCH/ALIGN/RAM/RECOVER/VERIFY/AVOID | 状态切换 |
| TEST_STOPPED | 内层循环异常中止 |
| RESCUE_TIME_LIMIT | 正常到期摘要 |
| STOP=1 | CH10 遥测确认 0 |
| RESTORE_FUNCTION=... confirmed=1 | 原功能恢复成功 |
| ERROR ... | 预检/建链/未捕获异常 |
| SAMPLES=... | CSV 采样统计 |

## 11. 跨进程文件时序

| 时刻 | 视觉 | 控制器 | 启动器 |
| --- | --- | --- | --- |
| 启动 | 写 rescue_state.txt=0 0 -1 0 0 | — | 写同文件、删旧 status |
| 预热 | 每帧写 state/tuning；写录像/CSV | — | 每 50 ms 检查状态新鲜 |
| 预热完成 | 继续更新 | 启动，读状态/调参 | 启动控制器，等待 |
| 预检 | 继续更新 | 读状态；写 rescue_recovery.txt | 等待 |
| 运行 | 每帧写 state/tuning；读 status 回显 | 每 20 ms 读状态、写 status；每 100 ms 请求遥测 | 等待 |
| 收尾 | 继续预览直到被终止 | CH10 回 0、恢复功能、删恢复记录、写 CSV | 等控制器；终止视觉；状态归零 |
| 退出 | 关闭录像/相机 | 关闭串口/恢复 termios | 释放锁 |

## 12. 与旧巡线项目的差异

- 不启动下视相机，不读 down_curve_state / gap_light_state。
- 不操作 CH12，不发 DO_SET_RELAY，不要求灯参数。
- 不解析 STATUSTEXT，不显示飞控文本。
- 不写 wide_search_* / route_* 状态文件；使用 rescue_*。
- 旧 route_recovery.txt 会被视为不安全状态并拒绝启动。
