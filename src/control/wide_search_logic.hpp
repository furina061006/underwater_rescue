#pragma once
#include <cmath>
#include <stdexcept>
#include <algorithm>
struct HorizontalLossRoute {
 int stage=1;unsigned turns=0;double candidate=-1,last_down=-1,center=900,last_now=-1,turn_until=-1,turn_start=0;bool ready=true;
 // horizontal/any are reserved for future vision cues; routing uses x plus the down-camera arc.
 int step(double now,double t,double stamp,double x,bool /*horizontal*/,bool /*any*/,double down_stamp,bool left,double strength){
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
