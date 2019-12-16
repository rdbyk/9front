#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <cursor.h>
#include <mouse.h>
#include <keyboard.h>
#include <frame.h>
#include <fcall.h>
#include "dat.h"
#include "fns.h"

/* 
 * NOTICE: code is heavily inspired by "Riox - a modified version of rio
 * window manager" which is part of "Object Icon", which is MIT licensed,
 * cf. "/lib/legal/mit.objecticon".
 */

void
histinit(Window *w)
{
	History *h = &w->history;

	h->line = emalloc(MAXHISTLINES * sizeof(Rune *));
	h->first = h->last = h->pos = h->size = 0;
	h->edit = nil;
	h->buf = nil;
	h->buflen = 0;	
}

/*
 * Remove oldest (first) history entry
 */
static void
histremove(Window *w)
{
	History *h = &w->history;

	if(h->size == 0)
		return;
	free(h->line[h->first]);
	h->first = (h->first + 1) % MAXHISTLINES;
	h->size--;
}

/*
 * Add new (last) history entry
 *
 * Rune sequence (from..to) is handled, just before it is sent to the
 * cons reader. If the sequence is ending with  '\n' or '^d', then a
 * new history entry is created, otherwise the sequence is buffered.
 */
void
histadd(Window *w, int from, int to)
{
	int i;
	History *h = &w->history;

	/* append to buffer */
	h->buf = erealloc(h->buf, (h->buflen + to - from) * sizeof(Rune));
	for(i = from; i < to; ++i)
		h->buf[h->buflen++] = w->r[i];

	if(h->buflen > 0){
		Rune end = h->buf[h->buflen - 1];
		if(end == '\n' || end == Keof /* EOF (control-D) */){
			if(h->buflen > 1){ /* line not empty */
				Rune *line = emalloc(h->buflen * sizeof(Rune));
				memcpy(line, h->buf, (h->buflen - 1) * sizeof(Rune));
				line[h->buflen - 1] = '\0';
				if(h->size == MAXHISTLINES) /* history full? */
					histremove(w);
				h->line[h->last] = line;
				h->last = (h->last + 1) % MAXHISTLINES;
				h->size++;
			}
		}
		h->buflen = 0;
		histreset(w);
	}
}

/*
 * Insert history entry (line)
 *
 * Save current edit line and replace it by an entry (line) from the
 * history specified by "pos".
 */
void
histinsert(Window *w, int pos)
{
	History *h = &w->history;
	Rune *line;

	if(pos < 0 || pos > h->size)
		return;

	/* save current edit line? */
	if(h->pos == h->size){
		int len = w->nr - w->qh;
		h->edit = erealloc(h->edit, (len + 1) * sizeof(Rune));
		memcpy(h->edit, &w->r[w->qh], len * sizeof(Rune));
		h->edit[len] = '\0';
	}

	/* goto end and delete current edit line */
	wsetselect(w, w->nr, w->nr);
	if(w->qh != w->nr){
		int nb = wbswidth(w, 0x15); /* 0x15 = ^U */
		if(nb > 0){
			wdelete(w, w->nr - nb, w->nr);
			wsetselect(w, w->nr, w->nr); 
		}
	}

	/* line to insert */
	if(pos == h->size)
		line = h->edit;
	else
		line = histentry(w, pos);

	/* insertion */
	winsert(w, line, runestrlen(line), w->nr);
	wsetselect(w, w->nr, w->nr);
	wshow(w, w->nr);

	/* new position */
	h->pos = pos;
}

/*
 * Free allocated history memory
 */
void
histfree(Window *w)
{
	History *h = &w->history;
	while(h->size > 0)
		histremove(w);
	free(h->line);
	free(h->edit);
	free(h->buf);
}
