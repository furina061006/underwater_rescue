"""救援模式前视视觉主循环：检测、GUI、状态发布和录像。

只打开前置摄像头；不启动下视、不跑曲线/断口、不控制 CH12。
窗口显示原图+二值图，滑条实时调检测阈值和长宽比；控制参数写入
runtime/rescue_tuning.txt，由控制器启动时读取。

@file rescue_vision.py
@brief 水中救援前视检测、调参 GUI、状态发布与录像。
"""
import argparse
import os
import signal
import time
from pathlib import Path

import cv2
import numpy as np

from pillar_detect import PillarParams, PillarTracker, build_mask, detect_pillar
from pillar_recording import PillarRecorder

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = Path(os.environ.get('RESCUE_RUNTIME',
                              os.environ.get('WIDE_SEARCH_RUNTIME', ROOT / 'runtime'))).resolve()
FRONT_CAMERA = os.environ.get(
    'RESCUE_FRONT_CAMERA',
    os.environ.get('WIDE_SEARCH_FRONT_CAMERA',
                   '/dev/v4l/by-id/usb-H264_USB_Camera_H264_USB_Camera_2020032801-video-index0'))

WIN = 'Rescue | front camera'
WIN_V = 'Rescue | vision tuning'
WIN_C = 'Rescue | control tuning'

VISION_BARS = [
    ('Vmax', 255, 100), ('Smax', 255, 120), ('Close', 31, 9),
    ('MinArea', 5000, 300), ('RatioMin x10', 60, 16), ('RatioMax x10', 100, 60),
    ('Wmin px', 200, 12), ('Wmax px', 640, 500), ('FillMin %', 95, 45),
    ('TiltMax', 45, 25), ('AimX %', 70, 50), ('TrackLost x10', 20, 3),
]
CONTROL_BARS = [
    ('Trigger %', 60, 20), ('Deadband %', 30, 8), ('SteerGain', 800, 300),
    ('CruiseAmp', 200, 100), ('ScanBias', 300, 150), ('RamAmp', 300, 100),
    ('RamFreq x10', 80, 50), ('RamBurst x10', 20, 6), ('Recover x10', 20, 6),
    ('Verify x10', 20, 5), ('HitW %', 60, 30), ('HitX %', 30, 15),
    ('RetryMax', 4, 3),
]


def publish_state(stamp, found, x, width_frac, aspect):
    """@brief 原子发布 rescue_state.txt。"""
    tmp = RUNTIME / 'rescue_state.tmp'
    tmp.write_text('%.9f %d %.6f %.6f %.6f\n' % (stamp, 1 if found else 0, x, width_frac, aspect))
    tmp.replace(RUNTIME / 'rescue_state.txt')


def publish_tuning(text):
    """@brief 原子发布 rescue_tuning.txt。"""
    tmp = RUNTIME / 'rescue_tuning.tmp'
    tmp.write_text(text)
    tmp.replace(RUNTIME / 'rescue_tuning.txt')


def _pos(window, name):
    """@brief 读取滑条整数值。"""
    return int(cv2.getTrackbarPos(name, window))


def create_trackbars():
    """@brief 创建视觉和控制调参滑条。"""
    for name, maxv, default in VISION_BARS:
        cv2.createTrackbar(name, WIN_V, default, maxv, lambda _: None)
    for name, maxv, default in CONTROL_BARS:
        cv2.createTrackbar(name, WIN_C, default, maxv, lambda _: None)


def read_params():
    """@brief 从滑条读取视觉参数和控制参数。

    @return (PillarParams, control_dict)。
    """
    close = _pos(WIN_V, 'Close')
    if close % 2 == 0:
        close += 1
    close = max(1, min(31, close))
    ratio_min = max(.5, _pos(WIN_V, 'RatioMin x10') / 10.0)
    ratio_max = max(ratio_min + .1, _pos(WIN_V, 'RatioMax x10') / 10.0)
    p = PillarParams(
        v_max=_pos(WIN_V, 'Vmax'), s_max=_pos(WIN_V, 'Smax'), close=close,
        min_area=_pos(WIN_V, 'MinArea'), ratio_min=ratio_min, ratio_max=ratio_max,
        width_min=_pos(WIN_V, 'Wmin px'), width_max=_pos(WIN_V, 'Wmax px'),
        fill_min=_pos(WIN_V, 'FillMin %') / 100.0,
        tilt_max=float(_pos(WIN_V, 'TiltMax')),
        aim_x=_pos(WIN_V, 'AimX %') / 100.0,
        track_lost=max(.1, _pos(WIN_V, 'TrackLost x10') / 10.0))
    c = {
        'trigger_width': max(.02, min(.8, _pos(WIN_C, 'Trigger %') / 100.0)),
        'deadband': min(.5, _pos(WIN_C, 'Deadband %') / 100.0),
        'steer_gain': float(_pos(WIN_C, 'SteerGain')),
        'cruise_amp': max(30.0, float(_pos(WIN_C, 'CruiseAmp'))),
        'scan_bias': float(_pos(WIN_C, 'ScanBias')),
        'ram_amp': max(30.0, float(_pos(WIN_C, 'RamAmp'))),
        'ram_freq': max(1.0, _pos(WIN_C, 'RamFreq x10') / 10.0),
        'ram_burst': max(.2, _pos(WIN_C, 'RamBurst x10') / 10.0),
        'recover_s': max(.2, _pos(WIN_C, 'Recover x10') / 10.0),
        'verify_s': max(.1, _pos(WIN_C, 'Verify x10') / 10.0),
        'hit_width_delta': max(.01, _pos(WIN_C, 'HitW %') / 100.0),
        'hit_x_delta': max(.01, _pos(WIN_C, 'HitX %') / 100.0),
        'retry_max': _pos(WIN_C, 'RetryMax'),
        'cruise_freq': 4.0, 'scan_period': 4.0, 'lost_s': .30, 'confirm_s': .10,
        'avoid_s': 1.5, 'avoid_bias': 200.0, 'pwm_min': 1200.0, 'pwm_max': 1800.0,
    }
    return p, c


def format_tuning(p, c):
    """@brief 把视觉/控制参数格式化成 key=value 文本。"""
    lines = [
        'v_max=%d' % p.v_max, 's_max=%d' % p.s_max, 'close=%d' % p.close,
        'min_area=%d' % p.min_area, 'ratio_min=%.4f' % p.ratio_min,
        'ratio_max=%.4f' % p.ratio_max, 'width_min=%d' % p.width_min,
        'width_max=%d' % p.width_max, 'fill_min=%.4f' % p.fill_min,
        'tilt_max=%.2f' % p.tilt_max, 'aim_x=%.4f' % p.aim_x,
        'track_lost=%.4f' % p.track_lost,
        'trigger_width=%.4f' % c['trigger_width'], 'deadband=%.4f' % c['deadband'],
        'steer_gain=%.2f' % c['steer_gain'], 'cruise_amp=%.2f' % c['cruise_amp'],
        'cruise_freq=%.2f' % c['cruise_freq'], 'scan_bias=%.2f' % c['scan_bias'],
        'scan_period=%.2f' % c['scan_period'], 'lost_s=%.2f' % c['lost_s'],
        'confirm_s=%.2f' % c['confirm_s'], 'ram_amp=%.2f' % c['ram_amp'],
        'ram_freq=%.2f' % c['ram_freq'], 'ram_burst=%.2f' % c['ram_burst'],
        'recover_s=%.2f' % c['recover_s'], 'verify_s=%.2f' % c['verify_s'],
        'avoid_s=%.2f' % c['avoid_s'], 'avoid_bias=%.2f' % c['avoid_bias'],
        'hit_width_delta=%.4f' % c['hit_width_delta'],
        'hit_x_delta=%.4f' % c['hit_x_delta'], 'retry_max=%d' % c['retry_max'],
        'pwm_min=%d' % c['pwm_min'], 'pwm_max=%d' % c['pwm_max'],
    ]
    return '\n'.join(lines) + '\n'


def read_status():
    """@brief 读取控制器状态回显。

    @return (state, pwm, width_frac, event, fresh)；文件缺失/过期时 fresh=False。
    """
    try:
        parts = (RUNTIME / 'rescue_status.txt').read_text().split()
        if len(parts) >= 6:
            age = time.monotonic() - float(parts[0])
            if 0 <= age < .5:
                return (int(float(parts[1])), int(float(parts[2])),
                        float(parts[4]), int(float(parts[5])), True)
    except (OSError, ValueError):
        pass
    return 0, 0, 0.0, 0, False


def draw(frame, mask, target, p, state, pwm, event, fresh, fps):
    """@brief 生成左原图+右二值图的显示画布。"""
    h, w = frame.shape[:2]
    left = frame.copy()
    right = cv2.cvtColor(mask, cv2.COLOR_GRAY2BGR)
    aim = int(round(p.aim_x * w))
    for img in (left, right):
        cv2.line(img, (aim, 0), (aim, h - 1), (255, 0, 0), 1)
    if target is not None:
        box = cv2.boxPoints(target.rect).astype(np.int32)
        center = (int(round(target.x * w)), int(round(target.y * h)))
        for img in (left, right):
            cv2.polylines(img, [box], True, (0, 255, 0), 2)
            cv2.circle(img, center, 5, (0, 0, 255), -1)
        info = ('x=%.3f aim=%.3f Wpx=%.1f W%%=%.1f R=%.2f fill=%.2f tilt=%.1f'
                % (target.x, p.aim_x, target.width_px, target.width_frac * 100,
                   target.aspect, target.fill, target.tilt))
        head = 'HELD' if target.held else 'FOUND'
    else:
        info = ('x=- aim=%.3f Wpx=- W%%=- R=- fill=- tilt=-' % p.aim_x)
        head = 'NO TARGET'
    ctrl = 'PREVIEW ONLY' if not fresh else ('STATE %d PWM %d EVENT %d' % (state, pwm, event))
    fps_text = 'fps=%.1f' % fps
    for img in (left, right):
        cv2.putText(img, head, (8, 24), cv2.FONT_HERSHEY_SIMPLEX, .7, (0, 255, 255), 2)
        cv2.putText(img, info, (8, 52), cv2.FONT_HERSHEY_SIMPLEX, .5, (0, 255, 255), 1)
        cv2.putText(img, ctrl, (8, 78), cv2.FONT_HERSHEY_SIMPLEX, .5, (0, 255, 255), 1)
        cv2.putText(img, fps_text, (8, 104), cv2.FONT_HERSHEY_SIMPLEX, .5, (0, 255, 255), 1)
    return np.hstack((left, right))


def main():
    """@brief 救援视觉主循环：前视、检测、GUI、发布、录像。"""
    def stop(signum, frame):
        raise KeyboardInterrupt

    parser = argparse.ArgumentParser(description='Rescue front-camera detection GUI')
    parser.add_argument('--no-record', action='store_true', help='disable AVI recording')
    args = parser.parse_args()

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGHUP, stop)
    RUNTIME.mkdir(parents=True, exist_ok=True)
    publish_state(0, 0, -1, 0, 0)
    cap = cv2.VideoCapture(FRONT_CAMERA, cv2.CAP_V4L2)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
    recorder = PillarRecorder(RUNTIME, not args.no_record)
    tracker = PillarTracker()
    frame_times = []
    count = 0
    last_tune_text = None
    last_tune_time = 0.0
    try:
        if not cap.isOpened():
            raise RuntimeError('Camera unavailable')
        cv2.namedWindow(WIN, cv2.WINDOW_AUTOSIZE)
        cv2.moveWindow(WIN, 0, 30)
        cv2.namedWindow(WIN_V, cv2.WINDOW_NORMAL)
        cv2.resizeWindow(WIN_V, 380, 420)
        cv2.moveWindow(WIN_V, 1300, 30)
        cv2.namedWindow(WIN_C, cv2.WINDOW_NORMAL)
        cv2.resizeWindow(WIN_C, 380, 460)
        cv2.moveWindow(WIN_C, 1700, 30)
        create_trackbars()
        while True:
            stamp = time.monotonic()
            ok, frame = cap.read()
            if not ok:
                raise RuntimeError('Camera read failed')
            p, control = read_params()
            now = time.monotonic()
            text = format_tuning(p, control)
            if text != last_tune_text and now - last_tune_time >= .2:
                publish_tuning(text)
                last_tune_text = text
                last_tune_time = now
            mask = build_mask(frame, p)
            raw, _ = detect_pillar(frame, p, mask)
            target = tracker.update(raw, stamp, p)
            if target is None:
                publish_state(stamp, 0, -1, 0, 0)
            else:
                publish_state(stamp, 1, target.x, target.width_frac, target.aspect)
            state, pwm, width, event, fresh = read_status()
            frame_times.append(time.monotonic())
            frame_times = frame_times[-30:]
            fps = 0.0
            if len(frame_times) > 1:
                dt = frame_times[-1] - frame_times[0]
                if dt > 1e-6:
                    fps = (len(frame_times) - 1) / dt
            combined = draw(frame, mask, target, p, state, pwm, event, fresh, fps)
            recorder.save(combined, stamp, target, state, pwm, event)
            cv2.imshow(WIN, combined)
            count += 1
            if count == 1 or count % 100 == 0:
                print('RESCUE_PREVIEW frames=%d raw=%s found=%s fps=%.1f'
                      % (count, raw is not None, target is not None, fps), flush=True)
            key = cv2.waitKey(1) & 255
            if key in (27, ord('q')):
                break
            if key == ord('s'):
                snap = RUNTIME / ('rescue_snapshot_' + time.strftime('%Y%m%d_%H%M%S') + '.jpg')
                cv2.imwrite(str(snap), combined)
                print('SNAPSHOT', snap, flush=True)
    finally:
        publish_state(0, 0, -1, 0, 0)
        recorder.close()
        cap.release()
        cv2.destroyAllWindows()


if __name__ == '__main__':
    main()
