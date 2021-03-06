
#include "../smtp/smtp.h"
#include "../smtp/rfc822.tab.h"

typedef struct Addr Addr;
struct Addr
{
	char *addr;
	Addr *next;
};

String *from;
String *sender;
Field *firstfield;
int naddrlist;
Addr *addrlist;

extern String*	getaddr(Node *p);
extern void	getaddrs(void);
extern void	writeaddr(char *file, char *addr, int, char *);
extern void	remaddr(char *addr);
extern int	addaddr(char *addr);
extern void	readaddrs(char *file);
extern int	startmailer(char *name);
extern void	sendnotification(char *addr, char *listname, int rem);
