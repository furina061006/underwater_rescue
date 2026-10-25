> 返回 [AGENTS.md](../../AGENTS.md) ｜ 状态：【事实】+【设计】+【待验证】
> 本文件是 agent 摘要；阈值和算法的权威定义在源码与 [docs/ARCHITECTURE.md](../../docs/ARCHITECTURE.md)，冲突时以源码为准。
> 相关：[硬件接口](../../docs/HARDWARE_IO.md)、[控制](control.md)、[backlog](../../backlog.md)

# 视觉子系统

## 1. 文件与职责

| 文件 | 职责 |
| --- | --- |
| src/vision/rescue_vision.py | 前视采集、滑条 GUI、状态发布、录像 |
| src/vision/pillar_detect.py | 黑色掩码、闭运算、minAreaRect、过滤、跟踪 |
| src/vision/pillar_recording.py | 两画面 AVI + CSV + 录像说明 |

## 2. 前视管线

【事实】

1. 相机帧读取前先取时间戳，避免阻塞读把旧帧误判为新鲜。
2. 使用全画面 640×480，不裁 ROI。
3. 黑色掩码：HSV V≤Vmax（默认 100）、S≤Smax（默认 120）。
4. 椭圆核闭运算默认 9×9，填黑色网格孔洞；再 3×3 开运算。
5. minAreaRect 取 W=短边、H=长边；R=H/W；width_frac=W/画宽；x=中心/画宽。
6. 过滤：面积≥300、W∈[12,500]、R∈[1.6,6.0]、fill≥45%、|tilt|≤25°。
7. 多目标选 W 最大（最近）；同宽选离 AimX 最近。
8. 跟踪：连续 3 帧相邻才确认；丢失保持 0.3 s；x/width_frac EMA（α=0.5）。
9. 发布 rescue_state.txt：stamp found x width_frac aspect；原子 rename。
10. found=0 时 x=-1、width_frac=0、aspect=0。

【设计】检测只做传统视觉，不用深度学习。阈值/长宽比由现场滑条调，不写死到源码。

## 3. GUI 与调参

【事实】

- 左前视原图 + 右二值图；叠加 minAreaRect、中心点、AimX 竖线、W%/R/fill/tilt、状态/PWM/事件/FPS。
- Vision 滑条实时生效；Control 滑条写 rescue_tuning.txt，控制器启动时读取。
- 按键 s 保存快照；q/Esc 退出。
- 无 DISPLAY/XAUTHORITY 时明确报错，不静默降级。

【设计】控制参数不在运行中实时改；重启 --run 才生效，避免现场误操作。

## 4. 修改视觉时的规则

【设计】

1. 先读 [架构详解](../../docs/ARCHITECTURE.md) 的视觉章节和 [硬件接口](../../docs/HARDWARE_IO.md) 的阈值语义。
2. 不要把控制参数偷偷接进视觉状态；rescue_state.txt 只发布检测量。
3. 不要为了“让画面好看”改变 x/width_frac/aspect 的语义。
4. 相机路径优先通过 RESCUE_FRONT_CAMERA 覆盖。
5. 预览窗口不是“没有副作用”；它会打开相机、创建 GUI、写 runtime/，默认禁止运行。
6. 视觉测试需要 OpenCV/NumPy 和构造图像；默认不跑，需明确授权。

## 5. 待验证

【待验证】本机没有 cv2/NumPy，视觉测试未在开发机执行。

【待验证】目标机前视相机 by-id、曝光/白平衡、俯角和触发宽度未标定。

【待验证】黑色网格孔洞、水面反光、透明水杯在真实水池中的检测效果未验证。
