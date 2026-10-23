#pragma once
#include <cmath>
#include <stdexcept>
#include <algorithm>

/**
 * @file wide_search_logic.hpp
 * @brief 前视三段巡线与下视左弧固定 2 s 转弯的纯逻辑状态机。
 */

/**
 * @brief 根据前视底线位置和下视左弧状态计算 CH10 目标 PWM。
 *
 * stage=1 时执行前视巡线；下视左弧连续 0.1 s 后进入 stage=2，
 * 固定转弯 2 s，再回到 stage=1。转弯结束后必须先看到非左弧样本才会重新武装。
 */
struct HorizontalLossRoute {
 int stage=1;unsigned turns=0;double candidate=-1,last_down=-1,center=900,last_now=-1,turn_until=-1,turn_start=0;bool ready=true;
 /**
  * @brief 推进一步巡线/转弯状态机并返回本周期 PWM。
  * @param now 当前单调时钟，单位秒。
  * @param t 本次任务已运行的秒数，用于巡线正弦相位。
  * @param stamp 前视状态时间戳，单位秒。
  * @param x 前视底线横向比例，范围 [-1,1]；-1 表示没有底线。
  * @param horizontal 保留参数：前视上视线是否拟合成功（当前不参与控制）。
  * @param any 保留参数：是否检测到任意长线（当前不参与控制）。
  * @param down_stamp 下视状态时间戳，单位秒；0 或过期表示无效。
  * @param left 下视是否判定为持续左弧。
  * @param strength 左弧强度，范围 [0,1]。
  * @return CH10 目标 PWM，单位 us；无底线且不在转弯时返回 0。
  * @exception std::runtime_error 前视状态过期/非法，或转弯中下视状态过期。
  */
 int step(double now,double t,double stamp,double x,bool horizontal,bool any,double down_stamp,bool left,double strength){
  (void)horizontal; (void)any; // 保留参数，仅用于未来视觉线索。
  if(!std::isfinite(stamp)||!std::isfinite(x)||stamp>now||now-stamp>.5||x < -1||x>1)throw std::runtime_error("Front vision stale/invalid");
  bool fresh=std::isfinite(down_stamp)&&down_stamp>0&&down_stamp<=now&&now-down_stamp<=.5&&std::isfinite(strength)&&strength>=0&&strength<=1;
  if(!fresh){candidate=-1;if(stage==2)throw std::runtime_error("Down camera stale during turn");}
  if(stage==2&&now>=turn_until){stage=1;candidate=-1;ready=false;}
  if(fresh&&down_stamp>last_down){
   last_down=down_stamp;
   if(stage!=2){
    if(!left){ready=true;candidate=-1;}
    else if(ready){if(candidate<0)candidate=down_stamp;if(down_stamp-candidate>=.1){stage=2;++turns;center=900;turn_start=now;turn_until=now+2;ready=false;}}
   }
  }
  double dt=last_now<0?0:std::max(0.,std::min(.1,now-last_now));last_now=now;
  if(stage==2){
   double wanted=left?900-50*strength:center;
   center+=std::max(-600*dt,std::min(600*dt,wanted-center));
   return int(std::lround(center+100*std::sin(2*std::acos(-1.)*5*(now-turn_start))));
  }
  if(x<0)return 0;
  if(x<.45)return int(std::lround((1300+100*x/.45)+100*std::sin(10*std::acos(-1.)*t)));
  if(x>.55)return int(std::lround((1600+100*(x-.55)/.45)+100*std::sin(10*std::acos(-1.)*t)));
  return int(std::lround(1500+100*std::sin(8*std::acos(-1.)*t)));
 }
};
