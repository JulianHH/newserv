/* server.c
 * 
 * This module is responsible for tracking the active server tree.
 */

#include "../parser/parser.h"
#include "../irc/irc_config.h"
#include "../irc/irc.h"
#include "../lib/base64.h"
#include "../lib/sstring.h"
#include "../core/error.h"
#include "../core/hooks.h"
#include "../core/config.h"
#include "../lib/version.h"
#include "server.h"

#include <stdio.h>
#include <string.h>

MODULE_VERSION("$Id: server.c 663 2006-05-16 17:27:36Z newserv $")

int findserver(const char *name);
void completelink(int servernum);

server serverlist[MAXSERVERS];
int myhub;

void _init() {
  /* Initialise the server tree */
  memset(serverlist,0,MAXSERVERS*sizeof(server));
  
  myhub=-1;
  
  serverlist[numerictolong(mynumeric->content,2)].parent=-1;
  serverlist[numerictolong(mynumeric->content,2)].name=getsstring(myserver->content,HOSTLEN);
  serverlist[numerictolong(mynumeric->content,2)].description=getcopyconfigitem("irc","serverdescription","newserv",100);
  serverlist[numerictolong(mynumeric->content,2)].maxusernum=MAXLOCALUSER;
  serverlist[numerictolong(mynumeric->content,2)].linkstate=LS_LINKED;
  
  /* Register the protocol messages we handle */
  registerserverhandler("SERVER",&handleservermsg,8);
  registerserverhandler("S",&handleservermsg,8);
  registerserverhandler("SQ",&handlesquitmsg,3);
  registerserverhandler("EB",&handleeobmsg,0);
  
  /* And the events we hook */
  registerhook(HOOK_IRC_DISCON,&handledisconnect);
}

void _fini() {
  int i;

  /* Deregister everything */
  deregisterserverhandler("SERVER",&handleservermsg);
  deregisterserverhandler("S",&handleservermsg);
  deregisterserverhandler("SQ",&handlesquitmsg);
  deregisterserverhandler("EB",&handleeobmsg);
  deregisterhook(HOOK_IRC_DISCON,&handledisconnect);
  
  for (i=0;i<MAXSERVERS;i++) {
    if (serverlist[i].name!=NULL)
      freesstring(serverlist[i].name);
  }
}

int handleservermsg(void *source, int cargc, char **cargv) {
  int servernum;
  
  servernum=numerictolong(cargv[5],2);
  
  if (serverlist[servernum].name!=NULL) {
    Error("server",ERR_ERROR,"New server %d already exists in servertable.",servernum);
    return CMD_ERROR;
  }
 
  serverlist[servernum].name=getsstring(cargv[0],HOSTLEN);
  serverlist[servernum].description=getsstring(cargv[cargc-1],REALLEN);
  serverlist[servernum].maxusernum=numerictolong(cargv[5]+2,3);
  
  if (!strncmp((char *)source,"INIT",4)) {
    /* This is the initial server */
    myhub=servernum;
    serverlist[servernum].parent=numerictolong(mynumeric->content,2);
  } else {
    serverlist[servernum].parent=numerictolong(source,2);
  }    

  /* Set up the initial server state -- this MUST be one of the "linking" states */
  if (cargv[4][0]=='J') {
    serverlist[servernum].linkstate=LS_LINKING;
    Error("server",ERR_DEBUG,"Creating server %s in LS_LINKING state.",serverlist[servernum].name->content);
  } else {
    serverlist[servernum].linkstate=LS_PLINKING;
    Error("server",ERR_DEBUG,"Creating server %s in LS_PLINKING state.",serverlist[servernum].name->content);
  }

  triggerhook(HOOK_SERVER_NEWSERVER,(void *)servernum);  
  return CMD_OK;
}

int handleeobmsg(void *source, int cargc, char **argv) {
  int servernum;
  
  servernum=numerictolong(source,2);
  completelink(servernum);
  
  if (servernum==myhub) {
    /* Send EA */
    irc_send("%s EA",mynumeric->content);  
    Error("server",ERR_INFO,"Acknowledging end of burst");
    triggerhook(HOOK_SERVER_END_OF_BURST, NULL);
  }
  
  return CMD_OK;
}

int handlesquitmsg(void *source, int cargc, char **cargv) {
  int servernum=findserver(cargv[0]);
  if (servernum<0) {
    Error("server",ERR_WARNING,"Received SQUIT for unknown server %s\n",cargv[0]);
    return CMD_ERROR;
  }
  if (servernum==myhub) {
    Error("server",ERR_WARNING,"Rejected by our hub: %s",cargv[cargc-1]);
    irc_disconnected();
    return CMD_OK;
  }
  deleteserver(servernum);
  
  return CMD_OK;
}

void handledisconnect(int hooknum, void *arg) {
  if (myhub>=0) {
    deleteserver(myhub);
    myhub=-1;
  }
}

void completelink(int servernum) {
  int i;
  
  if (serverlist[servernum].name==NULL) {
    Error("server",ERR_WARNING,"Tried to complete link for server %d which doesn't exist.",servernum);
    return;
  }
  
  /* Complete the link for all children in the PLINKING state. */
  for (i=0;i<MAXSERVERS;i++) {
    if (serverlist[i].parent==servernum && serverlist[i].linkstate==LS_PLINKING) {
      completelink(i);
    }
  }
  
  /* This server is now fully linked. */
  serverlist[servernum].linkstate=LS_LINKED;
  Error("server",ERR_DEBUG,"Setting link state on %s to LS_LINKED",serverlist[servernum].name->content);
}  

void deleteserver(int servernum) {
  int i;
  
  if (serverlist[servernum].name==NULL) {
    Error("server",ERR_WARNING,"Tried to remove server %d which doesn't exist.",servernum);
    return;
  }
  
  /* Delete all it's children first */
  for (i=0;i<MAXSERVERS;i++) {
    if (serverlist[i].parent==servernum && serverlist[i].name!=NULL) {
      deleteserver(i);
    }
  }
  
  /* Set state to SQUITting, then trigger hook */
  Error("server",ERR_DEBUG,"Setting link state on %s to LS_SQUIT",serverlist[servernum].name->content);
  serverlist[servernum].linkstate=LS_SQUIT;

  /* Until hooks have priorities we need something like this */
  triggerhook(HOOK_SERVER_PRE_LOSTSERVER,(void *)servernum);
  triggerhook(HOOK_SERVER_LOSTSERVER,(void *)servernum);
  
  /* Now delete the actual server */
  freesstring(serverlist[servernum].name);
  freesstring(serverlist[servernum].description);
  memset(&(serverlist[servernum]),0,sizeof(server)); /* sets state back to LS_INVALID */
}

int findserver(const char *name) {
  int i;
  
  for(i=0;i<MAXSERVERS;i++)
    if (serverlist[i].name!=NULL && !strncmp(serverlist[i].name->content,name,serverlist[i].name->length))
      return i;
      
  return -1;
}

