# 硬件接口与参数

本文只描述救援软件实际使用的接口。整机供电、接线、防水和推进器安全按整机 SOP 执行；
这里不替代电气原理图。

## 1. 系统连接

~~~text
                 USB
┌────────────┐  H264   ┌──────────────┐
│ 前视相机   ├────────►│              │
└────────────┘         │   Jetson     │
                       │   ARM64      │
                       │              │
                       │ 视觉 + 控制器│
                       └──────┬───────┘
                              │ USB 串口 115200
                              ▼
                       ┌──────────────┐
                       │ Pixhawk6X    │
                       │ ArduSub      │
                       └──────┬───────┘
                              │
                         CH10 鱼尾
~~~

CH11 云台由人工设置；CH12 灯在本项目不使用。

## 2. 设备节点

| 设备 | 默认路径 | 打开参数 | 环境变量覆盖 |
| --- | --- | --- | --- |
| 前视相机 | /dev/v4l/by-id/usb-H264_USB_Camera_H264_USB_Camera_2020032801-video-index0 | V4L2，640×480，缓冲 1 | RESCUE_FRONT_CAMERA（兼容 WIDE_SEARCH_FRONT_CAMERA） |
| 飞控 | /dev/serial/by-id/usb-ArduPilot_Pixhawk6X-bdshot_20001F001551333234343538-if00 | 115200 8N1 raw，独占 | 控制器 --port |

检查设备：

~~~bash
ls -l /dev/v4l/by-id/
ls -l /dev/serial/by-id/
v4l2-ctl --list-devices 2>/dev/null || true
~~~

如果 by-id 名称变化，用环境变量指向新的 /dev/videoN，或修改源码默认值。
不要让两个视觉进程同时打开同一台相机。

## 3. 飞控串口

Link 的配置：

- open(path, O_RDWR | O_NOCTTY | O_NONBLOCK)；
- flock(fd, LOCK_EX | LOCK_NB)：防止两个控制器同时写；
- ioctl(fd, TIOCEXCL)：禁止其他进程打开同一 tty；
- 保存原 termios，退出时恢复；
- cfmakeraw，115200 输入/输出；
- CLOCAL | CREAD，关闭 CRTSCTS、HUPCL。

MAVLink：

- 使用 mavlink/v2.0/ardupilotmega/mavlink.h；
- 只接受 compid=1、autopilot=MAV_AUTOPILOT_ARDUPILOTMEGA、type=MAV_TYPE_SUBMARINE 的心跳 ;
- 控制器自身 system/component 为 245/190；
- 只解析目标 system 的消息，忽略其他系统；
- 不发送解锁、模式、RC override、继电器命令。

## 4. ArduSub 参数

启动时逐项读取并校验：

| 参数 | 要求 | 用途 | 不满足 |
| --- | --- | --- | --- |
| SERVO10_FUNCTION | 0，或 7 且 MNT1_TYPE=0 | CH10 作为鱼尾输出；7 为原云台功能，程序临时改为 0 | CH10 not idle |
| SERVO10_MIN | ≤ 调参文件 pwm_min（默认 1200） | 允许轻撞/转向输出 | SERVO10 range excludes configured rescue PWM |
| SERVO10_MAX | ≥ 调参文件 pwm_max（默认 1800） | 同上 | 同上 |
| SERVO_RATE | = 50 | 已验证的 50 Hz PWM 计划 | Expected SERVO_RATE=50 |
| MNT1_TYPE | 当 SERVO10_FUNCTION=7 时必须为 0 | 避免与云台功能冲突 | CH10 not idle |
| SERVO12_FUNCTION | 不读取 | CH12 不使用 | — |
| RELAY1_* | 不读取 | CH12 不使用 | — |

> 默认轻撞是 ±100 us（1400–1600），但调参文件默认 pwm_min=1200、pwm_max=1800，
> 以便现场升到 ±300。若飞控实际范围更窄，需要把 rescue_tuning.txt 的 pwm_min/pwm_max
> 改成实际可用范围，并在首次验证时确认不被限幅。

## 5. CH10 鱼尾

### 5.1 输出语义

| 状态 | PWM |
| --- | --- |
| 未运行/收尾 | 0 |
| SEARCH | 1500 + 扫描正弦 + 巡航正弦，默认 1250–1750 |
| ALIGN | 1500 + steer_gain·err + 巡航正弦，钳位到 pwm_min/pwm_max |
| RAM | 1500 ± ram_amp，默认 ±100 us、5 Hz、0.6 s |
| RECOVER/VERIFY | 0 |
| AVOID | 1500 + avoid_bias + 巡航正弦，钳位 |

- 0 表示停止/无输出，是本项目沿用原项目的怠速值；
- 默认轻撞档与已验证巡线幅度一致；±300 只作为可调上限；
- 所有值四舍五入为整数微秒，由 DO_SET_SERVO 的 param2 下发；
- 频率由飞控 SERVO_RATE=50 决定，控制器以 50 Hz 更新目标值。

### 5.2 轻撞档

- 默认 ram_amp=100、ram_freq=5 Hz、ram_burst=0.6 s；
- 现场可先用 ±50/±100 试撞，再按需要升到 ±200/±300；
- 撞后 RECOVER 0.6 s、VERIFY 0.5 s；
- 未命中重试 retry_max=3 次，仍不成立则 AVOID 1.5 s 离开；
- 输出始终钳位到 pwm_min/pwm_max，默认 1200–1800。

## 6. CH11 云台与 CH12 灯

- CH11：程序不读、不写、不输出；起飞前由人工设置。
- CH12：本项目不使用；控制器不读 CH12/RELAY 参数、不发 DO_SET_RELAY。
- 如果现场灯还亮着，由人工关闭，不要指望控制器收尾时关灯。

## 7. 场地与支柱几何

- 水池有效区 3 m × 2 m × 0.6 m，水深 0.2 m；
- 机器鱼从右侧池壁中心出发；
- 黑色笔筒：直径 80+5 mm，高度 190+10 mm；顶部透明水杯约 84 mm × 84 mm；
- 1/3/4 为双笔筒叠制（较高）；2 为 0.25 kg 配重；5 为 0.5 kg 配重；
- 按现场修正：5 号在中间、4 号在右中、2 号在左中，1/3 在左侧上下；
- 从右侧中心出发，中线顺序是 4→5→2；首版按最近/投影最大选择，不硬编码编号。

## 8. 视觉坐标与阈值

- 原始画面 640×480；使用全画面，不裁 ROI。
- 黑色掩码：HSV V≤Vmax（默认 100）、S≤Smax（默认 120）。
- 闭运算默认 9×9 椭圆核；3×3 开运算。
- W=minAreaRect 短边，H=长边；R=H/W；width_frac=W/画宽；x=中心/画宽。
- 过滤：面积≥300、W∈[12,500] px、R∈[1.6,6.0]、fill≥45%、|tilt|≤25°。
- 跟踪：3 帧确认；丢失保持 0.3 s；EMA α=0.5。
- GUI 滑条可改以上阈值；阈值不持久化到源码，只写 runtime/rescue_tuning.txt。

## 9. 安全联锁

| 联锁 | 检查点 | 动作 |
| --- | --- | --- |
| 未解锁 | 每 20 ms | 已解锁 -> 中止并收尾 |
| 心跳 | 每 20 ms | >2.5 s -> 中止并收尾 |
| 串口独占 | 启动 | 加锁失败 -> 拒绝启动 |
| CH10 范围/频率/输出 | 启动预检 | 不符 -> 中止，不接管 CH10 |
| 恢复记录 | 启动预检 | rescue_recovery.txt 或旧 route_recovery.txt 存在 -> 中止 |
| 前视新鲜 | 每周期 | >0.5 s -> 中止 |
| ACK | 每周期 | 拒绝/0.3 s 停滞 -> 中止 |
| 调度 | 每周期 | >60 ms -> 中止 |
| 时长 | 循环 | ≥300 s -> 正常收尾 |
| 收尾 | 退出 | CH10=0 且功能恢复才删恢复记录 |

## 10. 上电/断电顺序建议

程序启动前：

1. 按整机 SOP 上电；
2. 确认飞控启动完成、未解锁、参数正确；
3. 确认前视相机和飞控 by-id 节点出现；
4. 确认云台位置、鱼尾无动作、灯状态安全；
5. 确认 runtime/rescue_recovery.txt 和旧 route_recovery.txt 都不存在；
6. 启动预览，确认画面和检测正常；
7. 再执行 --run。

程序退出后：

1. 确认日志中 STOP=1、confirmed=1；
2. 确认没有残留 rescue_control / rescue_vision；
3. 确认 rescue_recovery.txt 不存在；
4. 目视确认鱼尾中位；
5. 按整机 SOP 断电。

## 11. 明确不在软件范围内的内容

- 推进器、深度、航向、压载、漏水检测；
- 电池、电源分配、保险丝；
- 云台控制；
- 相机曝光/白平衡/标定（使用相机默认值）；
- 水下密封与线缆应力；
- 紧急上浮/抛载；
- 飞控固件升级与参数持久化。
