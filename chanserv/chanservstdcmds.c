/*
 * This contains Q9's "built in" commands and CTCP handlers
 */

#include "chanserv.h"
#include "../core/schedule.h"
#include "../lib/irc_string.h"
#include "../pqsql/pqsql.h"

#include <string.h>

int cs_dorehash(void *source, int cargc, char **cargv) {
  nick *sender=source;
  Command *cmdlist[200];
  int i,n;
  
  /* Reload the response text first */
  loadmessages();

  /* Now the commands */
  n=getcommandlist(cscommands, cmdlist, 200);
  
  for(i=0;i<n;i++)
    loadcommandsummary(cmdlist[i]);

  chanservstdmessage(sender, QM_DONE);

  return CMD_OK;
}

int cs_doquit(void *source, int cargc, char **cargv) {
  char *reason="Leaving";
  nick *sender=(nick *)source;

  if (cargc>0) {
    reason=cargv[0];
  }

  chanservstdmessage(sender, QM_DONE);

  deregisterlocaluser(chanservnick, reason);  
  scheduleoneshot(time(NULL)+1,&chanservreguser,NULL);

  return CMD_OK;
}

int cs_dorename(void *source, int cargc, char **cargv) {
  char newnick[NICKLEN+1];
  nick *sender=source;

  if (cargc<1) {
    chanservstdmessage(sender, QM_NOTENOUGHPARAMS, "rename");
    return CMD_ERROR;
  }
  
  strncpy(newnick,cargv[0],NICKLEN);
  newnick[NICKLEN]='\0';
  
  renamelocaluser(chanservnick, newnick);
  chanservstdmessage(sender, QM_DONE);

  return CMD_OK;
}

int cs_doshowcommands(void *source, int cargc, char **cargv) {
  nick *sender=source;
  reguser *rup;
  Command *cmdlist[200];
  int i,n;
  int lang;
  char *message;
  cmdsummary *summary;
  
  n=getcommandlist(cscommands, cmdlist, 200);
  rup=getreguserfromnick(sender);

  if (!rup)
    lang=0;
  else
    lang=rup->languageid;

  chanservstdmessage(sender, QM_COMMANDLIST);

  for (i=0;i<n;i++) {
    if (cargc>0 && !match2strings(cargv[0],cmdlist[i]->command->content))
      continue;

    /* Don't list aliases */
    if (cmdlist[i]->level & QCMD_ALIAS)
      continue;
    
    /* Check that this user can use this command.. */
    if ((cmdlist[i]->level & QCMD_AUTHED) && !rup)
      continue;

    if ((cmdlist[i]->level & QCMD_NOTAUTHED) && rup)
      continue;

    if ((cmdlist[i]->level & QCMD_HELPER) && 
	(!rup || !UHasHelperPriv(rup)))
      continue;

    if ((cmdlist[i]->level & QCMD_OPER) &&
	(!rup || !UHasOperPriv(rup) || !IsOper(sender)))
      continue;

    if ((cmdlist[i]->level & QCMD_ADMIN) &&
	(!rup || !UHasAdminPriv(rup) || !IsOper(sender)))
      continue;
    
    if ((cmdlist[i]->level & QCMD_DEV) &&
	(!rup || !UIsDev(rup) || !IsOper(sender)))
      continue;
    
    summary=(cmdsummary *)cmdlist[i]->ext;
    
    if (summary->bylang[lang]) {
      message=summary->bylang[lang]->content;
    } else if (summary->bylang[0]) {
      message=summary->bylang[0]->content;
    } else {
      message=summary->def->content;
    }
    
    chanservsendmessage(sender, "%-20s %s",cmdlist[i]->command->content, message);
  }

  chanservstdmessage(sender, QM_ENDOFLIST);

  return CMD_OK;
}

int cs_sendhelp(nick *sender, char *thecmd, int oneline) {
  Command *cmd;
  cmdsummary *sum;
  reguser *rup;
  
  if (!(cmd=findcommandintree(cscommands, thecmd, 1))) {
    chanservstdmessage(sender, QM_UNKNOWNCMD, thecmd);
    return CMD_ERROR;
  }
  
/*  Disable database help for now - splidge
  csdb_dohelp(sender, cmd); */
  
  rup=getreguserfromnick(sender);
  
  /* Don't showhelp for privileged users to others.. */
  if (((cmd->level & QCMD_HELPER) && (!rup || !UHasHelperPriv(rup))) ||
      ((cmd->level & QCMD_OPER) && (!rup || !UHasOperPriv(rup))) ||
      ((cmd->level & QCMD_ADMIN) && (!rup || !UHasAdminPriv(rup))) ||
      ((cmd->level & QCMD_DEV) && (!rup || !UIsDev(rup)))) {
    chanservstdmessage(sender, QM_NOHELP, cmd->command->content);
    return CMD_OK;
  }

  sum=cmd->ext;

  if (sum->defhelp && *(sum->defhelp)) {
    if (oneline) {
      chanservsendmessageoneline(sender, sum->defhelp);
    } else {
      chanservsendmessage(sender, sum->defhelp);
    }
  } else {
    if (!oneline)
      chanservstdmessage(sender, QM_NOHELP, cmd->command->content);
  }

  return CMD_OK;
}


int cs_dohelp(void *source, int cargc, char **cargv) {
  nick *sender=source;

  if (cargc==0)
    return cs_doshowcommands(source,cargc,cargv);
  
  return cs_sendhelp(sender, cargv[0], 0);
}


int cs_doctcpping(void *source, int cargc, char **cargv) {
  char *nullbuf="\001";
  
  sendnoticetouser(chanservnick, source, "%cPING %s",
		   1, cargc?cargv[0]:nullbuf);

  return CMD_OK;
}
  
int cs_doctcpversion(void *source, int cargc, char **cargv) {
  sendnoticetouser(chanservnick, source, "\01VERSION Q9 version %s (Compiled on " __DATE__ ")  (C) 2002-3 David Mansell (splidge)\01", QVERSION);
  sendnoticetouser(chanservnick, source, "\01VERSION Built on newserv version 1.00.  (C) 2002-3 David Mansell (splidge)\01");

  return CMD_OK;
}

int cs_doversion(void *source, int cargc, char **cargv) {
  chanservsendmessage((nick *)source, "Q9 version %s (Compiled on " __DATE__ ") (C) 2002-3 David Mansell (splidge)", QVERSION);
  chanservsendmessage((nick *)source, "Built on newserv version 1.00.  (C) 2002-3 David Mansell (splidge)");
  return CMD_OK;
}

int cs_doctcpgender(void *source, int cargc, char **cargv) {
  sendnoticetouser(chanservnick, source, "\1GENDER Anyone who has a bitch mode has to be female ;)\1");

  return CMD_OK;
}

void csdb_dohelp_real(PGconn *, void *);

struct helpinfo {
  unsigned int numeric;
  sstring *commandname;
  Command *cmd;
};

/* Help stuff */
void csdb_dohelp(nick *np, Command *cmd) {
  struct helpinfo *hip;

  hip=(struct helpinfo *)malloc(sizeof(struct helpinfo));

  hip->numeric=np->numeric;
  hip->commandname=getsstring(cmd->command->content, cmd->command->length);
  hip->cmd=cmd;

  q9asyncquery(csdb_dohelp_real, (void *)hip, 
		  "SELECT languageID, fullinfo from help where lower(command)=lower('%s')",cmd->command->content);
}

void csdb_dohelp_real(PGconn *dbconn, void *arg) {
  struct helpinfo *hip=arg;
  nick *np=getnickbynumeric(hip->numeric);
  reguser *rup;
  char *result;
  PGresult *pgres;
  int i,j,num,lang=0;
  
  if(!dbconn) {
    freesstring(hip->commandname);
    free(hip);
    return; 
  }

  pgres=PQgetResult(dbconn);

  if (PQresultStatus(pgres) != PGRES_TUPLES_OK) {
    Error("chanserv",ERR_ERROR,"Error loading help text.");
    freesstring(hip->commandname);
    free(hip);
    return; 
  }

  if (PQnfields(pgres)!=2) {
    Error("chanserv",ERR_ERROR,"Help text format error.");
    PQclear(pgres);
    freesstring(hip->commandname);
    free(hip);
    return;
  }
  
  num=PQntuples(pgres);
  
  if (!np) {
    PQclear(pgres);
    freesstring(hip->commandname);
    free(hip);
    return;
  }

  if ((rup=getreguserfromnick(np)))
    lang=rup->languageid;

  result=NULL;
  
  for (i=0;i<num;i++) {
    j=strtoul(PQgetvalue(pgres,i,0),NULL,10);
    if ((j==0 && result==NULL) || (j==lang)) {
      result=PQgetvalue(pgres,i,1);
      if(strlen(result)==0)
	result=NULL;
    }
  }

  if (result) {
    chanservsendmessage(np, result);
  } else {
    cmdsummary *sum=hip->cmd->ext;
    if (sum->defhelp && *(sum->defhelp)) {
      chanservsendmessage(np, sum->defhelp);
    } else {
      chanservstdmessage(np, QM_NOHELP, hip->commandname->content);
    }
  }
  
  freesstring(hip->commandname);
  free(hip);
}
