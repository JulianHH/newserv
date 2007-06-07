/*
 * KILL functionality 
 */

#include "newsearch.h"

#include <stdio.h>
#include <stdlib.h>

#include "../control/control.h" /* controlreply() */
#include "../localuser/localuser.h" /* killuser() */
#include "../lib/irc_string.h" /* IPtostr() */

/* used for *_free functions that need to warn users of certain things
   i.e. hitting too many users in a (kill) or (gline) - declared in newsearch.c */
extern nick *senderNSExtern;

void *kill_exe(struct searchNode *thenode, void *theinput);
void kill_free(struct searchNode *thenode);

struct kill_localdata {
  unsigned int marker;
  int count;
  int type;
  char reason[NSMAX_REASON_LEN];
};

struct searchNode *kill_parse(int type, int argc, char **argv) {
  struct kill_localdata *localdata;
  struct searchNode *thenode;
  int len;

  if (!(localdata = (struct kill_localdata *) malloc(sizeof(struct kill_localdata)))) {
    parseError = "malloc: could not allocate memory for this search.";
    return NULL;
  }
  localdata->count = 0;
  localdata->type = type;
  if (type == SEARCHTYPE_CHANNEL)
    localdata->marker = nextchanmarker();
  else
    localdata->marker = nextnickmarker();

  if (argc==1) {
    len = snprintf(localdata->reason, NSMAX_REASON_LEN, ":%s", argv[0]);
    /* strip leading and trailing '"'s */
    localdata->reason[1] = ' ';
    localdata->reason[len-1] = '\0';
  }
  else
    snprintf(localdata->reason, NSMAX_REASON_LEN, ".");

  if (!(thenode=(struct searchNode *)malloc(sizeof (struct searchNode)))) {
    /* couldn't malloc() memory for thenode, so free localdata to avoid leakage */
    parseError = "malloc: could not allocate memory for this search.";
    free(localdata);
    return NULL;
  }

  thenode->returntype = RETURNTYPE_BOOL;
  thenode->localdata = localdata;
  thenode->exe = kill_exe;
  thenode->free = kill_free;

  return thenode;
}

void *kill_exe(struct searchNode *thenode, void *theinput) {
  struct kill_localdata *localdata;
  nick *np;
  chanindex *cip;

  localdata = thenode->localdata;

  if (localdata->type == SEARCHTYPE_CHANNEL) {
    cip = (chanindex *)theinput;
    cip->marker = localdata->marker;
    localdata->count += cip->channel->users->totalusers;
  }
  else {
    np = (nick *)theinput;
    np->marker = localdata->marker;
    localdata->count++;
  }

  return (void *)1;
}

void kill_free(struct searchNode *thenode) {
  struct kill_localdata *localdata;
  nick *np, *nnp;
  chanindex *cip, *ncip;
  int i, j, safe=0;

  localdata = thenode->localdata;

  if (localdata->count > NSMAX_KILL_LIMIT) {
    /* need to warn the user that they have just tried to twat half the network ... */
    controlreply(senderNSExtern, "Warning: your pattern matches too many users (%d) - nothing done.", localdata->count);
    free(localdata);
    free(thenode);
    return;
  }

  if (localdata->type == SEARCHTYPE_CHANNEL) {
    for (i=0;i<CHANNELHASHSIZE;i++) {
      for (cip=chantable[i];cip;cip=ncip) {
        ncip = cip->next;
        if (cip != NULL && cip->channel != NULL && cip->marker == localdata->marker) {
          for (j=0;j<cip->channel->users->hashsize;j++) {
            if (cip->channel->users->content[j]==nouser)
              continue;
    
            if ((np=getnickbynumeric(cip->channel->users->content[j]))) {
              if (!IsOper(np) && !IsService(np) && !IsXOper(np)) {
                killuser(NULL, np, "You (%s!%s@%s) have been disconnected for violating our terms of service%s", np->nick,
                  np->ident, IPtostr(np->p_ipaddr), localdata->reason);
              }
              else
                safe++;
            }
          }
        }
      }
    }
  }
  else {
    for (i=0;i<NICKHASHSIZE;i++) {
      for (np=nicktable[i];np;np=nnp) {
        nnp = np->next;
        if (np->marker == localdata->marker) {
          if (!IsOper(np) && !IsService(np) && !IsXOper(np)) {
            killuser(NULL, np, "You (%s!%s@%s) have been disconnected for violating our terms of service%s", np->nick,
              np->ident, IPtostr(np->p_ipaddr), localdata->reason);
          }
          else
              safe++;
        }
      }
    }
  }
  if (safe)
    controlreply(senderNSExtern, "Warning: your pattern matched privileged users (%d in total) - these have not been touched.", safe);
  /* notify opers of the action */
  controlwall(NO_OPER, NL_KICKKILLS, "%s/%s killed %d %s via %s [%d untouched].", senderNSExtern->nick, senderNSExtern->authname, (localdata->count - safe), 
    (localdata->count - safe) != 1 ? "users" : "user", (localdata->type == SEARCHTYPE_CHANNEL) ? "chansearch" : "nicksearch", safe);
  free(localdata);
  free(thenode);
}
