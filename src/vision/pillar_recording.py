"""救援模式的两画面录像与逐帧 CSV。

@file pillar_recording.py
@brief 把前视原图+二值图、检测值和控制器状态写入 runtime。
"""
import csv
import time
from pathlib import Path

import cv2


class PillarRecorder:
    """@brief 救援预览录像器；可禁用，禁用时所有方法空操作。"""

    def __init__(self, base, enabled=True):
        """@brief 创建录像目录、AVI 写入器和 CSV 表头。

        @param base 运行时目录。
        @param enabled False 时只保留空对象，不创建目录。
        @exception OSError 目录或 CSV 创建失败。
        """
        self.enabled = bool(enabled)
        self.folder = None
        self.writer = None
        self.file = None
        self.csv = None
        self.index = 0
        if not self.enabled:
            return
        self.folder = Path(base) / ('rescue_video_' + time.strftime('%Y%m%d_%H%M%S'))
        self.folder.mkdir()
        (Path(base) / 'latest_rescue_recording.txt').write_text(str(self.folder))
        self.file = (self.folder / 'frames.csv').open('w', newline='')
        self.csv = csv.writer(self.file)
        self.csv.writerow(['frame', 'capture_monotonic', 'write_monotonic', 'found', 'x',
                           'width_px', 'width_frac', 'aspect', 'fill', 'angle',
                           'state', 'pwm', 'event'])

    def save(self, image, stamp, target, state, pwm, event):
        """@brief 写入一帧两画面 AVI 和 CSV 行。

        @param image 左原图+右二值图的 BGR 画布。
        @param stamp 前视帧采集时间戳。
        @param target PillarTarget 或 None。
        @param state 控制器状态 0..5。
        @param pwm 控制器最近目标 PWM。
        @param event 最近一次撞后事件。
        @exception RuntimeError AVI 无法创建。
        @note AVI 标称 20 fps；真实时间在 CSV。
        """
        if not self.enabled:
            return
        if self.writer is None:
            self.writer = cv2.VideoWriter(
                str(self.folder / 'annotated.avi'),
                cv2.VideoWriter_fourcc(*'MJPG'), 20.,
                (image.shape[1], image.shape[0]))
            if not self.writer.isOpened():
                raise RuntimeError('Cannot create rescue recording')
        self.writer.write(image)
        now = time.monotonic()
        if target is None:
            row = [self.index, '%.9f' % stamp, '%.9f' % now, 0, -1, 0, 0, 0, 0, 0,
                   state, pwm, event]
        else:
            row = [self.index, '%.9f' % stamp, '%.9f' % now, 1, '%.6f' % target.x,
                   '%.3f' % target.width_px, '%.6f' % target.width_frac,
                   '%.4f' % target.aspect, '%.4f' % target.fill, '%.2f' % target.tilt,
                   state, pwm, event]
        self.csv.writerow(row)
        self.file.flush()
        self.index += 1

    def close(self):
        """@brief 释放 AVI/CSV 并写入录像说明；禁用时无操作。"""
        if not self.enabled:
            return
        if self.writer is not None:
            self.writer.release()
        if self.file is not None:
            self.file.close()
        (self.folder / 'README.txt').write_text(
            'Rescue recording: left=front camera, right=binary mask. '
            'AVI is nominal 20 fps; true frame timing is in frames.csv. '
            'Frames reflect detection and commanded PWM, not measured impact.\n'
            'frames=%d\n' % self.index)
