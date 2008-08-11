#ifndef __AUTHEXT_H
#define __AUTHEXT_H

#include "../irc/irc_config.h"
#include "../lib/flags.h"
#include "../lib/ccassert.h"

#include <sys/types.h>

#define MAXAUTHNAMEEXTS 5

/* checking to see that u_int64_t == unsigned long long for strtoull */
CCASSERT(sizeof(unsigned long long) == sizeof(u_int64_t))

struct nick;

typedef struct authname {
  unsigned long userid;
  int usercount;
  unsigned int marker;
  struct nick *nicks;
  struct authname *next, *nextbyname;
  unsigned int namebucket;
  u_int64_t flags;
  char name[ACCOUNTLEN+1];
  /* These are extensions only used by other modules */
  void *exts[MAXAUTHNAMEEXTS];
} authname;

#define AUTHNAMEHASHSIZE  60000

extern authname *authnametable[AUTHNAMEHASHSIZE];

/* Allocators */
authname *newauthname(void);
void freeauthname (authname *hp);

/* EXT management */
int registerauthnameext(const char *name);
int findauthnameext(const char *name);
void releaseauthnameext(int index);

/* Actual user commands */
authname *findauthname(unsigned long userid);
authname *findauthnamebyname(const char *name);
authname *findorcreateauthname(unsigned long userid, const char *name);
void releaseauthname(authname *anp);

/* Marker */
unsigned int nextauthnamemarker(void);

authname *getauthbyname(const char *name);

#endif
