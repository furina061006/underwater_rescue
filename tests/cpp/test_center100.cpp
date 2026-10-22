#include "wide_search_logic.hpp"
#include <cassert>
int main(){for(double x:{.45,.5,.55}){HorizontalLossRoute r;assert(r.step(1,.0625,1,x,false,false,1,false,0)==1600);assert(r.step(1,.1875,1,x,false,false,1,false,0)==1400);assert(r.step(1,.25,1,x,false,false,1,false,0)==1500);}}
