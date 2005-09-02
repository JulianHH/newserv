
#ifndef __PROXYSCAN_H
#define __PROXYSCAN_H

#include "../nick/nick.h"
#include <time.h>

#define MAGICSTRING       "NOTICE AUTH :*** Looking up your hostname\r\n"
#define MAGICSTRINGLENGTH 42

#define PSCAN_MAXSCANS     50
#define PSCAN_READBUFSIZE (MAGICSTRINGLENGTH * 2)

#define SSTATE_CONNECTING   0
#define SSTATE_SENTREQUEST  1
#define SSTATE_GOTRESPONSE  2

#define STYPE_SOCKS4        0
#define STYPE_SOCKS5        1
#define STYPE_HTTP          2
#define STYPE_WINGATE       3
#define STYPE_CISCO         4
#define STYPE_DIRECT        5

#define SOUTCOME_INPROGRESS 0
#define SOUTCOME_OPEN       1
#define SOUTCOME_CLOSED     2

#define SCLASS_NORMAL       0
#define SCLASS_CHECK        1
#define SCLASS_PASS2        2
#define SCLASS_PASS3        3
#define SCLASS_PASS4        4

typedef struct scantype {
  int type;
  int port;
  int hits;
} scantype;

typedef struct pendingscan {
  unsigned int IP;
  unsigned short port;
  unsigned char type;
  unsigned char class;
  time_t when;
  struct pendingscan *next;
} pendingscan;

typedef struct foundproxy {
  short type;
  unsigned short port;
  struct foundproxy *next;
} foundproxy;

typedef struct cachehost {
  unsigned long IP;
  time_t lastscan;
  foundproxy *proxies;
  int glineid;
  unsigned char marker;
#if defined(PROXYSCAN_MAIL)
  sstring *lasthostmask; /* Not saved to disk */
  time_t lastconnect;    /* Not saved to disk */
#endif
  struct cachehost *next;
} cachehost;

typedef struct scan {
  int fd;
  unsigned int IP;
  short type;
  unsigned short port;
  unsigned short state;
  unsigned short outcome;
  unsigned short class;
  struct scan *next;
  void *sch;
  char readbuf[PSCAN_READBUFSIZE];
  int bytesread;
  int totalbytesread;
} scan;

#if defined(PROXYSCAN_MAIL)
extern unsigned int ps_mailip;
extern unsigned int ps_mailport;
extern sstring *ps_mailname;
extern int psm_mailerfd;
#endif

extern int activescans;
extern int maxscans;
extern int numscans;
extern scantype thescans[];
extern int brokendb;

extern unsigned int normalqueuedscans;
extern unsigned int prioqueuedscans;

/* proxyscancache.c */
cachehost *addcleanhost(unsigned long IP, time_t timestamp);
cachehost *findcachehost(unsigned long IP);
void delcachehost(cachehost *);
void dumpcachehosts();
void loadcachehosts();
unsigned int cleancount();
unsigned int dirtycount();
void cachehostinit(time_t ri);
void scanall(int type, int port);

/* proxyscanalloc.c */
scan *getscan();
void freescan(scan *sp);
cachehost *getcachehost();
void freecachehost(cachehost *chp);
foundproxy *getfoundproxy();
void freefoundproxy(foundproxy *fpp);
pendingscan *getpendingscan();
void freependingscan(pendingscan *psp);
void sfreeall();

/* proxyscanlisten.c */
int openlistensocket(int portnum);
void handlelistensocket(int fd, short events);

/* proxyscanconnect.c */
int createconnectsocket(long ip, int socknum);

/* proxyscandb.c */
void loggline(cachehost *chp);
void proxyscandbclose();
int proxyscandbinit();
void proxyscandolistopen(nick *mynick, nick *usernick, time_t snce);
const char *scantostr(int type);

#if defined(PROXYSCAN_MAIL)
/* proxyscanmail.c */
void ps_makereportmail(scanhost *shp);
#endif

/* proxyscanqueue.c */
void queuescan(unsigned int IP, short scantype, unsigned short port, char class, time_t when);
void startqueuedscans();

/* proxyscan.c */
void startscan(unsigned int IP, int type, int port, int class);


#endif
