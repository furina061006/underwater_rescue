"""录制带标注的四画面预览，并保留每次转弯前后的关键帧。

@file route_recording.py
@brief AVI/CSV 录像与转弯关键帧保存。
"""
import csv
import time
from collections import deque
from pathlib import Path
import cv2

class Recorder:
    """@brief 把视觉主循环的标注画面和检测值写入运行时目录。"""

    def __init__(self, base):
        """@brief 创建本次运行的录像目录和 CSV 表头。

        @param base 运行时目录（通常是 runtime/）。
        @exception OSError 目录创建或文件打开失败。
        @note 同时更新 latest_route_recording.txt 指向本次录像目录。
        """
        self.folder = base / ('route_video_' + time.strftime('%Y%m%d_%H%M%S'))
        self.folder.mkdir()
        (base / 'latest_route_recording.txt').write_text(str(self.folder))
        self.writer = None
        self.file = (self.folder/'frames.csv').open('w', newline='')
        self.csv = csv.writer(self.file)
        self.csv.writerow(['frame','capture_monotonic','write_monotonic','stage','pwm','route_elapsed_s','bottom_x_fraction','horizontal','angle_degrees'])
        self.buffer = deque(maxlen=120)
        self.pending = []
        self.seen = set()
        self.index = 0

    def save(self, image, stamp, stage, pwm, elapsed, x, horizontal, angle):
        """@brief 写入一帧四画面录像、CSV 行，并处理转弯关键帧。

        @param image 带标注的 BGR 四画面图像。
        @param stamp 前视帧采集时间戳，单位秒。
        @param stage 控制器阶段（1=巡线，2=固定转弯）。
        @param pwm 控制器当前目标 PWM。
        @param elapsed 本次任务已运行秒数。
        @param x 前视底线横向比例；-1 表示没有底线。
        @param horizontal 是否拟合出上视线。
        @param angle 上视线角度；无拟合时为空字符串。
        @exception RuntimeError 无法创建 AVI 或 JPEG 编码失败。
        @note AVI 按 20 fps 标称写入，真实帧时间以 frames.csv 为准；
        首次看到 stage=2 时保存触发前后关键帧，并不代表舵机物理到位。
        """
        if self.writer is None:
            self.writer = cv2.VideoWriter(str(self.folder/'annotated.avi'),cv2.VideoWriter_fourcc(*'MJPG'),20.,(image.shape[1],image.shape[0]))
            if not self.writer.isOpened():raise RuntimeError('Cannot create recording')
        self.writer.write(image)
        now=time.monotonic()
        self.csv.writerow([self.index,'%.9f'%stamp,'%.9f'%now,stage,pwm,elapsed,x,horizontal,angle])
        self.file.flush()
        ok,jpg=cv2.imencode('.jpg',image,[cv2.IMWRITE_JPEG_QUALITY,92])
        if not ok:raise RuntimeError('Frame encoding failed')
        self.buffer.append((stamp,self.index,jpg))
        if stage in (2,4) and stage not in self.seen:
            self.seen.add(stage)
            folder=self.folder/('turn_%d'%((stage//2)))
            folder.mkdir()
            (folder/'trigger.txt').write_text('First camera frame observing stage=%d, PWM=%d, route_elapsed=%.6f, capture_monotonic=%.9f. This is frame-level alignment, not exact servo motion feedback.\n'%(stage,pwm,elapsed,stamp))
            for dt,label in [(-.5,'before_500ms'),(-.1,'before_100ms'),(0,'trigger')]:
                row=min(self.buffer,key=lambda row:abs(row[0]-(stamp+dt)))
                (folder/(label+'.jpg')).write_bytes(row[2].tobytes())
            self.pending.extend([(stamp+.1,folder/'after_100ms.jpg'),(stamp+.5,folder/'after_500ms.jpg')])
        for target,path in self.pending[:]:
            if stamp>=target:
                path.write_bytes(jpg.tobytes())
                self.pending.remove((target,path))
        self.index+=1

    def close(self):
        """@brief 释放 AVI 写入器、关闭 CSV 并写入录像说明。

        @note 应在视觉主循环的 finally 中调用。
        """
        if self.writer is not None:self.writer.release()
        self.file.close()
        (self.folder/'README.txt').write_text('Annotated camera+binary-mask recording; green line=upper fit, red dot=binary bottom, blue lines=45/55%% width.\nAVI nominal playback is 20 fps; true frame timing is in frames.csv. Turn folders preserve before/first stage-observed/after frames.\nFrames reflect detection and commanded PWM, not measured physical servo angle.\nFrames=%d; turn events=%s\n'%(self.index,sorted(self.seen)))
