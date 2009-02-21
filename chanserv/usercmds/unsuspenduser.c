/* Automatically generated by refactor.pl.
 *
 *
 * CMDNAME: unsuspenduser
 * CMDLEVEL: QCMD_OPER
 * CMDARGS: 2
 * CMDDESC: Unsuspend a user.
 * CMDFUNC: csu_dounsuspenduser
 * CMDPROTO: int csu_dounsuspenduser(void *source, int cargc, char **cargv);
 * CMDHELP: Usage: unsuspenduser <username> <reason>
 * CMDHELP: Unsuspends the specified user.
 */

#include "../chanserv.h"
#include "../../lib/irc_string.h"
#include <stdio.h>
#include <string.h>

int csu_dounsuspenduser(void *source, int cargc, char **cargv) {
  nick *sender=source;
  reguser *rup=getreguserfromnick(sender);
  reguser *vrup, *suspendedby;
  char action[100];
  char *csuspendedby, *csuspendreason;
  char *unsuspendreason;
  
  if (!rup)
    return CMD_ERROR;
  
  if (cargc < 2) {
    chanservstdmessage(sender, QM_NOTENOUGHPARAMS, "unsuspenduser");
    return CMD_ERROR;
  }

  unsuspendreason = cargv[1];
  if(!checkreason(sender, unsuspendreason))
    return CMD_ERROR;

  if (cargv[0][0] == '#') {
    if (!(vrup=findreguserbynick(&cargv[0][1]))) {
      chanservstdmessage(sender, QM_UNKNOWNUSER, &cargv[0][1]);
      return CMD_ERROR;
    }
  }
  else {
    nick *np;
    
    if (!(np=getnickbynick(cargv[0]))) {
      chanservstdmessage(sender, QM_UNKNOWNUSER, cargv[0]);
      return CMD_ERROR;
    }
    
    if (!(vrup=getreguserfromnick(np)) && sender) {
      chanservstdmessage(sender, QM_USERNOTAUTHED, cargv[0]);
      return CMD_ERROR;
    }
  }
  
  if (!UHasSuspension(vrup)) {
    chanservstdmessage(sender, QM_USERNOTSUSPENDED, cargv[0]);
    return CMD_ERROR;
  }
  
  if (UHasOperPriv(vrup) && !UHasAdminPriv(rup)) {
    snprintf(action, 99, "unsuspenduser on %s", vrup->username);
    chanservstdmessage(sender, QM_NOACCESS, action);
    chanservwallmessage("%s (%s) FAILED to UNSUSPENDUSER %s", sender->nick, rup->username, vrup->username);
    cs_log(sender, "UNSUSPENDUSER FAILED (not admin) %s", vrup->username);
    return CMD_ERROR;
  }
  
  if (UIsDelayedGline(vrup)) {
    strcpy(action, "delayed gline");
  }
  else if (UIsGline(vrup)) {
    strcpy(action, "instant gline");
  }
  else if (UIsSuspended(vrup)) {
    strcpy(action, "normal");
  }
  else {
    chanservsendmessage(sender, "Unknown suspension type encountered.");
    return CMD_ERROR;
  }

  suspendedby = findreguserbyID(vrup->suspendby);
  csuspendedby = suspendedby?suspendedby->username:"(unknown)";
  csuspendreason = vrup->suspendreason?vrup->suspendreason->content:"(no reason)";

  chanservwallmessage("%s (%s) used UNSUSPENDUSER on %s, type: %s, suspended by: %s, suspension reason: %s, unsuspend reason: %s", sender->nick, rup->username, vrup->username, action, csuspendedby, csuspendreason, unsuspendreason);
  cs_log(sender,"UNSUSPENDUSER %s (%s, by: %s reason: %s), reason: %s", vrup->username, action, csuspendedby, csuspendreason, unsuspendreason);

  vrup->flags&=(~(QUFLAG_GLINE|QUFLAG_DELAYEDGLINE|QUFLAG_SUSPENDED));
  vrup->suspendby=0;
  vrup->suspendexp=0;
  freesstring(vrup->suspendreason);
  vrup->suspendreason=0;
  csdb_updateuser(vrup);
  
  chanservstdmessage(sender, QM_DONE);
  return CMD_OK;
}
