#pragma once
struct GapLamp {
 bool on=false;int seen=0;double until=0;
 int step(double now,double stamp,int event){
  bool fresh=stamp>0&&stamp<=now&&now-stamp<.5;
  if(on&&(!fresh||now>=until)){on=false;seen=event;return 0;}
  if(!fresh)return -1;
  if(event<seen)seen=event;
  if(event>seen){seen=event;if(!on){on=true;until=now+.5;return 1;}}
  return -1;
 }
};
