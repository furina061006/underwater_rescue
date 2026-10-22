import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'src'/'vision'))
import cv2,numpy as np
from down_curve import fit_curve
from wide_search_vision import upper_line
for name,coef,want in [('left',-160,True),('right',160,False),('straight',0,False)]:
 m=np.zeros((480,640),np.uint8);y=np.arange(30,460);z=(459-y)/429;xs=320+coef*z*z
 pts=np.column_stack((xs,y)).astype(np.int32);cv2.polylines(m,[pts],False,255,12);r=fit_curve(m);assert r is not None and r['left']==want,(name,r);print(name,r['offset'],r['delta'])
m=np.zeros((480,640),np.uint8);cv2.line(m,(100,450),(400,40),255,12);assert not fit_curve(m)['left'];assert fit_curve(np.zeros_like(m)) is None
m=np.zeros((480,640),np.uint8);cv2.line(m,(100,470),(400,250),255,10);assert upper_line(m) is not None
print('CURVE_AND_FRONT_ANGLE_PASS')
