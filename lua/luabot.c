/* Copyright (C) Chris Porter 2005 */
/* ALL RIGHTS RESERVED. */
/* Don't put this into the SVN repo. */

#include "../localuser/localuser.h"
#include "../localuser/localuserchannel.h"
#include "../core/schedule.h"
#include "../lib/irc_string.h"

#include "lua.h"
#include "luabot.h"

#include <stdarg.h>

nick *lua_nick = NULL;
void *myureconnect = NULL, *myublip = NULL, *myutick = NULL;

void lua_bothandler(nick *target, int type, void **args);

void lua_onnewnick(int hooknum, void *arg);
void lua_blip(void *arg);
void lua_tick(void *arg);
void lua_onkick(int hooknum, void *arg);
void lua_ontopic(int hooknum, void *arg);
void lua_onauth(int hooknum, void *arg);
void lua_ondisconnect(int hooknum, void *arg);
void lua_onmode(int hooknum, void *arg);
void lua_onop(int hooknum, void *arg);
void lua_onquit(int hooknum, void *arg);
void lua_onrename(int hooknum, void *arg);
void lua_onconnect(int hooknum, void *arg);
void lua_onjoin(int hooknum, void *arg);
void lua_onpart(int hooknum, void *arg);

void lua_registerevents(void) {
  registerhook(HOOK_NICK_NEWNICK, &lua_onnewnick);
  registerhook(HOOK_IRC_DISCON, &lua_ondisconnect);
  registerhook(HOOK_IRC_PRE_DISCON, &lua_ondisconnect);
  registerhook(HOOK_NICK_ACCOUNT, &lua_onauth);
  registerhook(HOOK_CHANNEL_TOPIC, &lua_ontopic);
  registerhook(HOOK_CHANNEL_KICK, &lua_onkick);
  registerhook(HOOK_CHANNEL_OPPED, &lua_onop);
  registerhook(HOOK_CHANNEL_DEOPPED, &lua_onop);
  registerhook(HOOK_NICK_LOSTNICK, &lua_onquit);
  registerhook(HOOK_NICK_RENAME, &lua_onrename);
  registerhook(HOOK_IRC_CONNECTED, &lua_onconnect);
  registerhook(HOOK_SERVER_END_OF_BURST, &lua_onconnect);
  registerhook(HOOK_CHANNEL_JOIN, &lua_onjoin);
  registerhook(HOOK_CHANNEL_PART, &lua_onpart);
  registerhook(HOOK_CHANNEL_CREATE, &lua_onjoin);
}

void lua_deregisterevents(void) {
  deregisterhook(HOOK_CHANNEL_PART, &lua_onpart);
  deregisterhook(HOOK_CHANNEL_CREATE, &lua_onjoin);
  deregisterhook(HOOK_CHANNEL_JOIN, &lua_onjoin);
  deregisterhook(HOOK_SERVER_END_OF_BURST, &lua_onconnect);
  deregisterhook(HOOK_IRC_CONNECTED, &lua_onconnect);
  deregisterhook(HOOK_NICK_RENAME, &lua_onrename);
  deregisterhook(HOOK_NICK_LOSTNICK, &lua_onquit);
  deregisterhook(HOOK_CHANNEL_DEOPPED, &lua_onop);
  deregisterhook(HOOK_CHANNEL_OPPED, &lua_onop);
  deregisterhook(HOOK_CHANNEL_KICK, &lua_onkick);
  deregisterhook(HOOK_CHANNEL_TOPIC, &lua_ontopic);
  deregisterhook(HOOK_NICK_ACCOUNT, &lua_onauth);
  deregisterhook(HOOK_IRC_PRE_DISCON, &lua_ondisconnect);
  deregisterhook(HOOK_IRC_DISCON, &lua_ondisconnect);
  deregisterhook(HOOK_NICK_NEWNICK, &lua_onnewnick);
}

void lua_startbot(void *arg) {
  channel *cp;

  myureconnect = NULL;

  lua_nick = registerlocaluser("U", "lua", "quakenet.department.of.corrections", LUA_FULLVERSION, "U", UMODE_ACCOUNT | UMODE_DEAF | UMODE_OPER | UMODE_SERVICE, &lua_bothandler);
  if(!lua_nick) {
    myureconnect = scheduleoneshot(time(NULL) + 1, &lua_startbot, NULL);
    return;
  }

  cp = findchannel(LUA_OPERCHAN);
  if(cp && localjoinchannel(lua_nick, cp)) {
    localgetops(lua_nick, cp);
  } else {
    localcreatechannel(lua_nick, LUA_OPERCHAN);
  }

  cp = findchannel(LUA_PUKECHAN);
  if(cp && localjoinchannel(lua_nick, cp)) {
    localgetops(lua_nick, cp);
  } else {
    localcreatechannel(lua_nick, LUA_PUKECHAN);
  }

  myublip = schedulerecurring(time(NULL) + 1, 0, 60, &lua_blip, NULL);
  myutick = schedulerecurring(time(NULL) + 1, 0, 1, &lua_tick, NULL);

  lua_registerevents();
}

void lua_destroybot(void) {
  if(myutick)
    deleteschedule(myutick, &lua_tick, NULL);

  if(myublip)
    deleteschedule(myublip, &lua_blip, NULL);

  lua_deregisterevents();

  if (myureconnect)
    deleteschedule(myureconnect, &lua_startbot, NULL);

  if(lua_nick)
    deregisterlocaluser(lua_nick, NULL);
}

int _lua_vpcall(lua_State *l, void *function, int mode, const char *sig, ...) {
  va_list va;
  int narg = 0, nres, top = lua_gettop(l);

  lua_getglobal(l, "scripterror");
  if(mode == LUA_CHARMODE) {
    lua_getglobal(l, (const char *)function);
  } else {
    lua_rawgeti(l, LUA_REGISTRYINDEX, (int)function);
  }

  if(!lua_isfunction(l, -1)) {
    lua_settop(l, top);
    return 1;
  }

  va_start(va, sig);

  while(*sig) {
    switch(*sig++) {
      case 'i':
        lua_pushint(l, va_arg(va, int));
        break;
      case 'l':
        lua_pushlong(l, va_arg(va, long));
        break;
      case 's':
        lua_pushstring(l, va_arg(va, char *));
        break;
      case 'S':
        lua_pushstring(l, ((sstring *)(va_arg(va, sstring *)))->content);
        break;
      case 'N':
        {
          nick *np = va_arg(va, nick *);
          LUA_PUSHNICK(l, np);
          break;
        }
      case 'C':
        {
          channel *cp = va_arg(va, channel *);
          LUA_PUSHCHAN(l, cp);
          break;
        }
      case '0':
        lua_pushnil(l);
        break;
      case '>':
        goto endwhile;

      default:
        Error("lua", ERR_ERROR, "Unable to parse vpcall signature (%c)", *(sig - 1));
    }
    narg++;
  }

endwhile:

  nres = strlen(sig);

  if(lua_debugpcall(l, (mode==LUA_CHARMODE)?function:"some_handler", narg, nres, top + 1)) {
    Error("lua", ERR_ERROR, "Error pcalling %s: %s.", (mode==LUA_CHARMODE)?function:"some_handler", lua_tostring(l, -1));
  } else {
    nres = -nres;
    while(*sig) {
      switch(*sig++) {
        default:
          Error("lua", ERR_ERROR, "Unable to parse vpcall return structure (%c)", *(sig - 1));
      }
      nres++;
    }
  }

  lua_settop(l, top);
  va_end(va);
  return 0;
}

void lua_bothandler(nick *target, int type, void **args) {
  nick *np;
  char *p;
  int le;

  switch(type) {
    case LU_PRIVMSG:
      np = (nick *)args[0];
      p = (char *)args[1];

      if(!np || !p)
        return;

      lua_avpcall("irc_onmsg", "Ns", np, p);

      break;
    case LU_PRIVNOTICE:
      np = (nick *)args[0];
      p = (char *)args[1];

      if(!np || !p)
        return;

      le = strlen(p);
      if((le > 1) && (p[0] == '\001')) {
        if(p[le - 1] == '\001')
          p[le - 1] = '\000';

        lua_avpcall("irc_onctcp", "Ns", np, p + 1);

      }

      break;

    case LU_KILLED:
      if(myublip) {
        myublip = NULL;
        deleteschedule(myublip, &lua_blip, NULL);
      }

      if(myutick) {
        myutick = NULL;
        deleteschedule(myutick, &lua_tick, NULL);
      }

      lua_nick = NULL;
      myureconnect = scheduleoneshot(time(NULL) + 1, &lua_startbot, NULL);

      break;
  }
}

void lua_blip(void *arg) {
  lua_avpcall("onblip", "");
}

void lua_tick(void *arg) {
  lua_avpcall("ontick", "");
}

void lua_onnewnick(int hooknum, void *arg) {
  nick *np = (nick *)arg;

  if(!np)
    return;

  lua_avpcall("irc_onnewnick", "N", np);
}

void lua_onkick(int hooknum, void *arg) {
  void **arglist = (void **)arg;
  chanindex *ci = ((channel *)arglist[0])->index;
  nick *kicked = arglist[1];
  nick *kicker = arglist[2];
  char *message = (char *)arglist[3];

  if(!kicker || IsOper(kicker) || IsService(kicker) || IsXOper(kicker)) /* bloody Cruicky */
    return;

  lua_avpcall("irc_onkick", "SNNs", ci->name, kicked, kicker, message);
}

void lua_ontopic(int hooknum, void *arg) {
  void **arglist = (void **)arg;
  channel *cp=(channel*)arglist[0];
  nick *np = (nick *)arglist[1];

  if(!np || IsOper(np) || IsService(np) || IsXOper(np))
    return;
  if(!cp || !cp->topic) 
    return;

  lua_avpcall("irc_ontopic", "SNS", cp->index->name, np, cp->topic);
}

void lua_onop(int hooknum, void *arg) {
  void **arglist = (void **)arg;
  chanindex *ci = ((channel *)arglist[0])->index;
  nick *np = arglist[1];
  nick *target = arglist[2];

  if(!target)
    return;

  if(np) {
    lua_avpcall(hooknum == HOOK_CHANNEL_OPPED?"irc_onop":"irc_ondeop", "SNN", ci->name, np, target);
  } else {
    lua_avpcall(hooknum == HOOK_CHANNEL_OPPED?"irc_onop":"irc_ondeop", "S0N", ci->name, target);
  }
}

void lua_onjoin(int hooknum, void *arg) {
  void **arglist = (void **)arg;
  chanindex *ci = ((channel *)arglist[0])->index;
  nick *np = arglist[1];

  if(!ci || !np)
    return;

  lua_avpcall("irc_onjoin", "SN", ci->name, np);
}

void lua_onpart(int hooknum, void *arg) {
  void **arglist = (void **)arg;
  chanindex *ci = ((channel *)arglist[0])->index;
  nick *np = arglist[1];

  if(!ci || !np)
    return;

  lua_avpcall("irc_onpart", "SN", ci->name, np);
}

void lua_onrename(int hooknum, void *arg) {
  nick *np = (void *)arg;

  if(!np)
    return;

  lua_avpcall("irc_onrename", "N", np);
}

void lua_onquit(int hooknum, void *arg) {
  nick *np = (nick *)arg;

  if(!np)
    return;

  lua_avpcall("irc_onquit", "N", np);
}

void lua_onauth(int hooknum, void *arg) {
  nick *np = (nick *)arg;

  if(!np)
    return;

  lua_avpcall("irc_onauth", "N", np);
}

void lua_ondisconnect(int hooknum, void *arg) {
  lua_avpcall(hooknum == HOOK_IRC_DISCON?"irc_ondisconnect":"irc_onpredisconnect", "");
}

void lua_onconnect(int hooknum, void *arg) {
  lua_avpcall(hooknum == HOOK_IRC_CONNECTED?"irc_onconnect":"irc_onendofburst", "");
}

void lua_onload(lua_State *l) {
  lua_vpcall(l, "onload", "");
}

void lua_onunload(lua_State *l) {
  lua_vpcall(l, "onunload", "");
}

int lua_channelmessage(channel *cp, char *message, ...) {
  char buf[512];
  va_list va;

  va_start(va, message);
  vsnprintf(buf, sizeof(buf), message, va);
  va_end(va);

  sendmessagetochannel(lua_nick, cp, "%s", buf);

  return 0;
}

int lua_message(nick *np, char *message, ...) {
  char buf[512];
  va_list va;

  va_start(va, message);
  vsnprintf(buf, sizeof(buf), message, va);
  va_end(va);

  sendmessagetouser(lua_nick, np, "%s", buf);

  return 0;
}

int lua_notice(nick *np, char *message, ...) {
  char buf[512];
  va_list va;

  va_start(va, message);
  vsnprintf(buf, sizeof(buf), message, va);
  va_end(va);

  sendnoticetouser(lua_nick, np, "%s", buf);

  return 0;
}
