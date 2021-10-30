#include <math.h>
#include <errno.h>

double
fmod (double x, double y)
{
	int sign, yexp, rexp;
	double r, yfr, rfr;

	if (isNaN(y) || x + y == x){	/* isNaN(y) || y==0 || isInf(x,0) */
		if(x + y == x)
			errno = EDOM;
		return NaN();
	}
	if (y < 0)
		y = -y;
	yfr = frexp(y, &yexp);
	sign = 0;
	if(x < 0) {
		r = -x;
		sign++;
	} else
		r = x;
	while(r >= y) {
		rfr = frexp(r, &rexp);
		r -= ldexp(y, rexp - yexp - (rfr < yfr));
	}
	if(sign)
		r = -r;
	return r;
}
