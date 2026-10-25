# underwater_rescue backlog

> 只列未完成/未知事项；完成即删除。每条写清楚：缺什么、谁能提供、阻塞什么。
> 相关规则：[AGENTS.md](AGENTS.md) ｜ 硬件安全：[.dsh/agents/hardware-safety.md](.dsh/agents/hardware-safety.md)

## 现场验证

- **0.5 kg 支柱轻撞可撞倒性**：默认 ±100 us / 0.6 s 轻触能否让顶部盒子落水未验证；需要水池/台架实测，必要时升到 ±200/±300。阻塞正式比赛策略。
- **相机安装与触发宽度标定**：前视相机高度/俯角/FOV 未记录；需要在 2 m/1 m/0.5 m 静态支柱下记录 W%，再定 Trigger 和 AimX。阻塞自动冲撞距离。
- **撞后事件阈值**：目标消失/宽度变化 30%/x 变化 15% 只是视觉代理；需要录像/CSV 标定，避免把近距离遮挡误判为命中。
- **完整 5 支柱路线**：只有前视、无航向/全局定位；4→5→2 中线之外的 1/3 搜索未验证。阻塞全项目标。
- **池壁/卡住风险**：单尾无倒车；撞 2 号后可能贴住池壁，AVOID 是否足够未验证。

## 目标机环境

- **MAVLink 头文件**：开发机没有 /opt/ros/noetic/include/mavlink；控制器只在目标机 make control。
- **OpenCV/NumPy**：开发机 cv2 缺失、NumPy 不可用；视觉测试只在目标机 make test-python。
- **新二进制**：仓库不再自带 ARM64 控制器；目标机必须先 make control 生成 bin/rescue_control。
- **舵机热/电流**：如果现场从 ±100 升到 ±300，需要确认舵机/尾机构温度和限位。
- **CH10 参数**：确认 SERVO10_MIN≤pwm_min、SERVO10_MAX≥pwm_max、SERVO_RATE=50；默认 1200/1800。

## 其他

- **旧恢复记录**：如果现场还存在 runtime/route_recovery.txt，新控制器会拒绝启动；需要人工按 OPERATIONS 处理。
- **git 工作流**：未与人类确认 commit/push/分支策略；当前只做工作区修改。
