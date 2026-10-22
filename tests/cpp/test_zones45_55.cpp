#include "wide_search_logic.hpp"
#include <cassert>
int pwm(double x){HorizontalLossRoute r;return r.step(1,0,1,x,false,false,1,false,0);}
int main(){assert(pwm(0)==1300);assert(pwm(.225)==1350);assert(pwm(.4499)==1400);assert(pwm(.45)==1500);assert(pwm(.5)==1500);assert(pwm(.55)==1500);assert(pwm(.5501)==1600);assert(pwm(.775)==1650);assert(pwm(1)==1700);}
