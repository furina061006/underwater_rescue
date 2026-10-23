> 返回 [AGENTS.md](../../AGENTS.md) ｜ 状态：【事实】+【设计】+【待验证】
> 本文件是 agent 摘要；阈值和算法的权威定义在源码与 [docs/HARDWARE_IO.md](../../docs/HARDWARE_IO.md)，冲突时以源码为准。
> 相关：[架构详解](../../docs/ARCHITECTURE.md)、[硬件接口](../../docs/HARDWARE_IO.md)、[控制](control.md)、[backlog](../../backlog.md)

# 视觉子系统

## 1. 文件与职责

| 文件 | 职责 |
| --- | --- |
| `src/vision/wide_search_vision.py` | 相机采集、前视 ROI/阈值/底线、状态发布、四画面、录像 |
| `src/vision/down_curve.py` | 下视连通域、二次曲线、三段航向、左弧判定 |
| `src/vision/gap_light.py` | 断口条带配对、几何筛选、`GapEvent` 去抖 |
| `src/vision/route_recording.py` | `annotated.avi`、`frames.csv`、`down_curve.csv`、转弯关键帧 |

## 2. 前视管线

【事实】

1. 相机帧读取前先取时间戳，避免阻塞读把旧帧误判为新鲜。
2. ROI 为画面右侧 80%（横向 20%–100%）。
3. HSV 阈值默认 `S ≤ 60`、`V ≥ 220`，滑条可调但不持久化。
4. 取面积最大的连通域，用最底部 5 行的 x 中位数得到底线位置。
5. 没有合格连通域时发布 `x=-1`。
6. `upper_line()` 在 ROI 下半部拟合长条轮廓，**只用于预览，不参与控制**。
7. 发布格式：`front_stamp x horizontal any`，原子 rename。

【设计】改前视算法时，必须保持“控制器只依赖底线 x 和下视状态”这一接口；不要把预览专用量（上视线角度）偷偷接进控制。

## 3. 下视管线

【事实】

- 取面积 ≥80、高度 ≥30% 画面的最大连通域。
- 每 3 行取像素 x 中位数，做二次拟合；残差、连通域尺寸不满足时返回 `None`。
- 分近/中/远三段求航向角；`delta=far-near`。
- 左弧条件：`delta ≤ -12°`、`offset ≤ -0.03`、`curvature < 0`；强度来自 `-offset/0.5`。
- 曲率是图像平面 `px⁻¹`，不是物理半径。
- 下视不新鲜时发布时间戳 0；控制器据此关灯、取消左弧候选，并在转弯中中止。

【设计】下视数据过期时不能“沿用上一帧继续转弯”；这条由控制器的 `HorizontalLossRoute` 强制执行。

## 4. 断口与录像

【事实】

- 断口长度 8 px–45% 画面高度；两侧条带方向差 ≤15°；至少 80% 采样点为背景。
- `GapEvent` 连续检测 0.1 s 才增加事件；连续消失 0.5 s 才重新武装。
- 录像为四画面 AVI（标称 20 fps），真实帧时间在 `frames.csv`。
- 首次看到 stage=2 时保存触发前后关键帧；这是帧级对齐，不是舵机物理到位反馈。

## 5. 修改视觉时的规则

【设计】

1. 先读 [架构详解](../../docs/ARCHITECTURE.md) 的视觉章节和 [硬件接口](../../docs/HARDWARE_IO.md) 的阈值语义。
2. 阈值/几何条件变化会改变控制行为；默认视为冻结区，需人类确认和目标机验证计划。
3. 不要为了“让画面好看”改变发布给控制器的 `x`、`left`、`strength` 语义。
4. 相机路径优先通过 `WIDE_SEARCH_FRONT_CAMERA` / `WIDE_SEARCH_DOWN_CAMERA` 覆盖。
5. 不要把预览窗口当作“没有副作用”；它会打开相机、创建 GUI、写 `runtime/`，默认禁止运行。
6. 视觉测试需要 OpenCV/NumPy 和构造掩码；默认不跑，需明确授权。

## 6. 待验证

【待验证】本机没有验证 `cv2`/`numpy` 可用性，视觉测试未在目标机执行；详见 [backlog](../../backlog.md)。

【待验证】目标机相机 `by-id` 路径未确认；详见 [backlog](../../backlog.md)。

【待验证】视觉阈值和几何条件在真实水池/场地的效果未重新标定；本版本沿用冻结值。
