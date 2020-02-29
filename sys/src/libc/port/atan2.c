#include <u.h>
#include <libc.h>
/*
	atan2 discovers what quadrant the angle
	is in and calls atan.
*/

double
atan2(double arg1, double arg2)
{

	if(arg1+arg2 == arg1 || arg1-arg2 == arg1) {
		if(arg1 == 0) {
			if (signbit(arg2))
				return copysign(PI, arg1);
			return arg1;
		}
		if(isInf(arg2, 0)) {
			if(arg2 < 0)
				return copysign(3*PI/4, arg1);
			return copysign(PI/4, arg1);
		}
		return copysign(PIO2, arg1);
	}
	arg1 = atan(arg1/arg2);
	if(arg2 < 0)
		return arg1 - copysign(PI, arg1);
	return arg1;
}
