"""下视导引线断口检测与事件去抖。

通过配对方向一致、横向对齐的两段条带定位断口，允许画面边缘被裁掉的短片段。

@file gap_light.py
@brief 断口几何检测与 0.1 s/0.5 s 事件去抖。
"""
import cv2
import numpy as np

def detect_gap(mask):
    """@brief 在二值图中检测一个符合尺寸约束的导引线断口。

    @param mask 640×480 单通道下视二值图，非零像素为导引线。
    @return (起点, 终点) 两个整数像素坐标；没有合法断口时返回 None。
    @note 断口长度必须在 8 px 到画面高度 45% 之间，且断口采样点中至少 80%
    为背景；允许触边短片段与长条带配对。
    """
    h,w=mask.shape
    contours,_=cv2.findContours(mask,cv2.RETR_EXTERNAL,cv2.CHAIN_APPROX_NONE)
    parts=[]
    for c in contours:
        if cv2.contourArea(c)<40:continue
        pts=c[:,0,:].astype(float)
        short,long=sorted(cv2.minAreaRect(c)[1])
        x,y,bw,bh=cv2.boundingRect(c)
        edge=x<=3 or y<=3 or x+bw>=w-3 or y+bh>=h-3
        strong=long>=h*.08 and long/max(short,1)>=3
        if not strong and not(edge and long>=h*.025):continue
        vx,vy,cx,cy=cv2.fitLine(c,cv2.DIST_HUBER,0,.01,.01).ravel()
        parts.append((pts,np.array([vx,vy]),np.array([cx,cy]),max(short,2),strong))
    for i,a in enumerate(parts):
        for b in parts[i+1:]:
            if not(a[4] or b[4]):continue
            if not a[4]:a1,b1=b,a
            else:a1,b1=a,b
            pa,u,ca,wa,_=a1;pb,v,cb,wb,strong=b1
            if strong and abs(u@v)<np.cos(np.deg2rad(15)):continue
            normal=np.array([-u[1],u[0]])
            if abs((cb-ca)@normal)>max(6,(wa+wb)*.5):continue
            # Reject edge blobs whose transverse width is unlike the long strip.
            transverse=np.ptp(pb@normal)
            if not strong and not .4*wa<=transverse<=2*wa:continue
            ap=pa@u;bp=pb@u
            if ap.max()<bp.min():p=pa[ap.argmax()];q=pb[bp.argmin()]
            elif bp.max()<ap.min():p=pb[bp.argmax()];q=pa[ap.argmin()]
            else:continue
            gap=np.linalg.norm(q-p)
            if not 8<=gap<=h*.45:continue
            xy=np.rint(p[None,:]+np.linspace(.2,.8,20)[:,None]*(q-p)).astype(int)
            if np.mean(mask[xy[:,1],xy[:,0]]==0)<.8:continue
            return tuple(p.astype(int)),tuple(q.astype(int))
    return None

class GapEvent:
    """@brief 把逐帧断口检测转换为单调递增的事件计数。"""

    def __init__(self):
        """@brief 初始化断口确认/清除计时状态。

        @note since 记录本次连续检测的起点，clear 记录连续未检测的起点；
        ready=False 表示事件已触发、等待断口消失 0.5 s 后重新武装。
        """
        self.since=None;self.clear=None;self.ready=True;self.event=0;self.last=0

    def step(self,stamp,detected):
        """@brief 推进一步断口事件状态机。

        @param stamp 下视状态时间戳，单位秒；必须单调递增。
        @param detected 当前帧是否检测到断口。
        @return 最新事件计数；计数增加表示产生一次新的亮灯事件。
        @note 连续检测 0.1 s 才触发；连续未检测 0.5 s 才允许下一次触发。
        stamp 不大于上次值时直接返回当前计数。
        """
        if stamp<=self.last:return self.event
        self.last=stamp
        if detected:
            self.clear=None
            if self.since is None:self.since=stamp
            if self.ready and stamp-self.since>=.1:self.event+=1;self.ready=False
        else:
            self.since=None
            if self.clear is None:self.clear=stamp
            if stamp-self.clear>=.5:self.ready=True
        return self.event
