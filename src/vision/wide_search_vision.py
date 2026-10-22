"""50%-100% height horizontal fit + original binary-bottom following. Q exits."""
import cv2
import numpy as np
import time
import signal
import threading
import os
from pathlib import Path

from route_recording import Recorder
from gap_light import detect_gap, GapEvent
from down_curve import fit_curve

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = Path(os.environ.get('WIDE_SEARCH_RUNTIME', ROOT / 'runtime')).resolve()
FRONT_CAMERA = os.environ.get(
    'WIDE_SEARCH_FRONT_CAMERA',
    '/dev/v4l/by-id/usb-H264_USB_Camera_H264_USB_Camera_2020032801-video-index0')
DOWN_CAMERA = os.environ.get(
    'WIDE_SEARCH_DOWN_CAMERA',
    '/dev/v4l/by-id/usb-HD_Camera_Manufacturer_USB_2.0_Camera-video-index0')

def publish(stamp, x, horizontal, present=False):
    tmp = RUNTIME / 'wide_search_state.tmp'
    tmp.write_text('%.9f %.6f %d %d\n' % (stamp, x, horizontal, present))
    tmp.replace(RUNTIME / 'wide_search_state.txt')

def bottom(mask):
    n, labels, stats, _ = cv2.connectedComponentsWithStats(mask)
    ids = [i for i in range(1, n) if stats[i, cv2.CC_STAT_AREA] >= 40]
    if not ids:
        return None
    i = max(ids, key=lambda j: stats[j, cv2.CC_STAT_AREA])
    ys, xs = np.where(labels == i)
    y = int(ys.max())
    return float(np.median(xs[ys >= y - 5])), y

def upper_line(mask):
    y0, y1 = mask.shape[0] // 2, mask.shape[0]
    upper = mask[y0:y1]
    # A long, thin, moderately angled contour in the lower half is the guide line.
    contours, _ = cv2.findContours(upper.copy(), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
    candidates = []
    for c in contours:
        if cv2.contourArea(c) < 40:
            continue
        vx, vy, x, y = cv2.fitLine(c, cv2.DIST_HUBER, 0, .01, .01).ravel()
        angle = float(np.degrees(np.arctan2(vy, vx)))
        angle = (angle + 90) % 180 - 90
        points = c[:, 0, :]
        span = int(points[:, 0].max() - points[:, 0].min())
        short, long = sorted(cv2.minAreaRect(c)[1])
        if abs(angle) <= 70 and span >= .30 * mask.shape[1] and long / max(short, 1) >= 3:
            c = c.copy()
            c[:, 0, 1] += y0
            candidates.append((span, c, (vx, vy, x, y + y0), angle))
    return max(candidates, key=lambda c: c[0]) if candidates else None

def any_line(mask):
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
    for c in contours:
        if cv2.contourArea(c)<40:continue
        short,long=sorted(cv2.minAreaRect(c)[1])
        if long>=.2*mask.shape[0] and long/max(short,1)>=3:
            return True
    return False

class DownCamera:
    def __init__(self):
        self.frame=None;self.stamp=0;self.stop=False;self.packet=(None,0)
        self.thread=threading.Thread(target=self.read,daemon=True);self.thread.start()
    def read(self):
        c=cv2.VideoCapture(DOWN_CAMERA,cv2.CAP_V4L2)
        try:
            c.set(cv2.CAP_PROP_FOURCC,cv2.VideoWriter_fourcc(*'MJPG'))
            c.set(3,640);c.set(4,480);c.set(cv2.CAP_PROP_BUFFERSIZE,1)
            while not self.stop:
                ok,im=c.read()
                if not ok:break
                self.frame=im;self.stamp=time.monotonic();self.packet=(im,self.stamp)
        finally:c.release()

def tile(im,label):
    canvas=np.zeros((240,320,3),np.uint8)
    h,w=im.shape[:2];scale=min(320/w,240/h)
    rw,rh=round(w*scale),round(h*scale);x=(320-rw)//2;y=(240-rh)//2
    canvas[y:y+rh,x:x+rw]=cv2.resize(im,(rw,rh))
    cv2.rectangle(canvas,(0,0),(320,22),(0,0,0),-1)
    cv2.putText(canvas,label,(5,16),cv2.FONT_HERSHEY_SIMPLEX,.45,(0,255,255),1)
    return canvas

def main():
    def stop(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGHUP, stop)
    RUNTIME.mkdir(parents=True, exist_ok=True)
    publish(0, -1, False)
    cap = cv2.VideoCapture(FRONT_CAMERA, cv2.CAP_V4L2)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
    win = 'Front + Down | four panels | Q exits'
    controls='Camera thresholds'
    down=DownCamera()
    recorder = Recorder(RUNTIME)
    import csv
    curve_file=(recorder.folder/'down_curve.csv').open('w',newline='')
    curve_log=csv.writer(curve_file)
    curve_log.writerow(['front_stamp','down_stamp','left','strength','offset','curvature_per_px','heading_change_deg','near_deg','middle_deg','far_deg'])
    try:
        if not cap.isOpened():
            raise RuntimeError('Camera unavailable')
        cv2.namedWindow(win, cv2.WINDOW_AUTOSIZE)
        cv2.moveWindow(win,0,30)
        cv2.namedWindow(controls,cv2.WINDOW_NORMAL)
        cv2.resizeWindow(controls,320,240)
        cv2.moveWindow(controls,650,30)
        cv2.createTrackbar('S max', controls, 60, 255, lambda _: None)
        cv2.createTrackbar('V min', controls, 220, 255, lambda _: None)
        cv2.createTrackbar('Down S max',controls,60,255,lambda _:None)
        cv2.createTrackbar('Down V min',controls,175,255,lambda _:None)
        gap_event=GapEvent()
        count=0
        while True:
            # Timestamp before acquisition: a blocked read does not look fresh.
            stamp = time.monotonic()
            ok, frame = cap.read()
            if not ok:
                raise RuntimeError('Camera read failed')
            roi = frame[:, int(frame.shape[1] * .2):].copy()
            mask = cv2.inRange(cv2.cvtColor(roi, cv2.COLOR_BGR2HSV),
                               (0, 0, cv2.getTrackbarPos('V min', controls)),
                               (179, cv2.getTrackbarPos('S max', controls), 255))
            point = bottom(mask)
            x = point[0] / roi.shape[1] if point else -1
            upper = upper_line(mask)
            publish(stamp, x, upper is not None, upper is not None or any_line(mask))
            view = cv2.cvtColor(mask, cv2.COLOR_GRAY2BGR)
            for image in (roi, view):
                h, w = image.shape[:2]
                for by in (h//2, h-1):
                    cv2.line(image, (0, by), (w-1, by), (0,255,255), 1)
                for ratio in (.45,.55):
                    px=int(w*ratio);cv2.line(image,(px,0),(px,h-1),(255,0,0),1)
                if point:
                    cv2.circle(image,(int(point[0]),point[1]),6,(0,0,255),2)
                if upper:
                    _, c, (vx,vy,cx,cy), angle=upper
                    p1=(int(cx-vx*w*2),int(cy-vy*w*2))
                    p2=(int(cx+vx*w*2),int(cy+vy*w*2))
                    valid,p1,p2=cv2.clipLine((0,h//2,w,h-h//2),p1,p2)
                    if valid:cv2.line(image,p1,p2,(0,255,0),2)
            status='PREVIEW ONLY'
            stage,pwm,elapsed=0,0,0
            try:
                ts,stage,pwm,elapsed=map(float,(RUNTIME/'wide_search_status.txt').read_text().split())
                if 0<=time.monotonic()-ts<.5:status='Stage %d PWM %d %.1fs'%(stage,pwm,elapsed)
                else:stage,pwm,elapsed=0,0,0
            except (OSError,ValueError):pass
            cv2.putText(roi,status,(8,22),cv2.FONT_HERSHEY_SIMPLEX,.5,(0,255,255),1)
            cv2.putText(roi,'Bottom %.1f%% Horizontal %s'%(x*100,upper is not None),(8,44),cv2.FONT_HERSHEY_SIMPLEX,.45,(0,255,255),1)
            offset=int(frame.shape[1]*.2)
            front=frame.copy();front[:,offset:]=roi
            fullmask=cv2.inRange(cv2.cvtColor(frame,cv2.COLOR_BGR2HSV),(0,0,cv2.getTrackbarPos('V min',controls)),(179,cv2.getTrackbarPos('S max',controls),255))
            frontmask=cv2.cvtColor(fullmask,cv2.COLOR_GRAY2BGR);frontmask[:,offset:]=view
            df,ds=down.packet
            fresh=df is not None and 0<=time.monotonic()-ds<.5
            if df is None or time.monotonic()-down.stamp>1:
                df=np.zeros((480,640,3),np.uint8)
                cv2.putText(df,'Down camera unavailable',(20,220),0,.7,(0,0,255),2)
            dm=cv2.inRange(cv2.cvtColor(df,cv2.COLOR_BGR2HSV),(0,0,cv2.getTrackbarPos('Down V min',controls)),(179,cv2.getTrackbarPos('Down S max',controls),255))
            curve=fit_curve(dm) if fresh else None
            ctmp=RUNTIME/'down_curve_state.tmp'
            ctmp.write_text('%.9f %d %.6f\n'%(ds if fresh else 0,bool(curve and curve['left']),curve['strength'] if curve else 0))
            ctmp.replace(RUNTIME/'down_curve_state.txt')
            gap=detect_gap(dm) if fresh else None
            event=gap_event.step(ds,gap is not None) if fresh else gap_event.event
            tmp=RUNTIME/'gap_light_state.tmp'
            tmp.write_text('%.9f %d\n'%(ds if fresh else 0,event))
            tmp.replace(RUNTIME/'gap_light_state.txt')
            df=df.copy()
            if gap:cv2.line(df,gap[0],gap[1],(0,0,255),3)
            cv2.putText(df,'Gap %s Event %d'%(gap is not None,event),(8,40),0,.6,(0,255,255),2)
            if curve:
                pts=curve['points']
                for j,color in enumerate([(0,255,0),(0,255,255),(255,0,255)]):
                    cv2.polylines(df,[pts[j*20:min(60,(j+1)*20+1)]],False,color,2)
                cv2.putText(df,'Left=%s k=%.5f dx=%.2f'%(curve['left'],curve['curvature'],curve['offset']),(8,80),0,.5,(0,255,255),1)
            curve_log.writerow([stamp,ds,bool(curve and curve['left']),curve['strength'] if curve else 0,curve['offset'] if curve else '',curve['curvature'] if curve else '',curve['delta'] if curve else '',*(curve['headings'] if curve else ['','',''])])
            curve_file.flush()
            combined=np.vstack((np.hstack((tile(front,'Front camera'),tile(frontmask,'Front binary'))),np.hstack((tile(df,'Down camera'),tile(cv2.cvtColor(dm,cv2.COLOR_GRAY2BGR),'Down binary')))))
            count+=1
            if count==1 or count%100==0:
                print('FOUR_PANEL frames=%d down_fresh=%s'%(count,down.frame is not None and time.monotonic()-down.stamp<1),flush=True)
            if count==30:cv2.imwrite(str(RUNTIME/'four_panel_verified.jpg'),combined)
            recorder.save(combined,stamp,int(stage),int(pwm),elapsed,x,upper is not None,upper[3] if upper else '')
            cv2.imshow(win,combined)
            if cv2.waitKey(1)&255 in (27,ord('q')):break
    finally:
        publish(0,-1,False)
        recorder.close()
        curve_file.close()
        down.stop=True
        down.thread.join(timeout=2)
        cap.release()
        cv2.destroyAllWindows()

if __name__=='__main__':main()
