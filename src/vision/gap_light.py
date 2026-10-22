"""Pair aligned strips; allow clipped edge fragments against a long strip."""
import cv2
import numpy as np

def detect_gap(mask):
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
    def __init__(self): self.since=None;self.clear=None;self.ready=True;self.event=0;self.last=0
    def step(self,stamp,detected):
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
