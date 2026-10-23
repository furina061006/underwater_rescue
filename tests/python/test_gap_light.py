"""下视断口检测与事件去抖的算法测试。

@file test_gap_light.py
@brief 构造垂直/水平/边缘断口，验证 detect_gap 与 GapEvent。
"""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'src'/'vision'))
import cv2,numpy as np
from gap_light import detect_gap,GapEvent

def mask(lines):
    """@brief 按线段列表生成 640×480 二值掩码。

    @param lines 可迭代的 ((x1,y1),(x2,y2)) 线段序列。
    @return 单通道 uint8 掩码，线段宽度为 10 px。
    """
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
