#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <cursor.h>
#include <mouse.h>
#include <keyboard.h>
#include <frame.h>
#include <fcall.h>
#include <plumb.h>
#include <complete.h>
#include "dat.h"
#include "fns.h"

Window*
wlookid(int id)
{
	int i;

	for(i=0; i<nwindow; i++)
		if(window[i]->id == id)
			return window[i];
	return nil;
}

Window*
wpointto(Point pt)
{
	int i;
	Window *v, *w;

	w = nil;
	for(i=0; i<nwindow; i++){
		v = window[i];
		if(ptinrect(pt, v->screenr))
		if(w==nil || v->topped>w->topped)
			w = v;
	}
	return w;
}

static	int	topped;

void
wtopme(Window *w)
{
	if(w!=nil && w->i!=nil && w->topped!=topped){
		w->topped = ++topped;
		topwindow(w->i);
		flushimage(display, 1);
	}
}

void
wbottomme(Window *w)
{
	if(w!=nil && w->i!=nil){
		w->topped = - ++topped;
		bottomwindow(w->i);
		flushimage(display, 1);
	}
}

Window*
wtop(Point pt)
{
	Window *w;

	w = wpointto(pt);
	if(w!=nil){
		incref(w);
		wcurrent(w);
		wtopme(w);
		wsendctlmesg(w, Topped, ZR, nil);
		wclose(w);
	}
	return w;
}

void
wcurrent(Window *w)
{
	Channel *c;

	if(input == nil){
		input = w;
		return;
	}
	if(w == input)
		return;
	incref(input);
	c = chancreate(sizeof(Window*), 0);
	wsendctlmesg(input, Repaint, ZR, c);
	sendp(c, w);		/* send the new input */
	wclose(recvp(c));	/* release old input */
	chanfree(c);
}

void
wuncurrent(Window *w)
{
	Channel *c;

	if(input == nil || w != input)
		return;
	c = chancreate(sizeof(Window*), 0);
	wsendctlmesg(w, Repaint, ZR, c);
	sendp(c, nil);
	recvp(c);
	chanfree(c);
}

static	Cursor	*lastcursor;

void
riosetcursor(Cursor *p)
{
	if(p==lastcursor)
		return;
	setcursor(mousectl, p);
	lastcursor = p;
}

void
wsetcursor(Window *w, int force)
{
	Cursor *p;

	if(menuing || sweeping || (w!=input && wpointto(mouse->xy)!=w))
		return;
	if(w==nil)
		p = nil;
	else {
		p = w->cursorp;
		if(p==nil && w->holding)
			p = &whitearrow;
	}
	if(p && force)	/* force cursor reload */
		lastcursor = nil;
	riosetcursor(p);
}

static void
waddraw(Window *w, Rune *r, int nr)
{
	w->raw = runerealloc(w->raw, w->nraw+nr);
	runemove(w->raw+w->nraw, r, nr);
	w->nraw += nr;
}

enum
{
	HiWater	= 640000,	/* max size of history */
	LoWater	= 400000,	/* min size of history after max'ed */
	MinWater	= 20000,	/* room to leave available when reallocating */
};

uint
winsert(Window *w, Rune *r, int n, uint q0)
{
	uint m;

	if(n == 0)
		return q0;
	if(w->nr+n>HiWater && q0>=w->org && q0>=w->qh){
		m = min(HiWater-LoWater, min(w->org, w->qh));
		w->org -= m;
		w->qh -= m;
		if(w->q0 > m)
			w->q0 -= m;
		else
			w->q0 = 0;
		if(w->q1 > m)
			w->q1 -= m;
		else
			w->q1 = 0;
		w->nr -= m;
		runemove(w->r, w->r+m, w->nr);
		q0 -= m;
	}
	if(w->nr+n > w->maxr){
		/*
		 * Minimize realloc breakage:
		 *	Allocate at least MinWater
		 * 	Double allocation size each time
		 *	But don't go much above HiWater
		 */
		m = max(min(2*(w->nr+n), HiWater), w->nr+n)+MinWater;
		if(m > HiWater)
			m = max(HiWater+MinWater, w->nr+n);
		if(m > w->maxr){
			w->r = runerealloc(w->r, m);
			w->maxr = m;
		}
	}
	runemove(w->r+q0+n, w->r+q0, w->nr-q0);
	runemove(w->r+q0, r, n);
	w->nr += n;
	/* if output touches, advance selection, not qh; works best for keyboard and output */
	if(q0 <= w->q1)
		w->q1 += n;
	if(q0 <= w->q0)
		w->q0 += n;
	if(q0 < w->qh)
		w->qh += n;
	if(q0 < w->org)
		w->org += n;
	else if(q0 <= w->org+w->nchars)
		frinsert(w, r, r+n, q0-w->org);
	return q0;
}

static void
wfill(Window *w)
{
	Rune *rp;
	int i, n, m, nl;

	while(w->lastlinefull == FALSE){
		n = w->nr-(w->org+w->nchars);
		if(n == 0)
			break;
		if(n > 2000)	/* educated guess at reasonable amount */
			n = 2000;
		rp = w->r+(w->org+w->nchars);

		/*
		 * it's expensive to frinsert more than we need, so
		 * count newlines.
		 */
		nl = w->maxlines-w->nlines;
		m = 0;
		for(i=0; i<n; ){
			if(rp[i++] == '\n'){
				m++;
				if(m >= nl)
					break;
			}
		}
		frinsert(w, rp, rp+i, w->nchars);
	}
}

void
wsetselect(Window *w, uint q0, uint q1)
{
	int p0, p1;

	/* w->p0 and w->p1 are always right; w->q0 and w->q1 may be off */
	w->q0 = q0;
	w->q1 = q1;
	/* compute desired p0,p1 from q0,q1 */
	p0 = q0-w->org;
	p1 = q1-w->org;
	if(p0 < 0)
		p0 = 0;
	if(p1 < 0)
		p1 = 0;
	if(p0 > w->nchars)
		p0 = w->nchars;
	if(p1 > w->nchars)
		p1 = w->nchars;
	if(p0==w->p0 && p1==w->p1)
		return;
	/* screen disagrees with desired selection */
	if(w->p1<=p0 || p1<=w->p0 || p0==p1 || w->p1==w->p0){
		/* no overlap or too easy to bother trying */
		frdrawsel(w, frptofchar(w, w->p0), w->p0, w->p1, 0);
		frdrawsel(w, frptofchar(w, p0), p0, p1, 1);
		goto Return;
	}
	/* overlap; avoid unnecessary painting */
	if(p0 < w->p0){
		/* extend selection backwards */
		frdrawsel(w, frptofchar(w, p0), p0, w->p0, 1);
	}else if(p0 > w->p0){
		/* trim first part of selection */
		frdrawsel(w, frptofchar(w, w->p0), w->p0, p0, 0);
	}
	if(p1 > w->p1){
		/* extend selection forwards */
		frdrawsel(w, frptofchar(w, w->p1), w->p1, p1, 1);
	}else if(p1 < w->p1){
		/* trim last part of selection */
		frdrawsel(w, frptofchar(w, p1), p1, w->p1, 0);
	}

    Return:
	w->p0 = p0;
	w->p1 = p1;
}

static void
wborder(Window *w, int type)
{
	Image *col;

	if(w->i == nil)
		return;
	if(w->holding){
		if(type == Selborder)
			col = holdcol;
		else
			col = paleholdcol;
	}else{
		if(type == Selborder)
			col = titlecol;
		else
			col = lighttitlecol;
	}
	border(w->i, w->i->r, Selborder, col, ZP);
}

static void
wsetcols(Window *w, int topped)
{
	if(w->holding)
		if(topped)
			w->cols[TEXT] = holdcol;
		else
			w->cols[TEXT] = lightholdcol;
	else
		if(topped)
			w->cols[TEXT] = cols[TEXT];
		else
			w->cols[TEXT] = paletextcol;
}

void
wsetname(Window *w)
{
	int i, n;
	char err[ERRMAX];
	
	n = snprint(w->name, sizeof(w->name)-2, "window.%d.%d", w->id, w->namecount++);
	for(i='A'; i<='Z'; i++){
		if(nameimage(w->i, w->name, 1) > 0)
			return;
		errstr(err, sizeof err);
		if(strcmp(err, "image name in use") != 0)
			break;
		w->name[n] = i;
		w->name[n+1] = 0;
	}
	w->name[0] = 0;
	fprint(2, "rio: setname failed: %s\n", err);
}

static void
wresize(Window *w, Image *i)
{
	Rectangle r;

	w->i = i;
	w->mc.image = i;
	r = insetrect(i->r, Selborder+1);
	w->scrollr = r;
	w->scrollr.max.x = r.min.x+Scrollwid;
	w->lastsr = ZR;
	r.min.x += Scrollwid+Scrollgap;
	frclear(w, FALSE);
	frinit(w, r, w->font, w->i, cols);
	wsetcols(w, w == input);
	w->maxtab = maxtab*stringwidth(w->font, "0");
	if(!w->mouseopen || !w->winnameread){
		r = insetrect(w->i->r, Selborder);
		draw(w->i, r, cols[BACK], nil, w->entire.min);
		wfill(w);
		wsetselect(w, w->q0, w->q1);
		wscrdraw(w);
	}
	if(w == input)
		wborder(w, Selborder);
	else
		wborder(w, Unselborder);
	flushimage(display, 1);
	wsetname(w);
	w->topped = ++topped;
	w->resized = TRUE;
	w->winnameread = FALSE;
	w->mc.buttons = 0;	/* avoid re-triggering clicks on resize */
	w->mouse.counter++;
	w->wctlready = 1;
}

static void
wrepaint(Window *w)
{
	wsetcols(w, w == input);
	if(!w->mouseopen || !w->winnameread)
		frredraw(w);
	if(w == input)
		wborder(w, Selborder);
	else
		wborder(w, Unselborder);
}

static void
wrefresh(Window *w)
{
	Rectangle r;

	if(w == input)
		wborder(w, Selborder);
	else
		wborder(w, Unselborder);
	r = insetrect(w->i->r, Selborder);
	draw(w->i, r, w->cols[BACK], nil, w->entire.min);
	wfill(w);
	w->ticked = 0;
	if(w->p0 > 0)
		frdrawsel(w, frptofchar(w, 0), 0, w->p0, 0);
	if(w->p1 < w->nchars)
		frdrawsel(w, frptofchar(w, w->p1), w->p1, w->nchars, 0);
	frdrawsel(w, frptofchar(w, w->p0), w->p0, w->p1, 1);
	w->lastsr = ZR;
	wscrdraw(w);
}

/*
 * Need to do this in a separate proc because if process we're interrupting
 * is dying and trying to print tombstone, kernel is blocked holding p->debug lock.
 */
static void
interruptproc(void *v)
{
	int *notefd;

	notefd = v;
	write(*notefd, "interrupt", 9);
	close(*notefd);
	free(notefd);
}

typedef struct Completejob Completejob;
struct Completejob
{
	char	*dir;
	char	*str;
	Window	*win;
};

static void
completeproc(void *arg)
{
	Completejob *job;
	Completion *c;

	job = arg;
	threadsetname("namecomplete %s", job->dir);

	c = complete(job->dir, job->str);
	if(c != nil && sendp(job->win->complete, c) <= 0)
		freecompletion(c);

	wclose(job->win);

	free(job->dir);
	free(job->str);
	free(job);
}

static int
windfilewidth(Window *w, uint q0, int oneelement)
{
	uint q;
	Rune r;

	q = q0;
	while(q > 0){
		r = w->r[q-1];
		if(r<=' ' || r=='=' || r=='^' || r=='(' || r=='{')
			break;
		if(oneelement && r=='/')
			break;
		--q;
	}
	return q0-q;
}

static void
namecomplete(Window *w)
{
	int nstr, npath;
	Rune *path, *str;
	char *dir, *root;
	Completejob *job;

	/* control-f: filename completion; works back to white space or / */
	if(w->q0<w->nr && w->r[w->q0]>' ')	/* must be at end of word */
		return;
	nstr = windfilewidth(w, w->q0, TRUE);
	str = w->r+(w->q0-nstr);
	npath = windfilewidth(w, w->q0-nstr, FALSE);
	path = w->r+(w->q0-nstr-npath);

	/* is path rooted? if not, we need to make it relative to window path */
	if(npath>0 && path[0]=='/')
		dir = runetobyte(path, npath, &npath);
	else {
		if(strcmp(w->dir, "") == 0)
			root = ".";
		else
			root = w->dir;
		dir = smprint("%s/%.*S", root, npath, path);
	}
	if(dir == nil)
		return;

	/* run in background, winctl will collect the result on w->complete chan */
	job = emalloc(sizeof *job);
	job->str = runetobyte(str, nstr, &nstr);
	job->dir = cleanname(dir);
	job->win = w;
	incref(w);
	proccreate(completeproc, job, STACK);
}

static void
showcandidates(Window *w, Completion *c)
{
	int i;
	Fmt f;
	Rune *rp;
	uint nr, qline;
	char *s;

	runefmtstrinit(&f);
	if (c->nmatch == 0)
		s = "[no matches in ";
	else
		s = "[";
	if(c->nfile > 32)
		fmtprint(&f, "%s%d files]\n", s, c->nfile);
	else{
		fmtprint(&f, "%s", s);
		for(i=0; i<c->nfile; i++){
			if(i > 0)
				fmtprint(&f, " ");
			fmtprint(&f, "%s", c->filename[i]);
		}
		fmtprint(&f, "]\n");
	}
	rp = runefmtstrflush(&f);
	nr = runestrlen(rp);

	/* place text at beginning of line before cursor and host point */
	qline = min(w->qh, w->q0);
	while(qline>0 && w->r[qline-1] != '\n')
		qline--;

	if(qline == w->qh){
		/* advance host point to avoid readback */
		w->qh = winsert(w, rp, nr, qline)+nr;
	} else {
		winsert(w, rp, nr, qline);
	}
	free(rp);
}

int
wbswidth(Window *w, Rune c)
{
	uint q, eq, stop;
	Rune r;
	int skipping;

	/* there is known to be at least one character to erase */
	if(c == 0x08)	/* ^H: erase character */
		return 1;
	q = w->q0;
	stop = 0;
	if(q > w->qh)
		stop = w->qh;
	skipping = TRUE;
	while(q > stop){
		r = w->r[q-1];
		if(r == '\n'){		/* eat at most one more character */
			if(q == w->q0)	/* eat the newline */
				--q;
			break; 
		}
		if(c == 0x17){
			eq = isalnum(r);
			if(eq && skipping)	/* found one; stop skipping */
				skipping = FALSE;
			else if(!eq && !skipping)
				break;
		}
		--q;
	}
	return w->q0-q;
}

void
wsetorigin(Window *w, uint org, int exact)
{
	int i, a, fixup;
	Rune *r;
	uint n;

	if(org>0 && !exact){
		/* org is an estimate of the char posn; find a newline */
		/* don't try harder than 256 chars */
		for(i=0; i<256 && org<w->nr; i++){
			if(w->r[org] == '\n'){
				org++;
				break;
			}
			org++;
		}
	}
	a = org-w->org;
	fixup = 0;
	if(a>=0 && a<w->nchars){
		frdelete(w, 0, a);
		fixup = 1;	/* frdelete can leave end of last line in wrong selection mode; it doesn't know what follows */
	}else if(a<0 && -a<w->nchars){
		n = w->org - org;
		r = w->r+org;
		frinsert(w, r, r+n, 0);
	}else
		frdelete(w, 0, w->nchars);
	w->org = org;
	wfill(w);
	wscrdraw(w);
	wsetselect(w, w->q0, w->q1);
	if(fixup && w->p1 > w->p0)
		frdrawsel(w, frptofchar(w, w->p1-1), w->p1-1, w->p1, 1);
}

uint
wbacknl(Window *w, uint p, uint n)
{
	int i, j;

	/* look for start of this line if n==0 */
	if(n==0 && p>0 && w->r[p-1]!='\n')
		n = 1;
	i = n;
	while(i-->0 && p>0){
		--p;	/* it's at a newline now; back over it */
		if(p == 0)
			break;
		/* at 128 chars, call it a line anyway */
		for(j=128; --j>0 && p>0; p--)
			if(w->r[p-1]=='\n')
				break;
	}
	return p;
}

char*
whist(Window *w, int *ip)
{
	History *h = &w->history;
	int i, len;
	char *res, *p;

	len = 0;
	for(i = 0; i < h->size; ++i)
		len += runestrlen(histentry(w, i));

	res = p = emalloc(len * UTFmax + 2*h->size);

	for(i = 0; i < h->size; ++i)
		p += sprint(p, "%S\n", histentry(w, i));

	*ip = p - res;
	return res;
}

char*
wcontents(Window *w, int *ip)
{
	return runetobyte(w->r, w->nr, ip);
}

void
wshow(Window *w, uint q0)
{
	int qe;
	int nl;
	uint q;

	qe = w->org+w->nchars;
	if(w->org<=q0 && (q0<qe || (q0==qe && qe==w->nr)))
		wscrdraw(w);
	else{
		nl = 4*w->maxlines/5;
		q = wbacknl(w, q0, nl);
		/* avoid going backwards if trying to go forwards - long lines! */
		if(!(q0>w->org && q<w->org))
			wsetorigin(w, q, TRUE);
		while(q0 > w->org+w->nchars)
			wsetorigin(w, w->org+1, FALSE);
	}
}

void
wsnarf(Window *w)
{
	if(w->q1 == w->q0)
		return;
	nsnarf = w->q1-w->q0;
	snarf = runerealloc(snarf, nsnarf);
	snarfversion++;	/* maybe modified by parent */
	runemove(snarf, w->r+w->q0, nsnarf);
	putsnarf();
}

void
wsend(Window *w)
{
	getsnarf();
	wsnarf(w);
	if(nsnarf == 0)
		return;
	if(w->rawing){
		waddraw(w, snarf, nsnarf);
		if(snarf[nsnarf-1]!='\n' && snarf[nsnarf-1]!='\004')
			waddraw(w, L"\n", 1);
	}else{
		winsert(w, snarf, nsnarf, w->nr);
		if(snarf[nsnarf-1]!='\n' && snarf[nsnarf-1]!='\004')
			winsert(w, L"\n", 1, w->nr);
	}
	wsetselect(w, w->nr, w->nr);
	wshow(w, w->nr);
}

void
wdelete(Window *w, uint q0, uint q1)
{
	uint n, p0, p1;

	n = q1-q0;
	if(n == 0)
		return;
	runemove(w->r+q0, w->r+q1, w->nr-q1);
	w->nr -= n;
	if(q0 < w->q0)
		w->q0 -= min(n, w->q0-q0);
	if(q0 < w->q1)
		w->q1 -= min(n, w->q1-q0);
	if(q1 < w->qh)
		w->qh -= n;
	else if(q0 < w->qh)
		w->qh = q0;
	if(q1 <= w->org)
		w->org -= n;
	else if(q0 < w->org+w->nchars){
		p1 = q1 - w->org;
		if(p1 > w->nchars)
			p1 = w->nchars;
		if(q0 < w->org){
			w->org = q0;
			p0 = 0;
		}else
			p0 = q0 - w->org;
		frdelete(w, p0, p1);
		wfill(w);
	}
}

void
wcut(Window *w)
{
	if(w->q1 == w->q0)
		return;
	wdelete(w, w->q0, w->q1);
	wsetselect(w, w->q0, w->q0);
}

void
wpaste(Window *w)
{
	uint q0;

	if(nsnarf == 0)
		return;
	wcut(w);
	q0 = w->q0;
	if(w->rawing && q0==w->nr){
		waddraw(w, snarf, nsnarf);
		wsetselect(w, q0, q0);
	}else{
		q0 = winsert(w, snarf, nsnarf, w->q0);
		wsetselect(w, q0, q0+nsnarf);
	}
}

void
wlook(Window *w)
{
	int i, n, e;

	i = w->q1;
	n = i - w->q0;
	e = w->nr - n;
	if(n <= 0 || e < n)
		return;

	if(i > e)
		i = 0;

	while(runestrncmp(w->r+w->q0, w->r+i, n) != 0){
		if(i < e)
			i++;
		else
			i = 0;
	}

	wsetselect(w, i, i+n);
	wshow(w, i);
}

void
wplumb(Window *w)
{
	Plumbmsg *m;
	static int fd = -2;
	char buf[32];
	uint p0, p1;
	Cursor *c;

	if(fd == -2)
		fd = plumbopen("send", OWRITE|OCEXEC);
	if(fd < 0)
		return;
	m = emalloc(sizeof(Plumbmsg));
	m->src = estrdup("rio");
	m->dst = nil;
	m->wdir = estrdup(w->dir);
	m->type = estrdup("text");
	p0 = w->q0;
	p1 = w->q1;
	if(w->q1 > w->q0)
		m->attr = nil;
	else{
		while(p0>0 && w->r[p0-1]!=' ' && w->r[p0-1]!='\t' && w->r[p0-1]!='\n')
			p0--;
		while(p1<w->nr && w->r[p1]!=' ' && w->r[p1]!='\t' && w->r[p1]!='\n')
			p1++;
		snprint(buf, sizeof(buf), "click=%d", w->q0-p0);
		m->attr = plumbunpackattr(buf);
	}
	if(p1-p0 > messagesize-1024){
		plumbfree(m);
		return;	/* too large for 9P */
	}
	m->data = runetobyte(w->r+p0, p1-p0, &m->ndata);
	if(plumbsend(fd, m) < 0){
		c = lastcursor;
		riosetcursor(&query);
		sleep(300);
		riosetcursor(c);
	}
	plumbfree(m);
}

static void
wkeyctl(Window *w, Rune r)
{
	uint q0 ,q1;
	int n, nb;
	int *notefd;

	switch(r){
	case 0:
	case Kcaps:
	case Knum:
	case Kshift:
	case Kalt:
	case Kctl:
	case Kaltgr:
		return;
	}

	if(w->i==nil)
		return;
	/* navigation keys work only when mouse and kbd is not open */
	if(!w->mouseopen)
		switch(r){
		case Ksi: /* control-N */
			n = 1;
			goto case_Down;
		case Kdown:
			if(!shiftdown){
				n = w->maxlines/3;
				goto case_Down;
			} else break;
		case Kscrollonedown:
			n = mousescrollsize(w->maxlines);
			if(n <= 0)
				n = 1;
			goto case_Down;
		case Kpgdown:
			n = 2*w->maxlines/3;
		case_Down:
			q0 = w->org+frcharofpt(w, Pt(w->Frame.r.min.x, w->Frame.r.min.y+n*w->font->height));
			wsetorigin(w, q0, TRUE);
			return;
		case Kso: /* control-O */
			n = 1;
			goto case_Up;
		case Kup:
			if(!shiftdown){
				n = w->maxlines/3;
				goto case_Up;
			} else break;
		case Kscrolloneup:
			n = mousescrollsize(w->maxlines);
			if(n <= 0)
				n = 1;
			goto case_Up;
		case Kpgup:
			n = 2*w->maxlines/3;
		case_Up:
			q0 = wbacknl(w, w->org, n);
			wsetorigin(w, q0, TRUE);
			return;
		case Kleft:
			if(w->q0 > 0){
				q0 = w->q0-1;
				wsetselect(w, q0, q0);
				wshow(w, q0);
			}
			return;
		case Kright:
			if(w->q1 < w->nr){
				q1 = w->q1+1;
				wsetselect(w, q1, q1);
				wshow(w, q1);
			}
			return;
		case Khome:
			wshow(w, 0);
			return;
		case Kend:
			wshow(w, w->nr);
			return;
		case Kscroll:
			w->scrolling ^= 1;
			wshow(w, w->nr);
			return;
		case Ksoh:	/* ^A: beginning of line */
			if(w->q0==0 || w->q0==w->qh || w->r[w->q0-1]=='\n')
				return;
			nb = wbswidth(w, 0x15 /* ^U */);
			wsetselect(w, w->q0-nb, w->q0-nb);
			wshow(w, w->q0);
			return;
		case Kenq:	/* ^E: end of line */
			q0 = w->q0;
			while(q0 < w->nr && w->r[q0]!='\n')
				q0++;
			wsetselect(w, q0, q0);
			wshow(w, w->q0);
			return;
		case Kstx:	/* ^B: output point */
			wsetselect(w, w->qh, w->qh);
			wshow(w, w->q0);
			return;
		}
	if(w->rawing && (w->q0==w->nr || w->mouseopen)){
		waddraw(w, &r, 1);
		return;
	}
	if(r==Kesc || (w->holding && r==Kdel)){	/* toggle hold */
		if(w->holding)
			--w->holding;
		else
			w->holding++;
		wsetcursor(w, FALSE);
		wrepaint(w);
		if(r == Kesc)
			return;
	}
	if(r != Kdel){
		wsnarf(w);
		wcut(w);
	}
	switch(r){
	case Kup:
		if(!w->holding)
			histinsert(w, w->history.pos - 1);
		return;
	case Kdown:
		if(!w->holding)
			histinsert(w, w->history.pos + 1);
		return;
	case Kdel:	/* send interrupt */
		w->qh = w->nr;
		wsetselect(w, w->nr, w->nr);
		wshow(w, w->qh);
		histreset(w);
		if(w->notefd < 0)
			return;
		notefd = emalloc(sizeof(int));
		*notefd = dup(w->notefd, -1);
		proccreate(interruptproc, notefd, 4096);
		return;
	case Kack:	/* ^F: file name completion */
	case Kins:	/* Insert: file name completion */
		namecomplete(w);
		return;
	case Kbs:	/* ^H: erase character */
	case Knack:	/* ^U: erase line */
	case Ketb:	/* ^W: erase word */
		if(w->q0==0 || w->q0==w->qh)
			return;
		nb = wbswidth(w, r);
		q1 = w->q0;
		q0 = q1-nb;
		if(q0 < w->org){
			q0 = w->org;
			nb = q1-q0;
		}
		if(nb > 0){
			wdelete(w, q0, q0+nb);
			wsetselect(w, q0, q0);
		}
		return;
	}
	/* otherwise ordinary character; just insert */
	q0 = w->q0;
	q0 = winsert(w, &r, 1, q0);
	wshow(w, q0+1);
}

static Window	*clickwin;
static uint	clickmsec;
static Point	clickpt;
static uint	clickcount;
static Window	*selectwin;
static uint	selectq;

static void
wframescroll(Window *w, int dl)
{
	uint q0;

	if(dl == 0){
		wscrsleep(w, 100);
		return;
	}
	if(dl < 0){
		q0 = wbacknl(w, w->org, -dl);
		if(selectq > w->org+w->p0)
			wsetselect(w, w->org+w->p0, selectq);
		else
			wsetselect(w, selectq, w->org+w->p0);
	}else{
		if(w->org+w->nchars == w->nr)
			return;
		q0 = w->org+frcharofpt(w, Pt(w->Frame.r.min.x, w->Frame.r.min.y+dl*w->font->height));
		if(selectq >= w->org+w->p1)
			wsetselect(w, w->org+w->p1, selectq);
		else
			wsetselect(w, selectq, w->org+w->p1);
	}
	wsetorigin(w, q0, TRUE);
}

/*
 * called from frame library
 */
static void
framescroll(Frame *f, int dl)
{
	if(f != &selectwin->Frame)
		error("frameselect not right frame");
	wframescroll(selectwin, dl);
}

static Rune left1[] =  { L'{', L'[', L'(', L'<', L'«', 0 };
static Rune right1[] = { L'}', L']', L')', L'>', L'»', 0 };
static Rune left2[] =  { L'\n', 0 };
static Rune left3[] =  { L'\'', L'"', L'`', 0 };

static Rune *left[] = {
	left1,
	left2,
	left3,
	nil
};
static Rune *right[] = {
	right1,
	left2,
	left3,
	nil
};

static int
wclickmatch(Window *w, int cl, int cr, int dir, uint *q)
{
	Rune c;
	int nest;

	nest = 1;
	for(;;){
		if(dir > 0){
			if(*q == w->nr)
				break;
			c = w->r[*q];
			(*q)++;
		}else{
			if(*q == 0)
				break;
			(*q)--;
			c = w->r[*q];
		}
		if(c == cr){
			if(--nest==0)
				return 1;
		}else if(c == cl)
			nest++;
	}
	return cl=='\n' && nest==1;
}

static int
inmode(Rune r, int mode)
{
	return (mode == 1) ? isalnum(r) : r && !isspace(r);
}

static void
wstretchsel(Window *w, uint pt, uint *q0, uint *q1, int mode)
{
	int c, i;
	Rune *r, *l, *p;
	uint q;

	*q0 = pt;
	*q1 = pt;
	for(i=0; left[i]!=nil; i++){
		q = *q0;
		l = left[i];
		r = right[i];
		/* try matching character to left, looking right */
		if(q == 0)
			c = '\n';
		else
			c = w->r[q-1];
		p = strrune(l, c);
		if(p != nil){
			if(wclickmatch(w, c, r[p-l], 1, &q))
				*q1 = q-(c!='\n');
			return;
		}
		/* try matching character to right, looking left */
		if(q == w->nr)
			c = '\n';
		else
			c = w->r[q];
		p = strrune(r, c);
		if(p != nil){
			if(wclickmatch(w, c, l[p-r], -1, &q)){
				*q1 = *q0+(*q0<w->nr && c=='\n');
				*q0 = q;
				if(c!='\n' || q!=0 || w->r[0]=='\n')
					(*q0)++;
			}
			return;
		}
	}
	/* try filling out word to right */
	while(*q1<w->nr && inmode(w->r[*q1], mode))
		(*q1)++;
	/* try filling out word to left */
	while(*q0>0 && inmode(w->r[*q0-1], mode))
		(*q0)--;
}

static void
wselect(Window *w)
{
	uint q0, q1;
	int b, x, y, dx, dy, mode, first;

	first = 1;
	selectwin = w;
	/*
	 * Double-click immediately if it might make sense.
	 */
	b = w->mc.buttons;
	q0 = w->q0;
	q1 = w->q1;
	dx = abs(clickpt.x - w->mc.xy.x);
	dy = abs(clickpt.y - w->mc.xy.y);
	clickpt = w->mc.xy;
	selectq = w->org+frcharofpt(w, w->mc.xy);
	clickcount++;
	if(w->mc.msec-clickmsec >= 500 || clickwin != w || clickcount > 3 || dx > 3 || dy > 3)
		clickcount = 0;
	if(clickwin == w && clickcount >= 1 && w->mc.msec-clickmsec < 500){
		mode = (clickcount > 2) ? 2 : clickcount;
		wstretchsel(w, selectq, &q0, &q1, mode);
		wsetselect(w, q0, q1);
		x = w->mc.xy.x;
		y = w->mc.xy.y;
		/* stay here until something interesting happens */
		while(1){
			readmouse(&w->mc);
			dx = abs(w->mc.xy.x-x);
			dy = abs(w->mc.xy.y-y);
			if(w->mc.buttons != b || dx >= 3 && dy >= 3)
				break;
			clickcount++;
			clickmsec = w->mc.msec;
		}
		w->mc.xy.x = x;	/* in case we're calling frselect */
		w->mc.xy.y = y;
		q0 = w->q0;	/* may have changed */
		q1 = w->q1;
		selectq = w->org+frcharofpt(w, w->mc.xy);
	}
	if(w->mc.buttons == b && clickcount == 0){
		w->scroll = framescroll;
		frselect(w, &w->mc);
		/* horrible botch: while asleep, may have lost selection altogether */
		if(selectq > w->nr)
			selectq = w->org + w->p0;
		w->Frame.scroll = nil;
		if(selectq < w->org)
			q0 = selectq;
		else
			q0 = w->org + w->p0;
		if(selectq > w->org+w->nchars)
			q1 = selectq;
		else
			q1 = w->org+w->p1;
	}
	if(q0 == q1){
		mode = (clickcount > 2) ? 2 : clickcount;
		if(q0==w->q0 && clickwin==w && w->mc.msec-clickmsec<500)
			wstretchsel(w, selectq, &q0, &q1, mode);
		else
			clickwin = w;
		clickmsec = w->mc.msec;
	}
	wsetselect(w, q0, q1);
	while(w->mc.buttons){
		w->mc.msec = 0;
		b = w->mc.buttons;
		if(b & 6){
			if(b & 2){
				wsnarf(w);
				wcut(w);
			}else{
				if(first){
					first = 0;
					getsnarf();
				}
				wpaste(w);
			}
		}
		wscrdraw(w);
		while(w->mc.buttons == b)
			readmouse(&w->mc);
		if(w->mc.msec-clickmsec >= 500)
			clickwin = nil;
	}
}

/*
 * Convert back to physical coordinates
 */
static void
wmovemouse(Window *w, Point p)
{
	if(w != input || menuing || sweeping)
		return;
	p.x += w->screenr.min.x-w->i->r.min.x;
	p.y += w->screenr.min.y-w->i->r.min.y;
	moveto(mousectl, p);
}


Window*
wmk(Image *i, Mousectl *mc, Channel *ck, Channel *cctl, int scrolling)
{
	static int id;

	Window *w;
	Rectangle r;

	w = emalloc(sizeof(Window));
	w->screenr = i->r;
	r = insetrect(i->r, Selborder+1);
	w->i = i;
	w->mc = *mc;
	w->ck = ck;
	w->cctl = cctl;
	w->cursorp = nil;
	w->conswrite = chancreate(sizeof(Conswritemesg), 0);
	w->consread =  chancreate(sizeof(Consreadmesg), 0);
	w->kbdread =  chancreate(sizeof(Consreadmesg), 0);
	w->mouseread =  chancreate(sizeof(Mousereadmesg), 0);
	w->wctlread =  chancreate(sizeof(Consreadmesg), 0);
	w->complete = chancreate(sizeof(Completion*), 0);
	w->gone = chancreate(sizeof(char*), 0);
	w->scrollr = r;
	w->scrollr.max.x = r.min.x+Scrollwid;
	w->lastsr = ZR;
	r.min.x += Scrollwid+Scrollgap;
	frinit(w, r, font, i, cols);
	w->maxtab = maxtab*stringwidth(font, "0");
	w->topped = ++topped;
	w->id = ++id;
	w->notefd = -1;
	w->scrolling = scrolling;
	w->dir = estrdup(startdir);
	w->label = estrdup("<unnamed>");
	r = insetrect(w->i->r, Selborder);
	draw(w->i, r, cols[BACK], nil, w->entire.min);
	wborder(w, Selborder);
	wscrdraw(w);
	histinit(w);
	incref(w);	/* ref will be removed after mounting; avoids delete before ready to be deleted */
	return w;
}

static void
wclosewin(Window *w)
{
	Image *i = w->i;
	if(i == nil)
		return;
	w->i = nil;
	/* move it off-screen to hide it, in case client is slow in letting it go */
	originwindow(i, i->r.min, view->r.max);
	freeimage(i);
}

static void
wclunk(Window *w)
{
	int i;

	if(w->deleted)
		return;
	w->deleted = TRUE;
	if(w == input){
		input = nil;
		riosetcursor(nil);
	}
	if(w == wkeyboard)
		wkeyboard = nil;
	for(i=0; i<nhidden; i++)
		if(hidden[i] == w){
			--nhidden;
			memmove(hidden+i, hidden+i+1, (nhidden-i)*sizeof(hidden[0]));
			break;
		}
	for(i=0; i<nwindow; i++)
		if(window[i] == w){
			--nwindow;
			memmove(window+i, window+i+1, (nwindow-i)*sizeof(window[0]));
			break;
		}
}

int
wclose(Window *w)
{
	int i;

	i = decref(w);
	if(i > 0)
		return 0;
	if(i < 0)
		error("negative ref count");
	wclunk(w);
	wsendctlmesg(w, Exited, ZR, nil);
	return 1;
}

void
wsendctlmesg(Window *w, int type, Rectangle r, void *p)
{
	Wctlmesg wcm;

	wcm.type = type;
	wcm.r = r;
	wcm.p = p;
	send(w->cctl, &wcm);
}

static int
wctlmesg(Window *w, int m, Rectangle r, void *p)
{
	Image *i = p;

	switch(m){
	default:
		error("unknown control message");
		break;
	case Wakeup:
		break;
	case Reshaped:
		if(w->deleted){
			freeimage(i);
			break;
		}
		w->screenr = r;
		wclosewin(w);
		wresize(w, i);
		wsetcursor(w, FALSE);
		break;
	case Topped:
		if(w->deleted)
			break;
		w->wctlready = 1;
		wsetcursor(w, FALSE);
		/* fall thrugh for redraw after input change */
	case Repaint:
		if(p != nil){
			/* sync with input change from wcurrent()/wuncurrent() */
			Channel *c = p;
			input = recvp(c);

			/* when we lost input, release mouse buttons */
			if(w->mc.buttons){
				w->mc.buttons = 0;
				w->mouse.counter++;
			}
			w->wctlready = 1;

			sendp(c, w);
		}
		if(w->i==nil || Dx(w->screenr)<=0)
			break;
		wrepaint(w);
		flushimage(display, 1);
		break;
	case Refresh:
		if(w->i==nil || Dx(w->screenr)<=0)
			break;
		wrefresh(w);
		flushimage(display, 1);
		break;
	case Movemouse:
		if(w->i==nil || Dx(w->screenr)<=0 || !ptinrect(r.min, w->i->r))
			break;
		wmovemouse(w, r.min);
	case Rawon:
		break;
	case Rawoff:
		while(w->nraw > 0){
			wkeyctl(w, w->raw[0]);
			--w->nraw;
			runemove(w->raw, w->raw+1, w->nraw);
		}
		break;
	case Holdon:
	case Holdoff:
		if(w->i==nil)
			break;
		wsetcursor(w, FALSE);
		wrepaint(w);
		flushimage(display, 1);
		break;
	case Truncate:
		wdelete(w, 0, w->nr);
		break;
	case Deleted:
		wclunk(w);
		if(w->notefd >= 0)
			write(w->notefd, "hangup", 6);
		wclosewin(w);
		flushimage(display, 1);
		break;
	case Exited:
		wclosewin(w);
		frclear(w, TRUE);
		flushimage(display, 1);
		if(w->notefd >= 0)
			close(w->notefd);
		chanfree(w->mc.c);
		chanfree(w->ck);
		chanfree(w->cctl);
		chanfree(w->conswrite);
		chanfree(w->consread);
		chanfree(w->mouseread);
		chanfree(w->wctlread);
		chanfree(w->kbdread);
		chanfree(w->complete);
		chanfree(w->gone);
		free(w->raw);
		free(w->r);
		free(w->dir);
		free(w->label);
		histfree(w);
		free(w);
		break;
	}
	return m;
}

static void
wmousectl(Window *w)
{
	int but;

	for(but=1;; but++){
		if(but > 5)
			return;
		if(w->mc.buttons == 1<<(but-1))
			break;
	}

	incref(w);		/* hold up window while we track */
	if(w->i != nil){
		if(shiftdown && but > 3)
			wkeyctl(w, but == 4 ? Kscrolloneup : Kscrollonedown);
		else if(ptinrect(w->mc.xy, w->scrollr) || (but > 3))
			wscroll(w, but);
		else if(but == 1)
			wselect(w);
	}
	wclose(w);
}

void
winctl(void *arg)
{
	Rune *rp, *up, r;
	uint qh, q0;
	int nr, nb, c, wid, i, npart, initial, lastb, qh0;
	char *s, *t, part[3];
	Window *w;
	Mousestate *mp, m;
	enum { WKbd, WKbdread, WMouse, WMouseread, WCtl, WCwrite, WCread, WWread, WComplete, Wgone, NWALT };
	Alt alts[NWALT+1];
	Consreadmesg crm;
	Mousereadmesg mrm;
	Conswritemesg cwm;
	Stringpair pair;
	Wctlmesg wcm;
	Completion *cr;
	char *kbdq[32], *kbds;
	uint kbdqr, kbdqw;

	w = arg;
	threadsetname("winctl-id%d", w->id);

	mrm.cm = chancreate(sizeof(Mouse), 0);
	crm.c1 = chancreate(sizeof(Stringpair), 0);
	crm.c2 = chancreate(sizeof(Stringpair), 0);
	cwm.cw = chancreate(sizeof(Stringpair), 0);
	
	alts[WKbd].c = w->ck;
	alts[WKbd].v = &kbds;
	alts[WKbd].op = CHANRCV;
	alts[WKbdread].c = w->kbdread;
	alts[WKbdread].v = &crm;
	alts[WKbdread].op = CHANSND;
	alts[WMouse].c = w->mc.c;
	alts[WMouse].v = &w->mc.Mouse;
	alts[WMouse].op = CHANRCV;
	alts[WMouseread].c = w->mouseread;
	alts[WMouseread].v = &mrm;
	alts[WMouseread].op = CHANSND;
	alts[WCtl].c = w->cctl;
	alts[WCtl].v = &wcm;
	alts[WCtl].op = CHANRCV;
	alts[WCwrite].c = w->conswrite;
	alts[WCwrite].v = &cwm;
	alts[WCwrite].op = CHANSND;
	alts[WCread].c = w->consread;
	alts[WCread].v = &crm;
	alts[WCread].op = CHANSND;
	alts[WWread].c = w->wctlread;
	alts[WWread].v = &crm;
	alts[WWread].op = CHANSND;
	alts[WComplete].c = w->complete;
	alts[WComplete].v = &cr;
	alts[WComplete].op = CHANRCV;
	alts[Wgone].c = w->gone;
	alts[Wgone].v = "window deleted";
	alts[Wgone].op = CHANNOP;
	alts[NWALT].op = CHANEND;

	kbdqr = kbdqw = 0;
	npart = 0;
	lastb = -1;
	for(;;){
		if(w->i==nil){
			/* window deleted */
			alts[Wgone].op = CHANSND;

			alts[WKbdread].op = CHANNOP;
			alts[WMouseread].op = CHANNOP;
			alts[WCwrite].op = CHANNOP;
			alts[WWread].op = CHANNOP;
			alts[WCread].op = CHANNOP;
		} else {
			alts[WKbdread].op = (w->kbdopen && kbdqw != kbdqr) ?
				CHANSND : CHANNOP;
			alts[WMouseread].op = (w->mouseopen && w->mouse.counter != w->mouse.lastcounter) ? 
				CHANSND : CHANNOP;
			alts[WCwrite].op = w->scrolling || w->mouseopen || (w->qh <= w->org+w->nchars) ?
				CHANSND : CHANNOP;
			alts[WWread].op = w->wctlready ?
				CHANSND : CHANNOP;
			/* this code depends on NL and EOT fitting in a single byte */
			/* kind of expensive for each loop; worth precomputing? */
			if(w->holding)
				alts[WCread].op = CHANNOP;
			else if(npart || (w->rawing && w->nraw>0))
				alts[WCread].op = CHANSND;
			else{
				alts[WCread].op = CHANNOP;
				for(i=w->qh; i<w->nr; i++){
					c = w->r[i];
					if(c=='\n' || c=='\004'){
						alts[WCread].op = CHANSND;
						break;
					}
				}
			}
		}
		switch(alt(alts)){
		case WKbd:
			if(kbdqw - kbdqr < nelem(kbdq))
				kbdq[kbdqw++ % nelem(kbdq)] = kbds;
			else
				free(kbds);
			if(w->kbdopen)
				continue;
			while(kbdqr != kbdqw){
				kbds = kbdq[kbdqr++ % nelem(kbdq)];
				if(*kbds == 'c'){
					chartorune(&r, kbds+1);
					if(r)
						wkeyctl(w, r);
				}
				free(kbds);
			}
			break;
		case WKbdread:
			recv(crm.c1, &pair);
			nb = 0;
			while(kbdqr != kbdqw){
				kbds = kbdq[kbdqr % nelem(kbdq)];
				i = strlen(kbds)+1;
				if(nb+i > pair.ns)
					break;
				memmove((char*)pair.s + nb, kbds, i);
				free(kbds);
				nb += i;
				kbdqr++;
			}
			pair.ns = nb;
			send(crm.c2, &pair);
			continue;
		case WMouse:
			if(w->mouseopen) {
				w->mouse.counter++;

				/* queue click events */
				if(!w->mouse.qfull && lastb != w->mc.buttons) {	/* add to ring */
					mp = &w->mouse.queue[w->mouse.wi];
					if(++w->mouse.wi == nelem(w->mouse.queue))
						w->mouse.wi = 0;
					if(w->mouse.wi == w->mouse.ri)
						w->mouse.qfull = TRUE;
					mp->Mouse = w->mc;
					mp->counter = w->mouse.counter;
					lastb = w->mc.buttons;
				}
			} else
				wmousectl(w);
			break;
		case WMouseread:
			/* send a queued event or, if the queue is empty, the current state */
			/* if the queue has filled, we discard all the events it contained. */
			/* the intent is to discard frantic clicking by the user during long latencies. */
			w->mouse.qfull = FALSE;
			if(w->mouse.wi != w->mouse.ri) {
				m = w->mouse.queue[w->mouse.ri];
				if(++w->mouse.ri == nelem(w->mouse.queue))
					w->mouse.ri = 0;
			} else
				m = (Mousestate){w->mc.Mouse, w->mouse.counter};

			w->mouse.lastcounter = m.counter;
			send(mrm.cm, &m.Mouse);
			continue;
		case WCtl:
			if(wctlmesg(w, wcm.type, wcm.r, wcm.p) == Exited){
				while(kbdqr != kbdqw)
					free(kbdq[kbdqr++ % nelem(kbdq)]);
				chanfree(crm.c1);
				chanfree(crm.c2);
				chanfree(mrm.cm);
				chanfree(cwm.cw);
				threadexits(nil);
			}
			continue;
		case WCwrite:
			recv(cwm.cw, &pair);
			rp = pair.s;
			nr = pair.ns;
			for(i=0; i<nr; i++)
				if(rp[i] == '\b'){
					up = rp+i;
					initial = 0;
					for(; i<nr; i++){
						if(rp[i] == '\b'){
							if(up == rp)
								initial++;
							else
								up--;
						}else
							*up++ = rp[i];
					}
					if(initial){
						if(initial > w->qh)
							initial = w->qh;
						qh = w->qh-initial;
						wdelete(w, qh, qh+initial);
						w->qh = qh;
					}
					nr = up - rp;
					break;
				}
			w->qh = winsert(w, rp, nr, w->qh)+nr;
			if(w->scrolling || w->mouseopen)
				wshow(w, w->qh);
			wsetselect(w, w->q0, w->q1);
			wscrdraw(w);
			free(rp);
			break;
		case WCread:
			recv(crm.c1, &pair);
			t = pair.s;
			nb = pair.ns;
			qh0 = w->qh; /* saved for histadd.c cf. below */
			i = npart;
			npart = 0;
			if(i)
				memmove(t, part, i);
			while(i<nb && (w->qh<w->nr || w->nraw>0)){
				if(w->qh == w->nr){
					wid = runetochar(t+i, &w->raw[0]);
					w->nraw--;
					runemove(w->raw, w->raw+1, w->nraw);
				}else
					wid = runetochar(t+i, &w->r[w->qh++]);
				c = t[i];	/* knows break characters fit in a byte */
				i += wid;
				if(!w->rawing && (c == '\n' || c=='\004')){
					if(c == '\004')
						i--;
					break;
				}
			}
			if(i==nb && w->qh<w->nr && w->r[w->qh]=='\004')
				w->qh++;
			if(i > nb){
				npart = i-nb;
				memmove(part, t+nb, npart);
				i = nb;
			}
			if(!w->rawing)
				histadd(w, qh0, w->qh);
			pair.s = t;
			pair.ns = i;
			send(crm.c2, &pair);
			continue;
		case WWread:
			w->wctlready = 0;
			recv(crm.c1, &pair);
			s = Dx(w->screenr) > 0 ? "visible" : "hidden";
			t = "notcurrent";
			if(w == input)
				t = "current";
			pair.ns = snprint(pair.s, pair.ns+1, "%11d %11d %11d %11d %11s %11s ",
				w->i->r.min.x, w->i->r.min.y, w->i->r.max.x, w->i->r.max.y, t, s);
			send(crm.c2, &pair);
			continue;
		case WComplete:
			if(w->i!=nil){
				if(!cr->advance)
					showcandidates(w, cr);
				if(cr->advance){
					rp = runesmprint("%s", cr->string);
					if(rp){
						nr = runestrlen(rp);
						q0 = w->q0;
						q0 = winsert(w, rp, nr, q0);
						wshow(w, q0+nr);
						free(rp);
					}
				}
			}
			freecompletion(cr);
			break;
		}
		if(w->i!=nil && Dx(w->screenr) > 0 && display->bufp > display->buf)
			flushimage(display, 1);
	}
}

void
wsetpid(Window *w, int pid, int dolabel)
{
	char buf[32];
	int ofd;

	ofd = w->notefd;
	if(pid <= 0)
		w->notefd = -1;
	else {
		if(dolabel){
			snprint(buf, sizeof(buf), "rc %lud", (ulong)pid);
			free(w->label);
			w->label = estrdup(buf);
		}
		snprint(buf, sizeof(buf), "/proc/%lud/notepg", (ulong)pid);
		w->notefd = open(buf, OWRITE|OCEXEC);
	}
	if(ofd >= 0)
		close(ofd);
}

void
winshell(void *args)
{
	Window *w;
	Channel *pidc;
	void **arg;
	char *cmd, *dir;
	char **argv;

	arg = args;
	w = arg[0];
	pidc = arg[1];
	cmd = arg[2];
	argv = arg[3];
	dir = arg[4];
	rfork(RFNAMEG|RFFDG|RFENVG);
	if(filsysmount(filsys, w->id) < 0){
		fprint(2, "mount failed: %r\n");
		sendul(pidc, 0);
		threadexits("mount failed");
	}
	close(0);
	if(open("/dev/cons", OREAD) < 0){
		fprint(2, "can't open /dev/cons: %r\n");
		sendul(pidc, 0);
		threadexits("/dev/cons");
	}
	close(1);
	if(open("/dev/cons", OWRITE) < 0){
		fprint(2, "can't open /dev/cons: %r\n");
		sendul(pidc, 0);
		threadexits("open");	/* BUG? was terminate() */
	}
	if(wclose(w) == 0){	/* remove extra ref hanging from creation */
		notify(nil);
		dup(1, 2);
		if(dir)
			chdir(dir);
		procexec(pidc, cmd, argv);
		_exits("exec failed");
	}
}
