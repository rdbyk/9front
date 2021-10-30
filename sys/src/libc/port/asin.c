/*
 * asin(arg) and acos(arg) return the arcsin, arccos,
 * respectively of their arguments.
 *
 * Arctan is called after appropriate range reduction.
 */

#include <u.h>
#include <libc.h>

double
asin(double arg)
{
	double temp;
	int sign;

	sign = 0;
	if(arg < 0) {
		arg = -arg;
		sign++;
	}
	temp = sqrt(1 - arg*arg);
	if(arg > 0.7)
		temp = PIO2 - atan(temp/arg);
	else
		temp = atan(arg/temp);
	if(sign)
		temp = -temp;
	return temp;
}

double
acos(double arg)
{
	return PIO2 - asin(arg);
}
