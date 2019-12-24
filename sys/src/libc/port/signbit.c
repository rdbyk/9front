#include <u.h>
#include <libc.h>

int
signbit(double x)
{
	FPdbleword xw;
	xw.x = x;
	return xw.hi >> 31;
}

double
copysign(double x, double y)
{
	FPdbleword xw, yw;
	yw.x = y;
	xw.x = x;	
	xw.hi &= 0x7FFFFFFFL;
	xw.hi |= yw.hi & 0x80000000L;
	return xw.x;
}
