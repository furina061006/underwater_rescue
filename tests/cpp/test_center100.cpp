/**
 * @file test_center100.cpp
 * @brief 验证前视中段 0.45/0.55 区域输出 1500。
 */
#include "wide_search_logic.hpp"
#include <cassert>
/**
 * @brief 执行中段 PWM 断言。
 * @return 0 表示全部断言通过。
 */
int main(){for(double x:{.45,.5,.55}){HorizontalLossRoute r;assert(r.step(1,.0625,1,x,false,false,1,false,0)==1600);assert(r.step(1,.1875,1,x,false,false,1,false,0)==1400);assert(r.step(1,.25,1,x,false,false,1,false,0)==1500);}}
