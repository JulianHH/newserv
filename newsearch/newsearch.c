
#include <stdio.h>
#include "newsearch.h"

#include "../irc/irc_config.h"
#include "../lib/irc_string.h"
#include "../parser/parser.h"
#include "../control/control.h"
#include "../lib/splitline.h"
#include "../lib/version.h"

MODULE_VERSION("");

CommandTree *searchTree;

int do_nicksearch(void *source, int cargc, char **cargv);
int do_chansearch(void *source, int cargc, char **cargv);
struct searchNode *search_parse(int type, char *input);

void registersearchterm(char *term, parseFunc parsefunc);
void deregistersearchterm(char *term, parseFunc parsefunc);

void *trueval(int type);
void *falseval(int type);

const char *parseError;
/* used for *_free functions that need to warn users of certain things
   i.e. hitting too many users in a (kill) or (gline) */
nick *senderNSExtern;

void _init() {
  searchTree=newcommandtree();

  /* Boolean operations */
  registersearchterm("and",and_parse);
  registersearchterm("not",not_parse);
  registersearchterm("or",or_parse);

  registersearchterm("eq",eq_parse);

  registersearchterm("lt",lt_parse);
  registersearchterm("gt",gt_parse);
 
  /* String operations */
  registersearchterm("match",match_parse);
  registersearchterm("regex",regex_parse);
  registersearchterm("length",length_parse);
  
  /* Nickname operations */
  registersearchterm("hostmask",hostmask_parse);
  registersearchterm("realname",realname_parse);
  registersearchterm("authname",authname_parse);
  registersearchterm("ident",ident_parse);
  registersearchterm("host",host_parse);
  registersearchterm("channel",channel_parse);
  registersearchterm("timestamp",timestamp_parse);
  registersearchterm("country",country_parse);
  registersearchterm("ip",ip_parse);

  /* Channel operations */
  registersearchterm("exists",exists_parse);
  registersearchterm("services",services_parse);
  registersearchterm("size",size_parse);
  registersearchterm("name",name_parse);
  registersearchterm("topic",topic_parse);
  registersearchterm("oppct",oppct_parse);
  registersearchterm("uniquehostpct",hostpct_parse);
  registersearchterm("authedpct",authedpct_parse);

  /* Nickname / channel operations */
  registersearchterm("modes",modes_parse);
  registersearchterm("nick",nick_parse);

  /* Kill / gline parameters */
  registersearchterm("kill",kill_parse);
  registersearchterm("gline",gline_parse);

  registercontrolhelpcmd("nicksearch",NO_OPER,4,do_nicksearch, "Usage: nicksearch <criteria>\nSearches for nicknames with the given criteria.");
  registercontrolhelpcmd("chansearch",NO_OPER,4,do_chansearch, "Usage: chansearch <criteria>\nSearches for channels with the given criteria.");
}

void _fini() {
  destroycommandtree(searchTree);
  deregistercontrolcmd("nicksearch", do_nicksearch);
  deregistercontrolcmd("chansearch", do_chansearch);
}

void registersearchterm(char *term, parseFunc parsefunc) {
  addcommandtotree(searchTree, term, 0, 0, (CommandHandler) parsefunc);
}

void deregistersearchterm(char *term, parseFunc parsefunc) {
  deletecommandfromtree(searchTree, term, (CommandHandler) parsefunc);
}

void printnick(nick *sender, nick *np) {
  char hostbuf[HOSTLEN+NICKLEN+USERLEN+4];

  controlreply(sender,"%s [%s] (%s) (%s)",visiblehostmask(np,hostbuf),
	       IPtostr(np->p_ipaddr), printflags(np->umodes, umodeflags), np->realname->name->content);
}

void printchannel(nick *sender, chanindex *cip) {
  /* shamelessly stolen from (now defunct) chansearch.c */
  int i;
  int op,voice,peon;
  int oper,service,hosts;
  nick *np;
  chanuserhash *cuhp;
  unsigned int marker;
  
  op=voice=peon=oper=service=hosts=0;
  marker=nexthostmarker();
  
  if (cip->channel==NULL) {
    controlreply(sender,"[         Channel currently empty          ] %s",cip->name->content);
  } else {
    cuhp=cip->channel->users;
    for (i=0;i<cuhp->hashsize;i++) {
      if (cuhp->content[i]!=nouser) {
        if (cuhp->content[i]&CUMODE_OP) {
          op++;
        } else if (cuhp->content[i]&CUMODE_VOICE) {
          voice++;
        } else {
          peon++;
        }
        if ((np=getnickbynumeric(cuhp->content[i]&CU_NUMERICMASK))!=NULL) {
          if (IsOper(np)) {
            oper++;
          }
          if (IsService(np)) {
            service++;
          }
          if (np->host->marker!=marker) {
            np->host->marker=marker;
            hosts++;
          }            
        }
      }
    }
    controlreply(sender,"[ %4dU %4d@ %4d+ %4d %4d* %4dk %4dH ] %s (%s)",cuhp->totalusers,op,voice,peon,oper,
      service,hosts,cip->name->content, printflags(cip->channel->flags, cmodeflags));
  }
}

int do_nicksearch(void *source, int cargc, char **cargv) {
  nick *sender = senderNSExtern = source, *np;
  int i;
  struct searchNode *search;
  int limit=500,matches=0;
  char *ch;
  int arg=0;

  if (cargc<1)
    return CMD_USAGE;
  
  if (*cargv[0] == '-') {
    /* options */
    arg++;
    
    for (ch=cargv[0]+1;*ch;ch++) {
      switch(*ch) {
      case 'l':
	if (cargc<arg) {
	  controlreply(sender,"Error: -l switch requires an argument");
	  return CMD_USAGE;
	}
	limit=strtoul(cargv[arg++],NULL,10);
	break;
	
      default:
	controlreply(sender,"Unrecognised flag -%c.",*ch);
      }
    }
  }

  if (arg>=cargc) {
    controlreply(sender,"No search terms - aborting.");
    return CMD_ERROR;
  }

  if (arg<(cargc-1)) {
    rejoinline(cargv[arg],cargc-arg);
  }
  
  if (!(search = search_parse(SEARCHTYPE_NICK, cargv[arg]))) {
    controlreply(sender,"Parse error: %s",parseError);
    return CMD_ERROR;
  }
  
  /* The top-level node needs to return a BOOL */
  search=coerceNode(search, RETURNTYPE_BOOL);
  
  for (i=0;i<NICKHASHSIZE;i++) {
    for (np=nicktable[i];np;np=np->next) {
      if ((search->exe)(search, np)) {
	if (matches<limit)
	  printnick(sender, np);
	if (matches==limit)
	  controlreply(sender, "--- More than %d matches, skipping the rest",limit);
	matches++;
      }
    }
  }

  (search->free)(search);

  controlreply(sender,"--- End of list: %d matches", matches);
  
  return CMD_OK;
}  

int do_chansearch(void *source, int cargc, char **cargv) {
  nick *sender = senderNSExtern = source;
  chanindex *cip;
  int i;
  struct searchNode *search;
  int limit=500,matches=0;
  char *ch;
  int arg=0;

  if (cargc<1)
    return CMD_USAGE;
  
  if (*cargv[0] == '-') {
    /* options */
    arg++;
    
    for (ch=cargv[0]+1;*ch;ch++) {
      switch(*ch) {
      case 'l':
	if (cargc<arg) {
	  controlreply(sender,"Error: -l switch requires an argument");
	  return CMD_USAGE;
	}
	limit=strtoul(cargv[arg++],NULL,10);
	break;
	
      default:
	controlreply(sender,"Unrecognised flag -%c.",*ch);
      }
    }
  }

  if (arg>=cargc) {
    controlreply(sender,"No search terms - aborting.");
    return CMD_ERROR;
  }

  if (arg<(cargc-1)) {
    rejoinline(cargv[arg],cargc-arg);
  }
  
  if (!(search = search_parse(SEARCHTYPE_CHANNEL, cargv[arg]))) {
    controlreply(sender,"Parse error: %s",parseError);
    return CMD_ERROR;
  }

  search=coerceNode(search, RETURNTYPE_BOOL);
  
  for (i=0;i<CHANNELHASHSIZE;i++) {
    for (cip=chantable[i];cip;cip=cip->next) {
      if ((search->exe)(search, cip)) {
	if (matches<limit)
	  printchannel(sender, cip);
	if (matches==limit)
	  controlreply(sender, "--- More than %d matches, skipping the rest",limit);
	matches++;
      }
    }
  }

  (search->free)(search);

  controlreply(sender,"--- End of list: %d matches", matches);

  return CMD_OK;
}

void *trueval(int type) {
  switch(type) {
  default:
  case RETURNTYPE_INT:
  case RETURNTYPE_BOOL:
    return (void *)1;
    
  case RETURNTYPE_STRING:
    return "1";
  }
}

void *falseval(int type) {
  switch (type) {
  default:
  case RETURNTYPE_INT:
  case RETURNTYPE_BOOL:
    return NULL;
    
  case RETURNTYPE_STRING:
    return "";
  }
}

struct coercedata {
  struct searchNode *child;
  union {
    char *stringbuf;
    unsigned long val;
  } u;
};

/* Free a coerce node */
void free_coerce(struct searchNode *thenode) {
  struct coercedata *cd=thenode->localdata;
  
  cd->child->free(cd->child);
  free(thenode->localdata);
  free(thenode);
}

/* Free a coerce node with a stringbuf allocated */
void free_coercestring(struct searchNode *thenode) {
  free(((struct coercedata *)thenode->localdata)->u.stringbuf);
  free_coerce(thenode);
}

/* exe_tostr_null: return the constant string */
void *exe_tostr_null(struct searchNode *thenode, void *theinput) {
  struct coercedata *cd=thenode->localdata;
  
  return cd->u.stringbuf;
}

/* exe_val_null: return the constant value */
void *exe_val_null(struct searchNode *thenode, void *theinput) {
  struct coercedata *cd=thenode->localdata;
  
  return (void *)cd->u.val;
}

/* Lots of very dull type conversion functions */
void *exe_inttostr(struct searchNode *thenode, void *theinput) {
  struct coercedata *cd=thenode->localdata;
  
  sprintf(cd->u.stringbuf, "%lu", (unsigned long)(cd->child->exe)(cd->child, theinput));
  
  return cd->u.stringbuf;
}

void *exe_booltostr(struct searchNode *thenode, void *theinput) {
  struct coercedata *cd=thenode->localdata;
  
  if ((cd->child->exe)(cd->child, theinput)) {
    sprintf(cd->u.stringbuf,"1");
  } else {
    cd->u.stringbuf[0]='\0';
  }
  
  return cd->u.stringbuf;
}

void *exe_strtoint(struct searchNode *thenode, void *theinput) {
  struct coercedata *cd=thenode->localdata;
  
  return (void *)strtoul((cd->child->exe)(cd->child,theinput),NULL,10);
}

void *exe_booltoint(struct searchNode *thenode, void *theinput) {
  struct coercedata *cd=thenode->localdata;
  
  /* Don't need to do anything */
  return (cd->child->exe)(cd->child, theinput); 
}

void *exe_strtobool(struct searchNode *thenode, void *theinput) {
  struct coercedata *cd=thenode->localdata;
  char *ch=(cd->child->exe)(cd->child, theinput);
  
  if (!ch || *ch=='\0' || (*ch=='0' && ch[1]=='\0')) {
    return (void *)0;
  } else { 
    return (void *)1;
  }
}

void *exe_inttobool(struct searchNode *thenode, void *theinput) {
  struct coercedata *cd=thenode->localdata;
  
  if ((cd->child->exe)(cd->child, theinput)) {
    return (void *)1;
  } else {
    return (void *)0;
  }
}

struct searchNode *coerceNode(struct searchNode *thenode, int type) {
  struct searchNode *anode;
  struct coercedata *cd;

  /* You can't coerce a NULL */
  if (!thenode)
    return NULL;
  
  /* No effort required to coerce to the same type */
  if (type==(thenode->returntype & RETURNTYPE_TYPE))
    return thenode;
  
  anode=(struct searchNode *)malloc(sizeof(struct searchNode));
  anode->localdata=cd=(struct coercedata *)malloc(sizeof(struct coercedata));
  cd->child=thenode;
  anode->returntype=type; /* We'll return what they want, always */
  anode->free=free_coerce;
  
  switch(type) {
    case RETURNTYPE_STRING:
      /* For a string we'll need a buffer */
      /* A 64-bit number prints out to 20 digits, this leaves some slack */
      cd->u.stringbuf=malloc(25); 
      anode->free=free_coercestring;
      
      switch(thenode->returntype & RETURNTYPE_TYPE) {
        default:
        case RETURNTYPE_INT:
          if (thenode->returntype & RETURNTYPE_CONST) {
            /* Constant node: sort it out now */
            sprintf(cd->u.stringbuf, "%lu", (unsigned long)thenode->exe(thenode, NULL));
            anode->exe=exe_tostr_null;
            anode->returntype |= RETURNTYPE_CONST;
          } else {
            /* Variable data */
            anode->exe=exe_inttostr;
          }
          break;
        
        case RETURNTYPE_BOOL:
          if (thenode->returntype & RETURNTYPE_CONST) {
            /* Constant bool value */
            if (thenode->exe(thenode,NULL)) {
              /* True! */
              sprintf(cd->u.stringbuf, "1");
            } else {
              cd->u.stringbuf[0] = '\0';
            }
            anode->exe=exe_tostr_null;
            anode->returntype |= RETURNTYPE_CONST;
          } else {
            /* Variable bool value */
            anode->exe=exe_booltostr;
          }            
          break;
      }
      break;
    
    case RETURNTYPE_INT:
      /* we want an int */
      switch (thenode->returntype & RETURNTYPE_TYPE) {
        case RETURNTYPE_STRING:
          if (thenode->returntype & RETURNTYPE_CONST) {
            cd->u.val=strtoul((thenode->exe)(thenode, NULL), NULL, 10);
            anode->exe=exe_val_null;
            anode->returntype |= RETURNTYPE_CONST;
          } else {
            anode->exe=exe_strtoint;
          }
          break;
        
        default:
        case RETURNTYPE_BOOL:
          if (thenode->returntype & RETURNTYPE_CONST) {
            if ((thenode->exe)(thenode,NULL))
              cd->u.val=1;
            else
              cd->u.val=0;
            
            anode->exe=exe_val_null;
            anode->returntype |= RETURNTYPE_CONST;
          } else {
            anode->exe=exe_booltoint;
          }
          break;
      }      
      break;
    
    default:
    case RETURNTYPE_BOOL:
      /* we want a bool */
      switch (thenode->returntype & RETURNTYPE_TYPE) {
        case RETURNTYPE_STRING:
          if (thenode->returntype & RETURNTYPE_CONST) {
            char *rv=(char *)((thenode->exe)(thenode, NULL));
            if (!rv || *rv=='\0' || (*rv=='0' && rv[1]=='\0'))
              cd->u.val=0;
            else
              cd->u.val=1;
            
            anode->exe=exe_val_null;
            anode->returntype |= RETURNTYPE_CONST;
          } else {
            anode->exe=exe_strtobool;
          }
          break;
        
        default:
        case RETURNTYPE_INT:
          if (thenode->returntype & RETURNTYPE_CONST) {
            if ((thenode->exe)(thenode,NULL))
              cd->u.val=1;
            else
              cd->u.val=0;
            
            anode->exe=exe_val_null;
            anode->returntype |= RETURNTYPE_CONST;
          } else {
            anode->exe=exe_inttobool;
          }
          break;
      }
      break;
  }
  
  return anode;
}

/* Literals always return constant strings... */
void *literal_exe(struct searchNode *thenode, void *theinput) {
  return ((sstring *)thenode->localdata)->content;
}

void literal_free(struct searchNode *thenode) {
  freesstring(thenode->localdata);
  free(thenode);
}

/* search_parse:
 *  Given an input string, return a searchNode.
 */

struct searchNode *search_parse(int type, char *input) {
  /* OK, we need to split the input into chunks on spaces and brackets.. */
  char *argvector[100];
  char thestring[500];
  int i,j,q=0,e=0;
  char *ch,*ch2;
  struct Command *cmd;
  struct searchNode *thenode;

  /* If it starts with a bracket, it's a function call.. */
  if (*input=='(') {
    /* Skip past string */
    for (ch=input;*ch;ch++);
    if (*(ch-1) != ')') {
      parseError = "Bracket mismatch!";
      return NULL;
    }
    input++;
    *(ch-1)='\0';

    /* Split further args */
    i=-1; /* i = -1 BoW, 0 = inword, 1 = bracket nest depth */
    j=0;  /* j = current arg */
    e=0;
    q=0;
    argvector[0]="";
    for (ch=input;*ch;ch++) {
      if (i==-1) {
        argvector[j]=ch;
        if (*ch=='(') {
          i=1;
        } else if (*ch != ' ') {
          i=0;
          if (*ch=='\\') {
            e=1;
          } else if (*ch=='\"') {
            q=1;
          }
        }
      } else if (e==1) {
        e=0;
      } else if (q==1) {
        if (*ch=='\"')	
        q=0;
      } else if (i==0) {
        if (*ch=='\\') {
          e=1;
        } else if (*ch=='\"') {
          q=1;
        } else if (*ch==' ') {
          *ch='\0';
          j++;
          if(j >= (sizeof(argvector) / sizeof(*argvector))) {
            parseError = "Too many arguments";
            return NULL;
          }
          i=-1;
        }
      } else {
        if (*ch=='\\') {
          e=1;
        } else if (*ch=='\"') {
          q=1;
        } else if (*ch=='(') {
          i++;
        } else if (*ch==')') {
          i--;
        }
      }
    }
    
    if (i>0) {
      parseError = "Bracket mismatch!";
      return NULL;
    }

    if (*(ch-1) == 0) /* if the last character was a space */
      j--; /* remove an argument */
    
    if (!(cmd=findcommandintree(searchTree,argvector[0],1))) {
      parseError = "Unknown command";
      return NULL;
    } else {
      return ((parseFunc)cmd->handler)(type, j, argvector+1);
    }
  } else {
    /* Literal */
    if (*input=='\"') {
      for (ch=input;*ch;ch++);
      
      if (*(ch-1) != '\"') {
        parseError="Quote mismatch";
        return NULL;
      }

      *(ch-1)='\0';
      input++;
    }
    
    ch2=thestring;
    for (ch=input;*ch;ch++) {
      if (e) {
        e=0;
        *ch2++=*ch;
      } else if (*ch=='\\') {
        e=1;
      } else {
        *ch2++=*ch;
      }
    }
    *ch2='\0';
        
    if (!(thenode=(struct searchNode *)malloc(sizeof(struct searchNode)))) {
      parseError = "malloc: could not allocate memory for this search.";
      return NULL;
    }

    thenode->localdata  = getsstring(thestring,512);
    thenode->returntype = RETURNTYPE_CONST | RETURNTYPE_STRING;
    thenode->exe        = literal_exe;
    thenode->free       = literal_free;

    return thenode;
  }    
}
