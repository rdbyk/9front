#include <float.h>
#include <math.h>

double
fabs(double arg)
{
	FPdbleword a;

	a.x = arg;
	a.hi &= 0x7FFFFFFFL;
	return a.x;
}
