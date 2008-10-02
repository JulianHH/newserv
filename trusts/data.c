#include <stdlib.h>
#include <string.h>

#include "../lib/sstring.h"
#include "../core/hooks.h"
#include "../core/nsmalloc.h"
#include "../lib/irc_string.h"
#include "trusts.h"

trustgroup *tglist;

void th_dbupdatecounts(trusthost *);
void tg_dbupdatecounts(trustgroup *);

void trusts_freeall(void) {
  trustgroup *tg, *ntg;
  trusthost *th, *nth;

  for(tg=tglist;tg;tg=ntg) {
    ntg = tg->next;
    for(th=tg->hosts;th;th=nth) {
      nth = th->next;

      th_free(th);
    }

    tg_free(tg);
  }

  tglist = NULL;
}

trustgroup *tg_getbyid(unsigned int id) {
  trustgroup *tg;

  for(tg=tglist;tg;tg=tg->next)
    if(tg->id == id)
      return tg;

  return NULL;
}

void th_free(trusthost *th) {
  nsfree(POOL_TRUSTS, th);
}

int th_add(trustgroup *tg, unsigned int id, char *host, unsigned int maxusage, time_t lastseen) {
  u_int32_t ip, mask;
  trusthost *th;

  if(!trusts_str2cidr(host, &ip, &mask))
    return 0;

  th = nsmalloc(POOL_TRUSTS, sizeof(trusthost));
  if(!th)
    return 0;

  th->id = id;
  th->maxusage = maxusage;
  th->lastseen = lastseen;
  th->ip = ip;
  th->mask = mask;

  th->users = NULL;
  th->group = tg;
  th->count = 0;

  th->next = tg->hosts;
  tg->hosts = th;

  return 1;
}

void tg_free(trustgroup *tg) {
  triggerhook(HOOK_TRUSTS_LOSTGROUP, tg);

  freesstring(tg->name);
  freesstring(tg->createdby);
  freesstring(tg->contact);
  freesstring(tg->comment);
  nsfree(POOL_TRUSTS, tg);
}

int tg_add(unsigned int id, char *name, unsigned int trustedfor, int mode, unsigned int maxperident, unsigned int maxusage, time_t expires, time_t lastseen, time_t lastmaxuserreset, char *createdby, char *contact, char *comment) {
  trustgroup *tg = nsmalloc(POOL_TRUSTS, sizeof(trustgroup));
  if(!tg)
    return 0;

  tg->name = getsstring(name, TRUSTNAMELEN);
  tg->createdby = getsstring(createdby, NICKLEN);
  tg->contact = getsstring(contact, CONTACTLEN);
  tg->comment = getsstring(comment, COMMENTLEN);
  if(!tg->name || !tg->createdby || !tg->contact || !tg->comment) {
    tg_free(tg);
    return 0;
  }

  tg->id = id;
  tg->trustedfor = trustedfor;
  tg->mode = mode;
  tg->maxperident = maxperident;
  tg->maxusage = maxusage;
  tg->expires = expires;
  tg->lastseen = lastseen;
  tg->lastmaxuserreset = lastmaxuserreset;
  tg->hosts = NULL;

  tg->count = 0;

  memset(tg->exts, 0, sizeof(tg->exts));

  tg->next = tglist;
  tglist = tg;

  triggerhook(HOOK_TRUSTS_NEWGROUP, tg);

  return 1;
}

trusthost *th_getbyhost(uint32_t host) {
  trustgroup *tg;
  trusthost *th, *result = NULL;
  uint32_t mask;

  for(tg=tglist;tg;tg=tg->next) {
    for(th=tg->hosts;th;th=th->next) {
      if((host & th->mask) == th->ip) {
        if(!result || (th->mask > mask)) {
          mask = th->mask;
          result = th;
        }
      }
    }
  }

  return result;
}

void trusts_flush(void) {
  trustgroup *tg;
  trusthost *th;
  time_t t = time(NULL);

  for(tg=tglist;tg;tg=tg->next) {
    if(tg->count > 0)
      tg->lastseen = t;

    tg_dbupdatecounts(tg);

    for(th=tg->hosts;th;th=th->next) {
      if(th->count > 0)
        th->lastseen = t;

      th_dbupdatecounts(th);
    }
  }
}

trustgroup *tg_strtotg(char *name) {
  unsigned long id;
  trustgroup *tg;

  /* legacy format */
  if(name[0] == '#') {
    id = strtoul(&name[1], NULL, 10);
    if(id == ULONG_MAX)
      return NULL;

    for(tg=tglist;tg;tg=tg->next)
      if(tg->id == id)
        return tg;
  }

  for(tg=tglist;tg;tg=tg->next)
    if(!match(name, tg->name->content))
      return tg;

  id = strtoul(name, NULL, 10);
  if(id == ULONG_MAX)
    return NULL;

  /* legacy format */
  for(tg=tglist;tg;tg=tg->next)
    if(tg->id == id)
      return tg;

  return NULL;
}

