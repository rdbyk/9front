enum
{
	Qdir,			/* /dev for this window */
	Qcons,
	Qconsctl,
	Qcursor,
	Qwdir,
	Qwinid,
	Qwinname,
	Qlabel,
	Qkbd,
	Qmouse,
	Qnew,
	Qscreen,
	Qsnarf,
	Qtext,
	Qhist,
	Qwctl,
	Qwindow,
	Qwsys,		/* directory of window directories */
	Qwsysdir,		/* window directory, child of wsys */

	QMAX,
};

#define	STACK	8192
#define	MAXSNARF	100*1024
#define MAXHISTLINES	64

typedef	struct	Consreadmesg Consreadmesg;
typedef	struct	Conswritemesg Conswritemesg;
typedef struct	Kbdreadmesg Kbdreadmesg;
typedef	struct	Stringpair Stringpair;
typedef	struct	Dirtab Dirtab;
typedef	struct	Fid Fid;
typedef	struct	Filsys Filsys;
typedef	struct	Mouseinfo	Mouseinfo;
typedef	struct	Mousereadmesg Mousereadmesg;
typedef	struct	Mousestate	Mousestate;
typedef	struct	Ref Ref;
typedef	struct	Timer Timer;
typedef	struct	Wctlmesg Wctlmesg;
typedef	struct	Window Window;
typedef struct	History History;
typedef	struct	Xfid Xfid;

enum
{
	Selborder		= 4,		/* border of selected window */
	Unselborder	= 1,		/* border of unselected window */
	Scrollwid 		= 12,		/* width of scroll bar */
	Scrollgap 		= 4,		/* gap right of scroll bar */
	BIG			= 3,		/* factor by which window dimension can exceed screen */
	TRUE		= 1,
	FALSE		= 0,
};

#define	QID(w,q)	((w<<8)|(q))
#define	WIN(q)	((((ulong)(q).path)>>8) & 0xFFFFFF)
#define	FILE(q)	(((ulong)(q).path) & 0xFF)

enum	/* control messages */
{
	Wakeup,
	Reshaped,
	Topped,
	Repaint,
	Refresh,
	Movemouse,
	Rawon,
	Rawoff,
	Holdon,
	Holdoff,
	Truncate,
	Deleted,
	Exited,
};

struct Wctlmesg
{
	int		type;
	Rectangle	r;
	void		*p;
};

struct Conswritemesg
{
	Channel	*cw;		/* chan(Stringpair) */
};

struct Consreadmesg
{
	Channel	*c1;		/* chan(tuple(char*, int) == Stringpair) */
	Channel	*c2;		/* chan(tuple(char*, int) == Stringpair) */
};

struct Mousereadmesg
{
	Channel	*cm;		/* chan(Mouse) */
};

struct Stringpair	/* rune and nrune or byte and nbyte */
{
	void		*s;
	int		ns;
};

struct Mousestate
{
	Mouse;
	ulong	counter;	/* serial no. of mouse event */
};

struct Mouseinfo
{
	Mousestate	queue[16];
	int	ri;	/* read index into queue */
	int	wi;	/* write index */
	ulong	counter;	/* serial no. of last mouse event we received */
	ulong	lastcounter;	/* serial no. of last mouse event sent to client */
	int	lastb;	/* last button state we received */
	uchar	qfull;	/* filled the queue; no more recording until client comes back */	
};

struct History
{
	Rune	**line;	/* array of history entries (lines) */
	int 	first;	/* array index of oldest (first) entry */
	int 	last;	/* array index of last entry */
	int		size;	/* current number of entries */
	int		pos;	/* navigation position (0..size),
					   pos==size for current edit line */
	Rune	*edit;	/* current edit line, to be restored for pos==size */
	Rune	*buf;	/* buffer for partially read lines */
	int     buflen;	/* buffer length */
};

struct Window
{
	Ref;
	QLock;
	Frame;
	Image		*i;		/* window image, nil when deleted */
	Mousectl	mc;
	Mouseinfo	mouse;
	Channel		*ck;		/* chan(char*) */
	Channel		*cctl;		/* chan(Wctlmesg)[4] */
	Channel		*conswrite;	/* chan(Conswritemesg) */
	Channel		*consread;	/* chan(Consreadmesg) */
	Channel		*mouseread;	/* chan(Mousereadmesg) */
	Channel		*wctlread;	/* chan(Consreadmesg) */
	Channel		*kbdread;	/* chan(Consreadmesg) */
	Channel		*complete;	/* chan(Completion*) */
	Channel		*gone;		/* chan(char*) */
	uint			nr;			/* number of runes in window */
	uint			maxr;		/* number of runes allocated in r */
	Rune			*r;
	uint			nraw;
	Rune			*raw;
	uint			org;    /* first visible char in window */
	uint			q0;     /* cursor or start of selection */
	uint			q1;     /* end of selection */
	uint			qh;     /* start of line to be sent to reader of cons file */
	int			id;
	char			name[32];
	uint			namecount;
	Rectangle		scrollr;
	/*
	 * Rio once used originwindow, so screenr could be different from i->r.
	 * Now they're always the same but the code doesn't assume so.
	*/
	Rectangle		screenr;	/* screen coordinates of window */
	int			resized;
	int			wctlready;
	Rectangle		lastsr;
	int			topped;
	int			notefd;
	uchar		scrolling;
	History		history;
	Cursor		cursor;
	Cursor		*cursorp;
	uchar		holding;
	uchar		rawing;
	uchar		ctlopen;
	uchar		wctlopen;
	uchar		deleted;
	uchar		mouseopen;
	uchar		kbdopen;
	uchar		winnameread;
	char			*label;
	char			*dir;
};

void		winctl(void*);
void		winshell(void*);
Window*	wlookid(int);
Window*	wmk(Image*, Mousectl*, Channel*, Channel*, int);
Window*	wpointto(Point);
Window*	wtop(Point);
void		wtopme(Window*);
void		wbottomme(Window*);
char*	wcontents(Window*, int*);
char*	whist(Window*, int*);
int		wclose(Window*);
int		wbswidth(Window*, Rune);
uint		wbacknl(Window*, uint, uint);
void		wcurrent(Window*);
void		wuncurrent(Window*);
uint		winsert(Window*, Rune*, int, uint);
void		wcut(Window*);
void		wdelete(Window*, uint, uint);
void		wpaste(Window*);
void		wplumb(Window*);
void		wlook(Window*);
void		wscrdraw(Window*);
void		wscroll(Window*, int);
void		wsend(Window*);
void		wsendctlmesg(Window*, int, Rectangle, void*);
void		wsetcursor(Window*, int);
void		wsetname(Window*);
void		wsetorigin(Window*, uint, int);
void        wsetselect(Window*, uint, uint);
void		wsetpid(Window*, int, int);
void		wshow(Window*, uint);
void		wsnarf(Window*);
void 		wscrsleep(Window*, uint);

struct Dirtab
{
	char		*name;
	uchar	type;
	uint		qid;
	uint		perm;
};

struct Fid
{
	int		fid;
	int		busy;
	int		open;
	int		mode;
	Qid		qid;
	Window	*w;
	Dirtab	*dir;
	Fid		*next;
	int		nrpart;
	uchar	rpart[UTFmax];
};

struct Xfid
{
		Ref;
		Xfid		*next;
		Xfid		*free;
		Fcall;
		Channel	*c;	/* chan(void(*)(Xfid*)) */
		Fid		*f;
		uchar	*buf;
		Filsys	*fs;
		int		flushtag;	/* our tag, so flush can find us */
		Channel	*flushc;	/* channel(int) to notify us we're being flushed */
};

Channel*	xfidinit(void);
void		xfidctl(void*);
void		xfidflush(Xfid*);
void		xfidattach(Xfid*);
void		xfidopen(Xfid*);
void		xfidclose(Xfid*);
void		xfidread(Xfid*);
void		xfidwrite(Xfid*);

enum
{
	Nhash	= 16,
};

struct Filsys
{
		int		cfd;
		int		sfd;
		int		pid;
		char		*user;
		Channel	*cxfidalloc;	/* chan(Xfid*) */
		Channel	*csyncflush;	/* chan(int) */
		Fid		*fids[Nhash];
};

Filsys*	filsysinit(Channel*);
int		filsysmount(Filsys*, int);
Xfid*		filsysrespond(Filsys*, Xfid*, Fcall*, char*);
void		filsyscancel(Xfid*);

void		wctlproc(void*);
void		wctlthread(void*);

void		deletetimeoutproc(void*);

struct Timer
{
	int		dt;
	int		cancel;
	Channel	*c;	/* chan(int) */
	Timer	*next;
};

Font		*font;
Mousectl	*mousectl;
Mouse	*mouse;
Display	*display;
Image	*view;
Screen	*wscreen;
Cursor	boxcursor;
Cursor	crosscursor;
Cursor	sightcursor;
Cursor	whitearrow;
Cursor	query;
Cursor	*corners[9];

Image	*background;
Image	*cols[NCOL];
Image	*titlecol;
Image	*lighttitlecol;
Image	*dholdcol;
Image	*holdcol;
Image	*lightholdcol;
Image	*paleholdcol;
Image	*paletextcol;
Image	*sizecol;
int	reverse;	/* there are no pastel paints in the dungeons and dragons world -- rob pike */

Window	**window;
Window	*wkeyboard;	/* window of simulated keyboard */
int		nwindow;
int		snarffd;
int		gotscreen;
int		servekbd;
Window	*input;
QLock	all;			/* BUG */
Filsys	*filsys;
Window	*hidden[100];
int		nhidden;
int		nsnarf;
Rune*	snarf;
int		scrolling;
int		maxtab;
Channel*	winclosechan;
char		*startdir;
int		sweeping;
int		wctlfd;
char		srvpipe[];
char		srvwctl[];
int		errorshouldabort;
int		menuing;		/* menu action is pending; waiting for window to be indicated */
int		snarfversion;	/* updated each time it is written */
int		messagesize;		/* negotiated in 9P version setup */
int		shiftdown;
int		debug;
