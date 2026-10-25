#!/usr/bin/env python3
"""水中救援启动器。

默认只启动前视预览；加 --run 后启动 CH10 轻撞控制器。启动器负责单实例锁、
运行时目录准备、视觉/控制器进程生命周期和最终日志打印。

@file run_rescue.py
@brief 救援前视预览与 CH10 控制器的统一入口。
"""
import argparse
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
RUNTIME = Path(os.environ.get('RESCUE_RUNTIME',
                              os.environ.get('WIDE_SEARCH_RUNTIME', ROOT / 'runtime'))).resolve()
VISION = ROOT / 'src' / 'vision' / 'rescue_vision.py'
_control = os.environ.get('RESCUE_CONTROL')
if _control:
    CONTROL = Path(_control)
    if not CONTROL.is_absolute():
        CONTROL = (ROOT / CONTROL).resolve()
else:
    CONTROL = ROOT / 'bin' / 'rescue_control'


def child_env():
    """@brief 构造子进程环境，强制同一个运行时目录。

    @return 复制当前环境并写入 RESCUE_RUNTIME。
    """
    env = os.environ.copy()
    env['RESCUE_RUNTIME'] = str(RUNTIME)
    return env


def seed_state():
    """@brief 把救援状态文件重置为无目标。"""
    (RUNTIME / 'rescue_state.txt').write_text('0 0 -1 0 0\n')


def main():
    """@brief 启动器主流程：加锁、拉起视觉、等待状态、启动控制器并回收。

    @return 退出码：0 正常，1 启动/运行错误，130 表示被 Ctrl+C 中断。
    @exception RuntimeError 视觉提前退出，或 10 秒内没有新鲜状态。
    """
    parser = argparse.ArgumentParser(
        description='Preview the rescue vision pipeline, or run it with CH10 control.')
    parser.add_argument('--run', action='store_true',
                        help='start the controller and CH10 output (default is preview only)')
    parser.add_argument('--duration', type=float, default=290.0,
                        help='control duration in seconds, 2..300 (default: 290)')
    parser.add_argument('--no-record', action='store_true',
                        help='disable AVI recording in preview/run')
    args = parser.parse_args()
    if not 2 <= args.duration <= 300:
        parser.error('--duration must be between 2 and 300 seconds')
    if not VISION.is_file():
        parser.error('missing vision entry point: %s' % VISION)
    if args.run and not os.access(CONTROL, os.X_OK):
        parser.error('missing executable %s; build it on the target with "make control"' % CONTROL)

    RUNTIME.mkdir(parents=True, exist_ok=True)
    os.chdir(ROOT)

    def stop(signum, frame):
        """@brief 把 SIGTERM/SIGHUP 转换为 KeyboardInterrupt。"""
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGHUP, stop)
    os.environ.setdefault('DISPLAY', ':0')
    xauthority = ROOT / '.preview_xauthority'
    os.environ.setdefault(
        'XAUTHORITY', str(xauthority) if xauthority.exists() else '/home/auv8/.Xauthority')

    lock = open(RUNTIME / 'rescue_launcher.lock', 'w')
    import fcntl
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        lock.close()
        print('Another rescue launcher is already running (lock: %s)' %
              (RUNTIME / 'rescue_launcher.lock'), file=sys.stderr)
        return 1

    seed_state()
    try:
        (RUNTIME / 'rescue_status.txt').unlink()
    except FileNotFoundError:
        pass

    vision_cmd = [sys.executable, '-u', str(VISION)]
    if args.no_record:
        vision_cmd.append('--no-record')
    vision = subprocess.Popen(
        vision_cmd, cwd=str(ROOT), env=child_env(), start_new_session=True)
    control = None
    try:
        if not args.run:
            return vision.wait()

        deadline = time.monotonic() + 10
        while True:
            if vision.poll() is not None:
                raise RuntimeError('Camera preview failed; see the messages above')
            try:
                ts, *_ = map(float, (RUNTIME / 'rescue_state.txt').read_text().split())
                if 0 <= time.monotonic() - ts < .5:
                    break
            except (ValueError, OSError):
                pass
            if time.monotonic() > deadline:
                raise RuntimeError('No fresh camera data within 10 s')
            time.sleep(.05)

        name = 'rescue_' + time.strftime('%Y%m%d_%H%M%S')
        log_path = RUNTIME / (name + '.log')
        csv_path = RUNTIME / (name + '.csv')
        with open(log_path, 'w') as log:
            control = subprocess.Popen(
                [str(CONTROL), '--duration', ('%g' % args.duration), '--log', str(csv_path)],
                cwd=str(RUNTIME), env=child_env(),
                stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            print('Started %.0f-second rescue run. Log: %s' % (args.duration, log_path),
                  flush=True)
            result = control.wait()
        print(log_path.read_text(), flush=True)
        return result
    except KeyboardInterrupt:
        print('Interrupted; stopping controller and camera preview.', file=sys.stderr, flush=True)
        return 130
    finally:
        if control is not None and control.poll() is None:
            control.send_signal(signal.SIGINT)
            try:
                control.wait(timeout=30)
            except subprocess.TimeoutExpired:
                control.terminate()
                try:
                    control.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    control.kill()
                    control.wait()
        if vision.poll() is None:
            vision.terminate()
            try:
                vision.wait(timeout=3)
            except subprocess.TimeoutExpired:
                vision.kill()
                vision.wait()
        seed_state()
        lock.close()
        for record in ('rescue_recovery.txt', 'route_recovery.txt'):
            if (RUNTIME / record).exists():
                print('WARNING: %s still exists; inspect CH10 before deleting it.' %
                      (RUNTIME / record), file=sys.stderr)


if __name__ == '__main__':
    sys.exit(main())
