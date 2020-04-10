#include <u.h>
#include <libc.h>

double
pow(double x, double y) /* return x ^ y (exponentiation) */
{
	double xy, y1, ye;
	long i;
	int ex, ey, flip;

	if(y == 0 || x == 1)
		return 1;
	if(isNaN(x) || isNaN(y))
		return NaN();

	flip = 0;
	if(y < 0.){
		y = -y;
		flip = 1;
	}
	y1 = modf(y, &ye);
	if(y1 != 0.0){
		if(x <= 0.)
			goto zreturn;
		if(isInf(x,1)){
			if(flip)
				return +0.0;
			return Inf(1);
		}
		if(y1 > 0.5) {
			y1 -= 1.;
			ye += 1.;
		}
		xy = exp(y1 * log(x));
	}else{
		xy = 1.0;
		if(isInf(y,1)){				/* modf(inf,*) == 0 */
			if(x == -1)
				return xy;
			if(x > -1 && x < 1){	/* |x| < 1 */
				if(flip)
					return Inf(1);
				return +0.0;
			}
			if(x != 1){				/* |x| > 1 */
				if(flip)
					return +0.0;
				return Inf(1);
			}			
		}
	}
	if(ye > 0x7FFFFFFF){	/* should be ~0UL but compiler can't convert double to ulong */
		if(x <= 0.){
 zreturn:
			if(x==0. && !flip)
				return 0.;
			return NaN();
		}
		if(flip){
			if(y == .5)
				return 1./sqrt(x);
			y = -y;
		}else if(y == .5)
			return sqrt(x);
		return exp(y * log(x));
	}
	x = frexp(x, &ex);
	ey = 0;
	i = ye;
	if(i){
		if(isInf(x, 0))
			if(!flip){
				if(i & 1)			/* inf^i, odd i > 0 */  
					return x;
				return -x;
			}
		for(;;){
			if(i & 1){
				xy *= x;
				ey += ex;
			}
			i >>= 1;
			if(i == 0)
				break;
			x *= x;
			ex <<= 1;
			if(x < .5){
				x += x;
				ex -= 1;
			}
		}
	}
	if(flip){
		xy = 1. / xy;
		ey = -ey;
	}
	return ldexp(xy, ey);
}
