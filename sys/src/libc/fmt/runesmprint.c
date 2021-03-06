#include <u.h>
#include <libc.h>

Rune*
runesmprint(char *fmt, ...)
{
	va_list args;
	Rune *p;

	va_start(args, fmt);
	p = runevsmprint(fmt, args);
	va_end(args);
	if(p != nil)
		setmalloctag(p, getcallerpc(&fmt));
	return p;
}
