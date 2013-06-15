#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../control/control.h"
#include "../nick/nick.h"
#include "../localuser/localuserchannel.h"
#include "../core/hooks.h"
#include "../server/server.h"
#include "../parser/parser.h"
#include "../core/schedule.h"
#include "../lib/array.h"
#include "../lib/base64.h"
#include "../lib/irc_string.h"
#include "../lib/splitline.h"
#include "../core/nsmalloc.h"

#include "gline.h"
#include "../lib/version.h"

MODULE_VERSION("1.0")

gline* glinelist = 0, *glinelistnonnode = 0;

int glinecount = 0;
int badchancount = 0;
int rnglinecount = 0;
int gl_nodeext = -1;

/* 
  <prefix> GL <target> [!][+|-|>|<]<mask> [<expiration>] [<lastmod>]
    [<lifetime>] [:<reason>]

 */

void _init() {
  gl_nodeext = registernodeext("gline");

  if ( gl_nodeext == -1 ) {
    Error("gline", ERR_FATAL, "Could not register a required node extension (gline)");
    return;
  }

  registerserverhandler("GL", &handleglinemsg, 6);

  /* send reburst command to get glines from before we registered handler */
  irc_send("%s RB G", mynumeric->content);
}

void _fini() {
  gline *gl, *ngl;

  deregisterserverhandler("GL", &handleglinemsg);

  for (gl=glinelist;gl;gl=ngl) { 
    ngl=gl->next;
    freegline(gl);    
  }
  glinelist = NULL;
 
  if (gl_nodeext != -1) 
    releasenodeext(gl_nodeext);

  nsfreeall(POOL_GLINE);
}

int gline_setnick(nick *np, char *creator, int duration, char *reason, int forceidentonly ) {
  gline *gl;
  char mask[NICKLEN+USERLEN+HOSTLEN+5];
  int expires = (intmax_t)getnettime() + duration;
  sprintf(mask, "*!%s@%s", np->ident, IPtostr(np->p_ipaddr));
  // TODO forceidentonly

  sstring *sscreator = getsstring(creator, HOSTLEN);
  gl = gline_add( mylongnum, sscreator, mask, reason, expires, (intmax_t)getnettime(), expires); // lifetime=expires

  if (!gl) {
    Error("gline", ERR_ERROR, "Could not set gline on nick ptr");
  }

  gline_propagate(gl);
}

int gline_setnode(patricia_node_t *node ) {

}

int gline_setmask(char *mask, int duration, char *reason ) {

}

gline* gline_add(long creatornum, sstring *creator, char *mask, char *reason, time_t expires, time_t lastmod, time_t lifetime) {
  gline* gl;
  char glineiddata[1024];

  if ( !(gl=gline_processmask(mask))) { /* sets up nick,user,host,node and flags */
    /* couldn't process gline mask */
    Error("gline", ERR_WARNING, "Tried to add malformed G-Line %s!", mask);
    return 0;
  }

  gl->creator = creator;
  gl->numeric = creatornum;

  /* it's not unreasonable to assume gline is active, if we're adding a deactivated gline, we can remove this later */
  gl->flags |= GLINE_ACTIVE;


  /* set up gline id */
  snprintf(glineiddata, sizeof(glineiddata), "gline %s %s", mask, reason);
  gl->glineid = crc32(glineiddata);  

  gl->reason = getsstring(reason, 255); /*TODO@@@ */ 
  gl->expires = expires;
  gl->lastmod = lastmod;
  gl->lifetime = lifetime; 

  /* Storage of glines */
  /* ipbased glines are stored at node->ext[gl_nodeext]   
   * other glines are stored in seperate linked list (for now) */
  if (gl->flags & GLINE_IPMASK) {
    gl->nextbynode = gl->node->exts[gl_nodeext];
    gl->node->exts[gl_nodeext] = gl; 
  } else {
    gl->nextbynonnode = glinelistnonnode;
    glinelistnonnode = gl;
  }  

  gl->next = glinelist;
  glinelist = gl;

  return gl;
}

/* returns 1 on success, 0 on a bad mask */
gline* gline_processmask(char *mask) {
  /* populate gl-> user,host,node,nick and set appropriate flags */
  int len;
  int foundat=-1,foundbang=-1;
  int foundwild=0;
  int i;
  struct irc_in_addr sin;
  unsigned char bits;
  gline *gl = NULL;

  Error("gline", ERR_FATAL,"processing: %s", mask);
  if (!(gl = newgline())) {
    Error("gline", ERR_ERROR, "Failed to allocate new gline");
    return 0;
  }

  len=strlen(mask);

  switch (*mask ) {
    case '#':
    case '&':
      gl->flags |= GLINE_BADCHAN;
      gl->user = getsstring(mask, CHANNELLEN);
      return gl;
    case '$':
      switch (mask[1]) {
        case 'R':
          gl->flags |= GLINE_REALNAME;
          break;
        default: 
          Error("gline", ERR_WARNING, "Tried to add malformed G-Line %s!", mask);
          assert(0);
          return 0;
      }
      gl->user = getsstring(mask,REALLEN);
      return gl;
    default:
      /* Default case of some host/ip/cidr mask */
      for (i=(len-1);i>=0;i--) {
        if (mask[i]=='@') {
          /* host */
          if ((len-i)-1 > HOSTLEN) {
            /* host too long */
            /* i'm going to assume it's not a CIDR */
            gl->host=getsstring(&mask[i+1],(len-i)-1);
            if (foundwild) {
              gl->flags |= GLINE_HOSTMASK;
            } else {
              gl->flags |= GLINE_HOSTEXACT;
            }
            gl->flags |= GLINE_BADMASK;
          } else if (i==(len-1)) {
            /* no host supplied aka gline ends @ */
            gl->flags |= GLINE_BADMASK;
            gl->flags |= GLINE_HOSTNULL;
          } else if (i==(len-2) && mask[i+1]=='*') {
            /* Special case: "@*" */
            gl->flags |= GLINE_HOSTANY;
            gl->host=NULL;
          } else {
            Error("gline", ERR_WARNING, "Hostpart: %s", &mask[i+1]);
            if (ipmask_parse(&mask[i+1], &sin, &bits) == 0) {
              /* we have some host string */
              gl->host=getsstring(&mask[i+1],HOSTLEN);
              if (foundwild) {
                gl->flags |= GLINE_HOSTMASK;
              } else {
                gl->flags |= GLINE_HOSTEXACT;
              } 
            } else {
              /* we have a / so cidr gline */
              Error("gline", ERR_WARNING, "CIDR: %s", &mask[i+1]);
              gl->node = refnode(iptree, &sin, bits);
              if (foundwild || !is_normalized_ipmask(&sin, bits)) {
                gl->host=getsstring(&mask[i+1],HOSTLEN);
                gl->flags |= GLINE_IPWILD;
              }
              gl->flags |= GLINE_IPMASK;
            }
          }
          foundat=i;
          break;
        } else if (mask[i]=='?' || mask[i]=='*') {
          if (!foundwild)  /* Mark last wildcard in string */
            foundwild=i;
        }
      }
  }
  /*TODO set hostexact/hostmask */
  if (foundat<0) {
    /* If there wasn't an @, this ban matches any host */
    gl->host=NULL;
    gl->flags |= GLINE_HOSTANY;
  }

  foundwild=0;

  for (i=0;i<foundat;i++) {
    if (mask[i]=='!') {
      if (i==0) {
        /* Invalid mask: nick is empty */
        gl->flags |= GLINE_NICKNULL;
        gl->nick=NULL;
      } else if (i==1 && mask[0]=='*') {
        /* matches any nick */
        gl->flags |= GLINE_NICKANY;
        gl->nick=NULL;
      } else {
        if (i>NICKLEN) {
          /* too long: just take the first NICKLEN chars */
          Error("gline", ERR_WARNING, "Bad Nick Length for gline: %s", mask);
        }
        gl->nick=getsstring(mask,i);
        if (foundwild)
          gl->flags |= GLINE_NICKMASK;
        else
          gl->flags |= GLINE_NICKEXACT;
      }
      foundbang=i;
      break;
    } else if (mask[i]=='?' || mask[i]=='*') {
      if (i<NICKLEN) {
        foundwild=1;
      }
    }
  }

  if (foundbang<0) {
    /* We didn't find a !, what we do now depends on what happened
     * with the @ */
    if (foundat<0) {
      /* A gline with no ! or @ is treated as a host gline only */
      /* Note that we've special-cased "*" at the top, so we can only
       * hit the MASK or EXACT case here. */
      Error("gline", ERR_WARNING, "no ! or @");

      gl->host=getsstring(mask,len);

      if (foundwild)
        gl->flags |= GLINE_HOSTMASK;
      else
        gl->flags |= GLINE_HOSTEXACT;

      gl->flags |= (GLINE_USERANY | GLINE_NICKANY);
      gl->nick=NULL;
      gl->user=NULL;
    } else {
      /* A gline with @ only is treated as user@host */
      gl->nick=NULL;
      gl->flags |= GLINE_NICKANY;
    }
  }

  if (foundat>=0) {
    /* We found an @, so everything between foundbang+1 and foundat-1 is supposed to be ident */
    /* This is true even if there was no !.. */
    if (foundat==(foundbang+1)) {
      /* empty ident matches nothing */ /*@@@TODO: * for glines? */
      gl->flags |= (GLINE_BADMASK | GLINE_USERNULL);
      gl->user=NULL;
    } else if (foundat - foundbang - 1 > USERLEN) {
      Error("gline", ERR_ERROR, "USERLEN part of mask too long"); 
      /* It's too long.. */
      gl->user=getsstring(&mask[foundbang+1],(foundat-foundbang-1));
      if (strchr(gl->user->content,'*') || strchr(gl->user->content,'?'))
        gl->flags |= GLINE_USERMASK;
      else
        gl->flags |= GLINE_USEREXACT;
      //return 0;
    } else if ((foundat - foundbang - 1 == 1) && mask[foundbang+1]=='*') {
      gl->user=NULL;
      gl->flags |= GLINE_USERANY;
    } else {
      gl->user=getsstring(&mask[foundbang+1],(foundat-foundbang-1));
      if (strchr(gl->user->content,'*') || strchr(gl->user->content,'?'))
        gl->flags |= GLINE_USERMASK;
      else
        gl->flags |= GLINE_USEREXACT;
    }
    /* Username part can't contain an @ */
    if (gl->user && strchr(gl->user->content,'@')) {
      //gl->flags |= CHANBAN_INVALID;
    }
  }

  assert(gl->flags & (GLINE_USEREXACT | GLINE_USERMASK | GLINE_USERANY | GLINE_USERNULL));
  assert(gl->flags & (GLINE_NICKEXACT | GLINE_NICKMASK | GLINE_NICKANY | GLINE_NICKNULL));
  assert(gl->flags & (GLINE_HOSTEXACT | GLINE_HOSTMASK | GLINE_HOSTANY | GLINE_HOSTNULL | GLINE_IPMASK));

  if (gl->flags & GLINE_BADMASK) {
    Error("gline", ERR_WARNING, "BADMASK: %s", mask);
  }

  if ( strcmp( glinetostring(gl), mask ) ) {
    // oper can specify * to glist, ircd by default converts * to *!*@*
    if ( ((gl->flags & ( GLINE_USERANY | GLINE_NICKANY | GLINE_HOSTANY )) == ( GLINE_USERANY | GLINE_NICKANY | GLINE_HOSTANY )) && !strcmp("*",mask)){
      return gl;
    }
    // oper can specifiy *@host, ircd by default converst *@host to *!*@host
    if ( ((gl->flags & ( GLINE_NICKANY )) == ( GLINE_NICKANY )) && (mask[1]!='!') ) {
      Error("gline", ERR_WARNING, "different: %s %s", glinetostring(gl), mask);
      return gl;
    }    

    Error("gline", ERR_WARNING, "DIFFERENT: %s %s", glinetostring(gl), mask); 
    assert(0);
  }
  return gl;
}

gline *gline_find( char *mask) {
  gline *gl, *sgl;
  gline *globalgline;
  time_t curtime = time(0);

  if( !(globalgline=gline_processmask(mask))) {
    /* gline mask couldn't be processed */
    return 0;
  }

  if (globalgline->flags & GLINE_IPMASK) {
    for (gl = globalgline->node->exts[gl_nodeext]; gl; gl = sgl) {
      sgl = gl->nextbynode;

      if (gl->lifetime <= curtime) {
        removegline(gl);
        continue;
      } else if (gl->expires <= curtime) {
        gl->flags &= ~GLINE_ACTIVE;
      }

      if ( gline_match( globalgline, gl) ) {
        freegline(globalgline);
        return gl;
      }
    }
  } else {
    for (gl = glinelist; gl; gl = sgl) {
      sgl = gl->nextbynonnode;

      if (gl->lifetime <= curtime) {
        removegline(gl);
        continue;
      } else if (gl->expires <= curtime) {
        gl->flags &= ~GLINE_ACTIVE;
      }

      if ( gline_match( globalgline, gl ) ) {
        freegline(globalgline);
        return gl;
      }
    }
  }
  freegline(globalgline);
  return 0;
}

/* returns non-zero on match */
int gline_match ( gline *gla, gline *glb) {
  if ((!gla->nick && glb->nick) || (gla->nick && !glb->nick))
    return 0;

  if (gla->nick && ircd_strcmp(gla->nick->content,glb->nick->content))
    return 0;

  if ((!gla->user && glb->user) || (gla->user && !glb->user))
    return 0;

  if (gla->user && ircd_strcmp(gla->user->content,glb->user->content))
    return 0;

  if ((!gla->host && glb->host) || (gla->host && !glb->host))
    return 0;

  if (gla->host && ircd_strcmp(gla->host->content,glb->host->content))
    return 0;
  
  /* TODO @@@ match bits flags */
  return 1;
}

/* returns non-zero on match */
int gline_match_mask ( gline *gla, gline *glb) {
  if ((!gla->nick && glb->nick) || (gla->nick && !glb->nick))
    return 0;

  if (gla->nick && match(gla->nick->content,glb->nick->content))
    return 0;

  // TODO should check for ANY etc
  if ((!gla->user && glb->user) || (gla->user && !glb->user))
    return 0;

  if (gla->user && match(gla->user->content,glb->user->content))
    return 0;

  if ((!gla->host && glb->host) || (gla->host && !glb->host))
    return 0;

  if (gla->host && match(gla->host->content,glb->host->content))
    return 0;

  /* TODO @@@ match bits flags */
  return 1;
}

void gline_send(gline *gl) {

}

char *glinetostring(gline *g) {
  static char outstring[NICKLEN+USERLEN+HOSTLEN+5]; // check
  int strpos=0;

  if ( g->flags & GLINE_REALNAME ) {
   if (g->user)
     strpos += sprintf(outstring+strpos, "%s", g->user->content);
   // @@ todo $R* 
   // add $R (not check badchans need same?)
   return outstring;
  }

  if ( g->flags & GLINE_BADCHAN ) {
    if (g->user)
      strpos += sprintf(outstring+strpos, "%s", g->user->content);
    return outstring;
  }

  if ( g->flags & GLINE_NICKANY ) {
    strpos += sprintf(outstring+strpos,"*");
  } else if (g->nick) {
    strpos += sprintf(outstring+strpos,"%s", g->nick->content);
  }

  strpos += sprintf(outstring+strpos, "!");

  if ( g->flags & GLINE_USERANY ) {
    strpos += sprintf(outstring+strpos, "*");
  } else if (g->user) {
    strpos += sprintf(outstring+strpos, "%s", g->user->content); 
  }

  strpos += sprintf(outstring+strpos, "@");

  if ( g->flags & GLINE_HOSTANY ) {
    strpos += sprintf(outstring+strpos, "*");
  } else if (g->host) {
    strpos += sprintf(outstring+strpos, "%s", g->host->content);
  } else if ( g->flags & GLINE_IPMASK ) {
    if ( g->flags & GLINE_IPWILD ) {
      strpos += sprintf(outstring+strpos, "%s", g->host->content);
    } else { 
      /* TODO bits == 128 */
      if (g->node->prefix->bitlen == 128)
        strpos += sprintf(outstring+strpos, "%s", IPtostr(g->node->prefix->sin));
      else
        strpos += sprintf(outstring+strpos, "%s/%d", IPtostr(g->node->prefix->sin), irc_bitlen(&(g->node->prefix->sin),(g->node->prefix->bitlen)));
    }
  }
  return outstring;
}

int gline_deactivate(gline *agline, time_t lastmod, int propagate) {
  time_t now = getnettime();
  agline->flags &= ~GLINE_ACTIVE;

  if (lastmod) {
    agline->lastmod = lastmod;
  } else {
    if(now<=agline->lastmod)
      agline->lastmod++;
    else
      agline->lastmod=now;
  }

  if ( propagate) {
    gline_propagate(agline);
  }
}

int gline_propagate(gline *agline) {
  if ( agline->flags & GLINE_ACTIVE ) {
    irc_send("%s GL * +%s %lu %lu :%s\r\n", mynumeric->content, glinetostring(agline), agline->expires, agline->lastmod, agline->reason->content);
  } else {
    irc_send("%s GL * -%s %lu %lu\r\n", mynumeric->content, glinetostring(agline), agline->expires, agline->lastmod);
  }
}