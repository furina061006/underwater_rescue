"""黑色笔筒支柱的检测与时间跟踪。

只用传统视觉：HSV 黑色掩码、形态学闭运算填网格孔洞、minAreaRect
取宽/高/长宽比，再做面积、宽度、填充率、倾角过滤。

@file pillar_detect.py
@brief 黑色笔筒检测、候选筛选和跨帧跟踪。
"""
import math
from dataclasses import dataclass, replace

import cv2
import numpy as np


@dataclass
class PillarParams:
    """@brief 检测与跟踪参数；GUI 滑条直接映射到这些字段。"""
    v_max: int = 100
    s_max: int = 120
    close: int = 9
    min_area: int = 300
    ratio_min: float = 1.6
    ratio_max: float = 6.0
    width_min: int = 12
    width_max: int = 500
    fill_min: float = .45
    tilt_max: float = 25.0
    aim_x: float = .50
    track_lost: float = .30


@dataclass
class PillarTarget:
    """@brief 一帧检测结果。width_frac 和 x 都用整幅画面归一化。"""
    x: float
    y: float
    width_px: float
    height_px: float
    aspect: float
    fill: float
    tilt: float
    angle: float
    rect: tuple
    width_frac: float
    held: bool = False


def build_mask(frame, params):
    """@brief 生成黑色目标掩码。

    @param frame BGR 前视帧。
    @param params PillarParams。
    @return 单通道 uint8 掩码，非零像素为黑色候选。
    @note 先闭运算填黑色网格孔洞，再 3x3 开运算去孤立噪点。
    """
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    upper = (179, max(0, min(255, int(params.s_max))), max(0, min(255, int(params.v_max))))
    mask = cv2.inRange(hsv, (0, 0, 0), upper)
    k = max(1, int(params.close))
    if k % 2 == 0:
        k += 1
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (k, k))
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, np.ones((3, 3), np.uint8))
    return mask


def _long_edge_tilt(rect):
    """@brief 计算 minAreaRect 长边相对竖直方向的夹角。

    @param rect cv2.minAreaRect 返回值。
    @return (tilt_deg, rect_angle)。
    @note tilt=0 表示长边竖直；tilt=90 表示长边水平。
    """
    box = cv2.boxPoints(rect)
    edges = []
    for i in range(4):
        a = box[i]
        b = box[(i + 1) % 4]
        dx, dy = float(b[0] - a[0]), float(b[1] - a[1])
        edges.append((dx * dx + dy * dy, dx, dy))
    _, dx, dy = max(edges, key=lambda e: e[0])
    deg = abs(math.degrees(math.atan2(dy, dx)))
    return abs(90.0 - deg), float(rect[2])


def detect_pillar(frame, params, mask=None):
    """@brief 在单帧中检测最可能的黑色笔筒。

    @param frame BGR 前视帧。
    @param params PillarParams。
    @param mask 可选的已生成掩码，避免 GUI 重复计算。
    @return (PillarTarget 或 None, mask)。
    @note 多目标时选短边 W 最大者（最近）；同宽选离 aim_x 最近者。
    """
    if mask is None:
        mask = build_mask(frame, params)
    h, w = mask.shape[:2]
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
    best = None
    for c in contours:
        area = float(cv2.contourArea(c))
        if area < params.min_area:
            continue
        rect = cv2.minAreaRect(c)
        (cx, cy), (rw, rh), angle = rect
        short, long = sorted((float(rw), float(rh)))
        if short < 1.0 or long < 1.0:
            continue
        width_frac = short / max(w, 1)
        if short < params.width_min or short > params.width_max:
            continue
        aspect = long / short
        if aspect < params.ratio_min or aspect > params.ratio_max:
            continue
        fill = area / max(short * long, 1.0)
        if fill < params.fill_min:
            continue
        tilt, rect_angle = _long_edge_tilt(rect)
        if tilt > params.tilt_max:
            continue
        target = PillarTarget(
            x=cx / max(w, 1), y=cy / max(h, 1),
            width_px=short, height_px=long, aspect=aspect,
            fill=fill, tilt=tilt, angle=rect_angle, rect=rect,
            width_frac=width_frac)
        if best is None or target.width_px > best.width_px:
            best = target
        elif abs(target.width_px - best.width_px) < 1.0:
            if abs(target.x - params.aim_x) < abs(best.x - params.aim_x):
                best = target
    return best, mask


class PillarTracker:
    """@brief 把逐帧候选变成稳定目标，处理确认、平滑和短暂丢失。"""

    def __init__(self):
        """@brief 初始化跟踪状态。"""
        self.accepted = None
        self.last_raw = None
        self.last_stamp = 0.0
        self.confirm = 0

    def reset(self):
        """@brief 清空跟踪状态。"""
        self.accepted = None
        self.last_raw = None
        self.last_stamp = 0.0
        self.confirm = 0

    def update(self, target, stamp, params):
        """@brief 推进一步跟踪。

        @param target 当前帧检测结果或 None。
        @param stamp 当前帧单调时间戳。
        @param params PillarParams。
        @return 稳定目标（可能 held=True）或 None。
        @note 连续 3 帧中心/宽度接近才确认；丢失后保持 track_lost 秒。
        """
        if target is None:
            if self.accepted is not None and stamp - self.last_stamp <= params.track_lost:
                return replace(self.accepted, held=True)
            self.reset()
            return None

        close = (self.last_raw is not None and
                 abs(target.x - self.last_raw.x) <= .125 and
                 .5 * self.last_raw.width_px <= target.width_px <= 2.0 * self.last_raw.width_px)
        self.confirm = self.confirm + 1 if close else 1
        self.last_raw = target
        self.last_stamp = stamp
        if self.confirm < 3:
            if self.accepted is not None and stamp - self.last_stamp <= params.track_lost:
                return replace(self.accepted, held=True)
            return None
        if self.accepted is None:
            self.accepted = target
        else:
            a = .5
            self.accepted = replace(
                target,
                x=a * target.x + (1 - a) * self.accepted.x,
                width_px=a * target.width_px + (1 - a) * self.accepted.width_px,
                width_frac=a * target.width_frac + (1 - a) * self.accepted.width_frac,
                aspect=a * target.aspect + (1 - a) * self.accepted.aspect,
                fill=a * target.fill + (1 - a) * self.accepted.fill,
                held=False)
        return self.accepted
