/**
 * @file test_gap_lamp.cpp
 * @brief 验证断带灯 0.1 s 确认、0.5 s 亮灯和状态文件失联关灯。
 */
#include "gap_light_logic.hpp"
#include <cassert>
/**
 * @brief 执行断带灯状态机断言。
 * @return 0 表示全部断言通过。
 */
int main(){GapLamp l;assert(l.step(1,1,1)==1);assert(l.step(1.49,1.49,1)==-1);assert(l.step(1.5,1.5,1)==0);assert(l.step(2,2,1)==-1);assert(l.step(3,3,2)==1);assert(l.step(3.1,0,2)==0);assert(l.step(4,0,3)==-1);}
