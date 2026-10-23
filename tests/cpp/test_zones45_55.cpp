/**
 * @file test_zones45_55.cpp
 * @brief 验证前视 0.45/0.55 分段边界与左右端 PWM。
 */
#include "wide_search_logic.hpp"
#include <cassert>
/**
 * @brief 用给定 x 运行一步巡线状态机并返回 PWM。
 * @param x 前视底线横向比例。
 * @return 该 x 对应的 CH10 目标 PWM。
 */
int pwm(double x){HorizontalLossRoute r;return r.step(1,0,1,x,false,false,1,false,0);}
/**
 * @brief 执行分区边界断言。
 * @return 0 表示全部断言通过。
 */
int main(){assert(pwm(0)==1300);assert(pwm(.225)==1350);assert(pwm(.4499)==1400);assert(pwm(.45)==1500);assert(pwm(.5)==1500);assert(pwm(.55)==1500);assert(pwm(.5501)==1600);assert(pwm(.775)==1650);assert(pwm(1)==1700);}
