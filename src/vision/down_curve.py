"""Image-plane curve fit. Curvature is pixels^-1, not physical radius."""
import cv2
import numpy as np

def fit_curve(mask):
 h,w=mask.shape
 n,labels,stats,_=cv2.connectedComponentsWithStats(mask)
 ids=[i for i in range(1,n) if stats[i,cv2.CC_STAT_AREA]>=80 and stats[i,cv2.CC_STAT_HEIGHT]>=h*.3]
 if not ids:return None
 i=max(ids,key=lambda j:stats[j,cv2.CC_STAT_AREA])
 ys,xs=np.where(labels==i)
 rows=np.unique(ys);yy=[];xx=[]
 for y in rows[::3]:
  pixels=xs[ys==y]
  if pixels.size:yy.append(float(y));xx.append(float(np.median(pixels)))
 if len(yy)<12:return None
 y=np.array(yy);x=np.array(xx);span=y.max()-y.min()
 # Forward coordinate increases from image bottom towards top.
 z=(y.max()-y)/span
 coef=np.polyfit(z,x,2);res=x-np.polyval(coef,z)
 good=np.abs(res)<=max(4,2.5*np.median(np.abs(res)))
 if good.sum()<12:return None
 coef=np.polyfit(z[good],x[good],2);a,b,c=coef
 residual=float(np.sqrt(np.mean((x[good]-np.polyval(coef,z[good]))**2)))
 if residual>max(8,.025*w):return None
 headings=[]
 for lo,hi in [(0,.34),(.33,.67),(.66,1.01)]:
  sel=good&(z>=lo)&(z<hi)
  if sel.sum()<3:return None
  slope=np.polyfit(z[sel]*span,x[sel],1)[0]
  headings.append(float(np.degrees(np.arctan(slope))))
 delta=headings[2]-headings[0]
 offset=float((np.polyval(coef,1)-np.polyval(coef,0))/w)
 curvature=float((2*a/span**2)/(1+((a+b)/span)**2)**1.5)
 left=delta<=-12 and offset<=-.03 and curvature<0
 strength=min(1.,max(0.,-offset/.5)) if left else 0.
 zz=np.linspace(0,1,60);points=np.column_stack((np.polyval(coef,zz),y.max()-zz*span)).round().astype(np.int32)
 return dict(left=bool(left),strength=strength,offset=offset,curvature=curvature,headings=headings,delta=delta,points=points,residual=residual)
