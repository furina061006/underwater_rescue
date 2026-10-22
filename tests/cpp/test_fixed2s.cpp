#include "wide_search_logic.hpp"
#include <cassert>
int main(){HorizontalLossRoute r;r.step(1,1,1,.5,false,false,1,true,1);r.step(1.2,1.2,1.2,.5,false,false,1.2,true,1);assert(r.stage==2);double end=r.turn_until;assert(end==3.2);
for(int i=1;i<100;i++){double t=1.2+i*.02;int p=r.step(t,t,t,.5,false,false,t,i<50,1);assert(r.stage==2&&p>=750&&p<=1000);assert(r.turn_until==end);}r.step(3.21,3.21,3.21,.5,false,false,3.21,true,1);assert(r.stage==1);r.step(5,5,5,.5,false,false,5,true,1);assert(r.stage==1);r.step(5.1,5.1,5.1,.5,false,false,5.1,false,0);r.step(5.2,5.2,5.2,.5,false,false,5.2,true,.5);r.step(5.4,5.4,5.4,.5,false,false,5.4,true,.5);assert(r.stage==2&&r.turns==2);
}
