# 2026-10-25 改造为水中救援专用项目

## 目标

按人类最新指示，把原 winner_fish 巡线参考项目改造成水中救援专用项目：

- 旧巡线只作参考，不再保留 route 模式；
- 删除不再使用的下视、曲线、断口、灯、双模式代码；
- 只用前置摄像头，传统视觉检测黑色笔筒；
- GUI 显示前视原图+二值图，滑条调阈值、长宽比、宽度、触发距离和轻撞参数；
- CH10 轻触支柱，撞后用目标消失/宽度突变/位置突变判定命中。

## 已确认的选择

- 首版 1A：单支柱闭环，先验证一个 0.5 kg 支柱。
- 力度非常轻：默认 ±100 us、5 Hz、0.6 s；可降到 ±50，上限 ±300 只作配置。
- 目标顺序：5 号中间、4 号右中；从右侧中心出发中线顺序 4→5→2，不硬编码编号。
- 调参 4A：视觉滑条实时；控制参数写配置文件，控制器启动读取，重启生效。

## 做了什么

1. 新增 src/control/pillar_ram_logic.hpp：SEARCH/ALIGN/RAM/RECOVER/VERIFY/AVOID 纯逻辑状态机。
2. 新增 src/control/rescue_control.cpp：复用旧 Link/MAVLink/预检/恢复记录框架，删除 CH12/灯/route 状态机。
3. 新增 src/vision/pillar_detect.py、rescue_vision.py、pillar_recording.py：黑筒检测、GUI、状态发布、两画面录像。
4. 新增 run_rescue.py、Makefile：唯一入口、预览/--run、默认 290 s、control-check。
5. 新增 tests/cpp/test_pillar_ram.cpp、tests/python/test_pillar_detect.py。
6. 改写 README/AGENTS/backlog/docs/* 和 .dsh/agents/*；删除 docs/legacy 和旧巡线源码/测试/二进制。

## 删除

- src/vision/down_curve.py、gap_light.py、wide_search_vision.py、route_recording.py
- src/control/wide_search_logic.hpp、gap_light_logic.hpp、wide_search_control.cpp
- tests/cpp/test_center100.cpp、test_fixed2s.cpp、test_gap_lamp.cpp、test_zones45_55.cpp
- tests/python/test_down_curve.py、test_gap_light.py
- run_wide_search.py、bin/wide_search_control
- docs/legacy/ 整个目录
- build/tests、build/legacy-bin 旧构建产物

## 证据与验证

- 本机执行 make test-cpp：PILLAR_RAM_LOGIC_TEST_PASS。
- 本机执行 python3 -m py_compile：新 Python 文件语法通过。
- 本机执行 check_docs、make checksums。
- 本机没有 cv2/NumPy、没有 MAVLink 头文件，未编译控制器、未跑视觉测试、未运行相机/GUI/控制器。
- 未做 git commit/push/分支操作。

## 有意留到以后

- 0.5 kg 支柱轻撞可撞倒性、相机/触发宽度标定、撞后事件阈值。
- 完整 5 支柱路线、池壁卡住风险。
- 目标机 OpenCV/NumPy/MAVLink 构建和现场调参。
- 旧 route_recovery.txt 的现场处理。
