#pragma once

/**
 * @file gap_light_logic.hpp
 * @brief 断带亮灯去抖状态机，不依赖硬件或视觉库。
 */

/**
 * @brief 把视觉断带事件转换为“亮灯 0.5 s / 关灯”命令。
 *
 * 输入是视觉侧单调递增的事件计数，输出是每个控制周期应执行的动作。
 * 数据时间戳超过 0.5 s 即视为不新鲜；灯亮时遇到不新鲜会立即要求关灯。
 */
struct GapLamp {
 bool on=false;int seen=0;double until=0;
 /**
  * @brief 推进一步灯状态机。
  * @param now 当前单调时钟，单位秒。
  * @param stamp 视觉断带状态的时间戳，单位秒；0 或过期表示无效。
  * @param event 视觉断带事件计数，单调递增。
  * @return 1 表示亮灯，0 表示关灯，-1 表示保持当前状态。
  * @note 一次亮灯固定持续 0.5 s；如果灯亮期间数据失效，会返回 0。
  */
 int step(double now,double stamp,int event){
  bool fresh=stamp>0&&stamp<=now&&now-stamp<.5;
  if(on&&(!fresh||now>=until)){on=false;seen=event;return 0;}
  if(!fresh)return -1;
  if(event<seen)seen=event;
  if(event>seen){seen=event;if(!on){on=true;until=now+.5;return 1;}}
  return -1;
 }
};
