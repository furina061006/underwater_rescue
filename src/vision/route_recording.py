"""Record the actual annotated preview; retain frames around each turn."""
import csv
import time
from collections import deque
from pathlib import Path
import cv2

class Recorder:
    def __init__(self, base):
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
        if self.writer is not None:self.writer.release()
        self.file.close()
        (self.folder/'README.txt').write_text('Annotated camera+binary-mask recording; green line=upper fit, red dot=binary bottom, blue lines=45/55%% width.\nAVI nominal playback is 20 fps; true frame timing is in frames.csv. Turn folders preserve before/first stage-observed/after frames.\nFrames reflect detection and commanded PWM, not measured physical servo angle.\nFrames=%d; turn events=%s\n'%(self.index,sorted(self.seen)))
