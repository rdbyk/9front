/*
 * asin(arg) and acos(arg) return the arcsin, arccos,
 * respectively of their arguments.
 *
 * Arctan is called after appropriate range reduction.
 */

#define _RESEARCH_SOURCE /* fixme: dump it into the bit bucket */
#include <math.h>

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
	temp = sqrt(1 - arg*arg);				/* sets errno */
	if(arg > 0.7)
		temp = M_PI_2 - atan(temp/arg);
	else
		temp = atan(arg/temp);
	if(sign)
		temp = -temp;
	return temp;
}

double
acos(double arg)
{
	return M_PI_2 - asin(arg);
}
