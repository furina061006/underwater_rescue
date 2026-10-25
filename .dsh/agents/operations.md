> 返回 [AGENTS.md](../../AGENTS.md) ｜ 状态：【事实】+【设计】+【待验证】
> 本文件是 agent 摘要；构建、运行、恢复的权威定义在 [docs/OPERATIONS.md](../../docs/OPERATIONS.md) 和 [docs/RUNTIME_SEQUENCE.md](../../docs/RUNTIME_SEQUENCE.md)，冲突时以 docs/源码为准。
> 相关：[硬件接口](../../docs/HARDWARE_IO.md)、[控制](control.md)、[视觉](vision.md)、[backlog](../../backlog.md)

# 操作与验证

## 1. 授权状态

【事实】默认只做静态阅读/编辑。任何会打开相机、GUI、串口或操作 CH10 的动作都需要人类当次明确授权。

【事实】make test-cpp、python3 -m py_compile、check_docs、make checksums 不碰硬件。

## 2. 本机可做的检查

~~~bash
make test-cpp
python3 -m py_compile src/vision/pillar_detect.py src/vision/pillar_recording.py src/vision/rescue_vision.py run_rescue.py tests/python/test_pillar_detect.py
python3 .dsh/tools/check_docs.py
make checksums
~~~

本机没有 cv2/NumPy，也没有 MAVLink 头文件；不能编译控制器、不能跑视觉测试、不能运行相机/GUI/控制器。

## 3. 目标机构建

~~~bash
make control
# 或
make control MAVLINK_INC=/path/to/include
~~~

产物：bin/rescue_control。仓库不再自带旧 wide_search_control。

## 4. 预览

~~~bash
python3 run_rescue.py
~~~

只打开前视相机、GUI、写 runtime；不打开飞控串口。按 Q/Esc 退出。

## 5. 正式运行

~~~bash
python3 run_rescue.py --run
python3 run_rescue.py --run --duration 30
~~~

启动器等待新鲜状态后启动控制器；控制器完成预检才输出 CH10。默认 290 s。

## 6. 停止与恢复

- 正常到期：CH10 回 0、恢复 SERVO10_FUNCTION、删 rescue_recovery.txt、写 CSV。
- Ctrl+C：启动器给控制器 SIGINT，控制器走同样收尾；只按一次。
- rescue_recovery.txt 存在时，下一次启动会中止；按 [docs/OPERATIONS.md](../../docs/OPERATIONS.md) 人工确认后处理。
- 旧 route_recovery.txt 存在时，新控制器拒绝启动；不要直接删。

## 7. 调参

- 视觉滑条实时生效；控制滑条写 rescue_tuning.txt，重启 --run 生效。
- 先调 Vmax/Smax/Close/MinArea/Ratio/Wmin/Wmax/Fill/Tilt，再设 Trigger/AimX。
- 轻撞先 ±50/±100；需要再升 RamAmp。
- 每次现场变更后重新预览、短程运行、再全时长。

## 8. 常见故障

| 现象 | 处理 |
| --- | --- |
| missing executable bin/rescue_control | 目标机 make control |
| No fresh camera data | 检查相机 by-id、DISPLAY、视觉进程 |
| CH10 not idle | 确认 CH10 回 0、功能值/MNT1_TYPE |
| SERVO10 range excludes ... | 核对 SERVO10_MIN/MAX 与 rescue_tuning.txt |
| Recovery record exists | 按 OPERATIONS 恢复流程，勿直接删 |
| Rescue vision stale/invalid | 查相机/USB/CPU/录像写盘 |
| SET_SERVO rejected / ACK timeout | 查串口、飞控、参数 |
| Scheduling stalled >60 ms | 关闭无关程序，降低录像负担 |

## 9. 待验证

【待验证】目标机 OpenCV/NumPy 和 MAVLink 构建。
【待验证】前视相机 by-id/安装/触发宽度。
【待验证】0.5 kg 支柱轻撞可撞倒性。
【待验证】完整 5 支柱路线和撞后事件阈值。
