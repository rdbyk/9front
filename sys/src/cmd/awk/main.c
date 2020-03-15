/****************************************************************
Copyright (C) Lucent Technologies 1997
All Rights Reserved

Permission to use, copy, modify, and distribute this software and
its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appear in all
copies and that both that the copyright notice and this
permission notice and warranty disclaimer appear in supporting
documentation, and that the name Lucent Technologies or any of
its entities not be used in advertising or publicity pertaining
to distribution of the software without specific, written prior
permission.

LUCENT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
IN NO EVENT SHALL LUCENT OR ANY OF ITS ENTITIES BE LIABLE FOR ANY
SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF
THIS SOFTWARE.
****************************************************************/

char	*version = "version 19990602";

#include <u.h>
#include <libc.h>
#include <bio.h>
#include "awk.h"
#include "y.tab.h"

extern	int	nfields;

Biobuf stdin;
Biobuf stdout;
Biobuf stderr;

int	dbg	= 0;
char	*cmdname;	/* gets argv[0] for error messages */
extern	Biobuf	*yyin;	/* lex input file */
char	*lexprog;	/* points to program argument if it exists */
int	compile_time = 2;	/* for error printing: */
				/* 2 = cmdline, 1 = compile, 0 = running */

char	*pfile[20];	/* program filenames from -f's */
int	npfile = 0;	/* number of filenames */
int	curpfile = 0;	/* current filename */

int	safe	= 0;	/* 1 => "safe" mode */

void main(int argc, char *argv[])
{
	char *fs = nil, *marg;
	int temp;

	setfcr(getfcr() & ~FPINVAL);

	Binit(&stdin, 0, OREAD);
	Binit(&stdout, 1, OWRITE);
	Binit(&stderr, 2, OWRITE);

	cmdname = argv[0];
	if (argc == 1) {
		Bprint(&stderr, "usage: %s [-F fieldsep] [-d] [-mf n] [-mr n] [-safe] [-v var=value] [-f programfile | 'program'] [file ...]\n", cmdname);
		exits("usage");
	}

	atnotify(handler, 1);
	yyin = nil;
	symtab = makesymtab(NSYMTAB);
	while (argc > 1 && argv[1][0] == '-' && argv[1][1] != '\0') {
		if (strcmp(argv[1], "--") == 0) {	/* explicit end of args */
			argc--;
			argv++;
			break;
		}
		switch (argv[1][1]) {
		case 's':
			if (strcmp(argv[1], "-safe") == 0)
				safe = 1;
			break;
		case 'f':	/* next argument is program filename */
			argc--;
			argv++;
			if (argc <= 1)
				FATAL("no program filename");
			pfile[npfile++] = argv[1];
			break;
		case 'F':	/* set field separator */
			if (argv[1][2] != 0) {	/* arg is -Fsomething */
				if (argv[1][2] == 't' && argv[1][3] == 0)	/* wart: t=>\t */
					fs = "\t";
				else if (argv[1][2] != 0)
					fs = &argv[1][2];
			} else {		/* arg is -F something */
				argc--; argv++;
				if (argc > 1 && argv[1][0] == 't' && argv[1][1] == 0)	/* wart: t=>\t */
					fs = "\t";
				else if (argc > 1 && argv[1][0] != 0)
					fs = &argv[1][0];
			}
			if (fs == nil || *fs == '\0')
				WARNING("field separator FS is empty");
			break;
		case 'v':	/* -v a=1 to be done NOW.  one -v for each */
			if (argv[1][2] == '\0' && --argc > 1 && isclvar((++argv)[1]))
				setclvar(argv[1]);
			break;
		case 'm':	/* more memory: -mr=record, -mf=fields */
				/* no longer needed */
			marg = argv[1];
			if (argv[1][3])
				temp = atoi(&argv[1][3]);
			else {
				argv++; argc--;
				temp = atoi(&argv[1][0]);
			}
			switch (marg[2]) {
			case 'r':	recsize = temp; break;
			case 'f':	nfields = temp; break;
			default: FATAL("unknown option %s\n", marg);
			}
			break;
		case 'd':
			dbg = atoi(&argv[1][2]);
			if (dbg == 0)
				dbg = 1;
			print("awk %s\n", version);
			break;
		case 'V':	/* added for exptools "standard" */
			print("awk %s\n", version);
			exits(0);
			break;
		default:
			WARNING("unknown option %s ignored", argv[1]);
			break;
		}
		argc--;
		argv++;
	}
	/* argv[1] is now the first argument */
	if (npfile == 0) {	/* no -f; first argument is program */
		if (argc <= 1) {
			if (dbg)
				exits(0);
			FATAL("no program given");
		}
		   dprint( ("program = |%s|\n", argv[1]) );
		lexprog = argv[1];
		argc--;
		argv++;
	}
	recinit(recsize);
	syminit();
	compile_time = 1;
	argv[0] = cmdname;	/* put prog name at front of arglist */
	   dprint( ("argc=%d, argv[0]=%s\n", argc, argv[0]) );
	arginit(argc, argv);
	yyparse();
	if (fs)
		*FS = qstring(fs, '\0');
	   dprint( ("exitstatus=%s\n", exitstatus) );
	if (exitstatus == nil) {
		compile_time = 0;
		run(winner);
	} else
		bracecheck();
	exits(exitstatus);
}

int pgetc(void)		/* get 1 character from awk program */
{
	int c;

	for (;;) {
		if (yyin == nil) {
			if (curpfile >= npfile)
				return Beof;
			if (strcmp(pfile[curpfile], "-") == 0)
				yyin = &stdin;
			else if ((yyin = Bopen(pfile[curpfile], OREAD)) == nil)
				FATAL("can't open file %s", pfile[curpfile]);
			lineno = 1;
		}
		if ((c = Bgetc(yyin)) != Beof)
			return c;
		if (yyin != &stdin)
			Bterm(yyin);
		yyin = nil;
		curpfile++;
	}
}

char *cursource(void)	/* current source file name */
{
	if (npfile > 0)
		return pfile[curpfile];
	else
		return nil;
}
