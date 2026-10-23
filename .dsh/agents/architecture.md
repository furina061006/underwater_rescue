> 返回 [AGENTS.md](../../AGENTS.md) ｜ 状态：【事实】为主
> 本文件是 agent 摘要；完整架构的权威定义在 [docs/ARCHITECTURE.md](../../docs/ARCHITECTURE.md)，冲突时以 docs/源码为准。
> 相关：[架构详解](../../docs/ARCHITECTURE.md)、[运行顺序](../../docs/RUNTIME_SEQUENCE.md)、[控制](control.md)、[视觉](vision.md)

# 架构与数据流

## 1. 三个执行体

【事实】运行由一个启动器和两个子进程组成：

```text
run_wide_search.py（启动器）
├── src/vision/wide_search_vision.py（Python 视觉）
└── bin/wide_search_control（C++ 控制）
```

| 执行体 | 主要职责 | 是否碰硬件 |
| --- | --- | --- |
| 启动器 | 单实例锁、运行时目录、拉起/回收进程、打印日志 | 只间接启动子进程 |
| 视觉 | 前视/下视相机、检测、预览、录像、状态发布 | 打开相机和 GUI |
| 控制器 | 串口、MAVLink、预检、50 Hz 控制、灯、收尾 | 打开飞控串口并操作 CH10/CH12 |

【设计】视觉和控制不共享内存，只通过 `runtime/` 下的文本状态文件交换数据；这使视觉算法和飞控控制可以独立阅读、测试和替换。

## 2. 运行时文件

【事实】控制器以 `cwd=runtime` 启动，因此它读写的状态文件和恢复记录都落在运行时目录。

| 文件 | 方向 | 用途 |
| --- | --- | --- |
| `wide_search_state.txt` | 视觉 → 控制 | 前视时间戳、底线位置、上视线标志 |
| `down_curve_state.txt` | 视觉 → 控制 | 下视时间戳、左弧、强度 |
| `gap_light_state.txt` | 视觉 → 控制 | 下视时间戳、断带事件计数 |
| `wide_search_status.txt` | 控制 → 视觉 | 当前阶段、PWM、运行时间 |
| `route_recovery.txt` | 控制器 | CH10 恢复快照；存在时预检中止 |
| 日志/CSV/录像 | 控制器/视觉 | 现场证据，不能随手删 |

【事实】状态文件使用“写 `.tmp` + rename”原子发布；消费者只接受 0.5 s 内的数据。

## 3. 源码职责

| 位置 | 职责 |
| --- | --- |
| `run_wide_search.py` | 启动入口；默认预览，`--run` 才启动控制器 |
| `src/vision/wide_search_vision.py` | 前视 ROI/阈值/底线、下视消费、状态发布、四画面、录像 |
| `src/vision/down_curve.py` | 下视二次曲线、三段航向角、左弧判定 |
| `src/vision/gap_light.py` | 断口条带配对和事件去抖 |
| `src/vision/route_recording.py` | AVI/CSV/转弯关键帧 |
| `src/control/wide_search_control.cpp` | 串口、MAVLink、预检、50 Hz 循环、灯、收尾 |
| `src/control/wide_search_logic.hpp` | 前视巡线 + 下视左弧 2 s 转弯状态机 |
| `src/control/gap_light_logic.hpp` | 断带亮灯 0.5 s 状态机 |

【事实】所有 C++/Python 函数都带 Doxygen 风格注释；修改行为时同步更新注释。

## 4. 必须保持的不变量

【设计】

1. 视觉进程不打开飞控串口；控制器不打开相机。
2. 控制器只操作 CH10/CH12，不碰 CH11 云台。
3. 状态文件发布必须原子；控制器只接受 0.5 s 内的状态。
4. 控制器写 `route_recovery.txt` 后才改变 CH10 功能。
5. 只有 CH10 回 0 且 `SERVO10_FUNCTION` 恢复确认后才删除恢复记录。
6. `route_recovery.txt` 的存在是安全信号，不是临时文件。

## 5. 修改架构时的检查

- 改路径：同时检查 `run_wide_search.py`、视觉的 `RUNTIME`、控制器的 `cwd` 和文档。
- 改状态格式：同时改视觉发布、控制器解析、异常处理和 `.dsh/agents/architecture.md`。
- 改设备路径：优先用环境变量，保留默认值；不要在没有目标机确认时硬编码新路径。
- 改进程生命周期：检查启动器的 finally、信号处理、超时和单实例锁。
- 任何涉及运行/硬件的验证：先读 [hardware-safety](hardware-safety.md)，不要直接启动。
