"""救援黑筒检测与跟踪的合成图像测试。

@file test_pillar_detect.py
@brief 构造蓝底+黑色网格矩形，验证检测、过滤和跟踪丢失。
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'src' / 'vision'))
import cv2
import numpy as np
from pillar_detect import PillarParams, PillarTracker, detect_pillar


def blue_image():
    """@brief 生成 640x480 蓝色水面背景。"""
    return np.full((480, 640, 3), (200, 120, 40), np.uint8)


def make_pillar(width=120, height=270, x=260, y=150):
    """@brief 在蓝底上画黑色笔筒，并用蓝色网格线模拟网孔。"""
    img = blue_image()
    cv2.rectangle(img, (x, y), (x + width, y + height), (0, 0, 0), -1)
    for gx in range(x + 20, x + width, 20):
        cv2.line(img, (gx, y + 5), (gx, y + height - 5), (200, 120, 40), 2)
    for gy in range(y + 20, y + height, 20):
        cv2.line(img, (x + 5, gy), (x + width - 5, gy), (200, 120, 40), 2)
    return img


p = PillarParams()
target, mask = detect_pillar(make_pillar(), p)
assert target is not None, 'main pillar not detected'
assert abs(target.x - .5) < .05, target.x
assert abs(target.width_frac - 120 / 640.0) < .03, target.width_frac
assert abs(target.aspect - 2.25) < .6, target.aspect
assert target.fill > .5, target.fill

small, _ = detect_pillar(make_pillar(width=10, height=30), p)
assert small is None, 'small target must be filtered'
square, _ = detect_pillar(make_pillar(width=120, height=120), p)
assert square is None, 'square target must be filtered by aspect'
empty, _ = detect_pillar(blue_image(), p)
assert empty is None, 'empty water must not detect'

tracker = PillarTracker()
assert tracker.update(target, 1.0, p) is None
assert tracker.update(target, 1.1, p) is None
held = tracker.update(target, 1.2, p)
assert held is not None and not held.held
held = tracker.update(None, 1.3, p)
assert held is not None and held.held
assert tracker.update(None, 1.7, p) is None

print('PILLAR_DETECT_TEST_PASS')
