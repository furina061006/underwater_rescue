"""下视导引线曲线拟合与左弧判定。

曲率是图像平面内的像素曲率（px^-1），不是物理半径。

@file down_curve.py
@brief 对下视二值图做二次拟合，输出左弧标志、强度、横向偏移和预览点。
"""
import cv2
import numpy as np

def fit_curve(mask):
 """@brief 拟合下视导引线并判断是否满足左弧条件。

 @param mask 640×480 单通道下视二值图，非零像素为导引线。
 @return 成功时返回字典，包含 left、strength、offset、curvature、headings、
 delta、points、residual；无法稳定拟合时返回 None。
 @note 左弧条件为 delta ≤ -12°、offset ≤ -0.03、curvature <0；strength 仅在
 左弧成立时非零。曲率单位为 px^-1，不是物理半径。
 """
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
