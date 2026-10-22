import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'src'/'vision'))
import cv2,numpy as np
from gap_light import detect_gap,GapEvent
def mask(lines):
 m=np.zeros((480,640),np.uint8)
 for a,b in lines:cv2.line(m,a,b,255,10)
 return m
assert detect_gap(mask([((300,30),(300,200)),((300,235),(300,450))])) is not None
assert detect_gap(mask([((40,200),(270,200)),((305,200),(580,200))])) is not None
assert detect_gap(mask([((300,30),(300,450))])) is None
assert detect_gap(mask([((300,30),(300,200))])) is None
assert detect_gap(mask([])) is None
assert detect_gap(mask([((100,30),(100,200)),((400,235),(400,450))])) is None
assert detect_gap(mask([((300,30),(300,150)),((200,250),(400,250))])) is None
e=GapEvent();assert e.step(1,True)==0;assert e.step(1.11,True)==1;assert e.step(2,True)==1
assert e.step(2.1,False)==1;assert e.step(2.7,False)==1;assert e.step(3,True)==1;assert e.step(3.11,True)==2
print('GAP_DETECTOR_TEST_PASS')
