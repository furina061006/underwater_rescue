#include "gap_light_logic.hpp"
#include <cassert>
int main(){GapLamp l;assert(l.step(1,1,1)==1);assert(l.step(1.49,1.49,1)==-1);assert(l.step(1.5,1.5,1)==0);assert(l.step(2,2,1)==-1);assert(l.step(3,3,2)==1);assert(l.step(3.1,0,2)==0);assert(l.step(4,0,3)==-1);}
