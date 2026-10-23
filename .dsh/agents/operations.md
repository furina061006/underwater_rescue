> 返回 [AGENTS.md](../../AGENTS.md) ｜ 状态：【事实】+【设计】+【待验证】
> 本文件是 agent 摘要；完整操作和排障步骤在 [docs/OPERATIONS.md](../../docs/OPERATIONS.md)，冲突时以 docs/源码为准。
> 相关：[操作手册](../../docs/OPERATIONS.md)、[硬件安全](hardware-safety.md)、[控制](control.md)、[backlog](../../backlog.md)

# 构建、运行与环境

## 1. 当前环境事实

【事实】目标机是 Jetson ARM64，使用 ArduSub/Pixhawk6X；运行需要 Python 3、OpenCV、NumPy、显示环境，以及 MAVLink v2 头文件。

【事实】仓库自带 `bin/wide_search_control` 是 ARM64 预编译二进制；x86 开发机不能直接运行。

【事实】截至 2026-10-23，开发机的默认 `python3` 不能导入 `cv2`，NumPy 也不可用；没有在目标机验证视觉环境。

【事实】截至 2026-10-23，开发机有 conda 环境 `aquacore`，用户表示其中包含部分依赖；尚未验证其包完整性，也未用它运行视觉。

【事实】控制器完整编译需要 `/opt/ros/noetic/include/mavlink/v2.0/ardupilotmega/mavlink.h`；当前开发机没有该头文件。

## 2. 默认允许的操作

【设计】以下操作不接触硬件，可以在需要时进行：

- 阅读源码和文档。
- 运行 `make test-cpp`（纯逻辑测试，不打开相机/串口）。
- Python AST 或语法检查（不导入 OpenCV）。
- 运行 `python3 .dsh/tools/check_docs.py`。
- 静态检查链接、表格和文件结构。

【设计】即使这些操作安全，也不要为了“多验证一点”反复跑整套测试；用户已明确表示当前只需要理解结构。

## 3. 默认禁止的操作

【事实】以下操作会碰硬件或现场数据，默认禁止：

- `python3 run_wide_search.py`（包括预览）。
- `python3 run_wide_search.py --run`。
- `make preview`、`make run`。
- 运行 `bin/wide_search_control`，包括 `--dry-run`。
- 打开相机、创建 OpenCV 窗口、占用飞控串口。
- QGC、MAVProxy、mavlink shell、参数写入、arm/disarm、模式切换。
- 删除 `runtime/` 下的日志、录像、CSV 或 `route_recovery.txt`。

需要执行其中任何一项时，先读 [hardware-safety](hardware-safety.md) 并取得人类明确授权。

## 4. 构建

【事实】

```bash
make control
# 如果 MAVLink 头文件不在默认位置：
make control MAVLINK_INC=/path/to/include
```

【设计】编译本身不操作硬件，但会改写 `bin/wide_search_control`；只有在人类要求构建时执行。

## 5. 运行与停止（仅授权后）

【事实】授权后的标准入口：

```bash
python3 run_wide_search.py          # 预览
python3 run_wide_search.py --run    # 控制器，默认 300 s
```

【设计】运行前必须完成 [hardware-safety](hardware-safety.md) 第 5 节检查；运行中不并发使用 QGC，不改参数。

【事实】Ctrl+C 只按一次；启动器给控制器 SIGINT，控制器关灯、CH10 回 0、恢复 `SERVO10_FUNCTION`，最后释放视觉。

## 6. 输出与恢复

【事实】运行时文件都在 `runtime/`：

- `wide_search_<ts>.log`：控制器日志；
- `wide_search_<ts>.csv`：50 Hz 采样；
- `route_video_<ts>/`：四画面 AVI、帧 CSV、下视曲线 CSV、转弯关键帧；
- `route_recovery.txt`：恢复记录，存在即代表上次未安全收尾。

【设计】日志/录像可能是在场证据，不删除、不覆盖；需要归档时先问。

【事实】恢复记录存在时，控制器会在预检阶段中止，不接管 CH10。处理步骤见 [操作手册](../../docs/OPERATIONS.md) 的恢复章节，不要直接删文件。

## 7. 测试与完整性

【事实】

```bash
make test-cpp      # 纯逻辑，安全
make test-python   # 需要 OpenCV + NumPy，默认不跑
make test          # 两者都跑
make checksums     # 只通过该命令生成完整性清单
sha256sum -c SHA256SUMS.txt
```

【设计】`make checksums` 不碰硬件；知识库/文档变更可以直接重算，源码或二进制变更先说明范围并取得人类同意。

## 8. Git 与环境注意事项

【事实】仓库当前有 `main` 基线提交；工作区可能有未提交改动，不要假设它是干净的。

【设计】不要自行 commit、push、merge、rebase、切分支；分支工作流尚未与人类确认。详见 [backlog](../../backlog.md)。

【事实】当前开发机不是目标机；在 x86 上通过只代表静态/纯逻辑，不能推广到 Jetson 或硬件。

## 9. 排障入口

- 硬件参数/通道： [硬件接口](../../docs/HARDWARE_IO.md)
- 完整运行顺序和异常路径： [运行顺序](../../docs/RUNTIME_SEQUENCE.md)
- 常见错误与恢复： [操作手册](../../docs/OPERATIONS.md)
- 已知未完成项： [backlog](../../backlog.md)
