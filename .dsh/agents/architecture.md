> 返回 [AGENTS.md](../../AGENTS.md) ｜ 状态：【事实】为主
> 本文件是 agent 摘要；完整架构的权威定义在 [docs/ARCHITECTURE.md](../../docs/ARCHITECTURE.md)，冲突时以 docs/源码为准。
> 相关：[运行顺序](../../docs/RUNTIME_SEQUENCE.md)、[视觉](vision.md)、[控制](control.md)、[操作](operations.md)

# 架构与数据流

## 1. 三个执行体

【事实】运行由一个启动器和两个子进程组成：

~~~text
run_rescue.py（启动器）
├── src/vision/rescue_vision.py（Python 前视视觉）
└── bin/rescue_control（C++ CH10 控制器）
~~~

| 执行体 | 主要职责 | 是否碰硬件 |
| --- | --- | --- |
| 启动器 | 单实例锁、运行时目录、拉起/回收进程、打印日志 | 只间接启动子进程 |
| 视觉 | 前视相机、检测、GUI、调参、状态发布、录像 | 打开相机和 GUI |
| 控制器 | 串口、MAVLink、预检、50 Hz 轻撞、收尾 | 打开飞控串口并操作 CH10 |

【设计】视觉和控制不共享内存，只通过 runtime/ 下的文本状态文件交换数据。

## 2. 运行时文件

【事实】控制器以 cwd=runtime 启动，因此它读写的状态文件和恢复记录都落在运行时目录。

| 文件 | 方向 | 用途 |
| --- | --- | --- |
| rescue_state.txt | 视觉 -> 控制 | 前视时间戳、found、x、width_frac、aspect |
| rescue_tuning.txt | 视觉 -> 控制 | 视觉/控制调参；控制器启动时读取 |
| rescue_status.txt | 控制 -> 视觉 | 状态、PWM、运行时间、宽度、event |
| rescue_recovery.txt | 控制器 | CH10 恢复快照；存在时预检中止 |
| 旧 route_recovery.txt | 旧项目 | 存在时新控制器拒绝启动，需人工处理 |
| 日志/CSV/录像 | 控制器/视觉 | 现场证据，不能随手删 |

【事实】状态文件使用“写 .tmp + rename”原子发布；消费者只接受 0.5 s 内的数据。

## 3. 源码职责

| 位置 | 职责 |
| --- | --- |
| run_rescue.py | 唯一启动入口；默认预览，--run 才启动控制器 |
| src/vision/rescue_vision.py | 前视采集、检测消费、滑条 GUI、状态发布、录像 |
| src/vision/pillar_detect.py | 黑筒掩码/闭运算/minAreaRect/过滤/跟踪 |
| src/vision/pillar_recording.py | 两画面 AVI + CSV |
| src/control/rescue_control.cpp | 串口、MAVLink、预检、50 Hz 循环、收尾、CSV |
| src/control/pillar_ram_logic.hpp | 搜索/对准/轻撞/冷却/事件/重试/避让 |
| tests/cpp/test_pillar_ram.cpp | 纯逻辑测试 |
| tests/python/test_pillar_detect.py | 合成图像视觉测试 |

【事实】所有 C++/Python 函数都带 Doxygen 风格注释；修改行为时同步更新注释。

## 4. 必须保持的不变量

【设计】

1. 视觉进程不打开飞控串口；控制器不打开相机。
2. 控制器只操作 CH10，不碰 CH11/CH12。
3. 状态文件发布必须原子；控制器只接受 0.5 s 内的状态。
4. 控制器写 rescue_recovery.txt 后才改变 CH10 功能。
5. 只有 CH10 回 0 且 SERVO10_FUNCTION 恢复确认后才删除恢复记录。
6. rescue_recovery.txt 和旧 route_recovery.txt 的存在都是安全信号，不是临时文件。

## 5. 修改架构时的检查

- 改路径：同时检查 run_rescue.py、视觉的 RUNTIME、控制器的 cwd 和文档。
- 改状态格式：同时改视觉发布、控制器解析、异常处理和本文件。
- 改设备路径：优先用环境变量，保留默认值；不要在没有目标机确认时硬编码新路径。
- 改进程生命周期：检查启动器的 finally、信号处理、超时和单实例锁。
- 任何涉及运行/硬件的验证：先读 [hardware-safety](hardware-safety.md)，不要直接启动。
