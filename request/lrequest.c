/* required modules: splitlist, chanfix(3) */

#include <stdio.h>
#include "request.h"
#include "lrequest.h"
#include "request_block.h"
#include "../localuser/localuser.h"

/* stats counters */
int lr_noregops = 0;
int lr_scoretoolow = 0;
int lr_top5 = 0;
int lr_floodattempts = 0;

#define min(a,b) ((a > b) ? b : a)

int lr_requestl(nick *svc, nick *np, channel *cp, nick *lnick) {
  chanfix *cf;
  regop *rolist[LR_TOPX], *ro;
  int i, rocount;

  if (strlen(cp->index->name->content) > LR_MAXCHANLEN) {
    sendnoticetouser(svc, np, "Channel name is too long. You will have to "
          "create a channel with a name less than %d characters long.",
          LR_MAXCHANLEN + 1);

    return RQ_ERROR;
  }

  cf = cf_findchanfix(cp->index);

  if (cf == NULL) {
    sendnoticetouser(svc, np, "Error: Your channel is too new. Try again later.");

    lr_noregops++;

    return RQ_ERROR;
  }

  rocount = cf_getsortedregops(cf, LR_TOPX, rolist);

  ro = NULL;

  for (i = 0; i < min(LR_TOPX, rocount); i++) {
    if (cf_cmpregopnick(rolist[i], np)) {
      ro = rolist[i];
      break;
    }
  }

  if (ro == NULL) {
    sendnoticetouser(svc, np, "Error: You must be one of the top %d ops "
          "for that channel.", LR_TOPX);

    lr_top5++;

    return RQ_ERROR;
  }

  /* treat blocked users as if their score is too low */
  if (ro->score < LR_CFSCORE || rq_findblock(np->authname)) {
    if (rq_isspam(np)) {
      sendnoticetouser(svc, np, "Error: Do not flood the request system. "
            "Try again in %s.", rq_longtoduration(rq_blocktime(np)));

      lr_floodattempts++;

      return RQ_ERROR;
    }

    sendnoticetouser(svc, np, "Try again later. You do not meet the "
          "requirements to request L. You may need to wait longer "
          "(see http://www.quakenet.org/faq/faq.php?c=3&f=112 )");

    lr_scoretoolow++;

    return RQ_ERROR;
  }

  sendmessagetouser(svc, lnick, "addchan %s #%s %s", cp->index->name->content,
        np->authname, np->nick);

  sendnoticetouser(svc, np, "Requirements met, L should be added. Contact #help"
        " should further assistance be required.");

  return RQ_OK;
}

void lr_requeststats(nick *rqnick, nick *np) {
  sendnoticetouser(rqnick, np, "- No registered ops (L):          %d", lr_noregops);
  sendnoticetouser(rqnick, np, "- Score too low (L):              %d", lr_scoretoolow);
  sendnoticetouser(rqnick, np, "- Not in top%d (L):                %d", LR_TOPX, lr_top5);
  sendnoticetouser(rqnick, np, "- Floods (L):                     %d", lr_floodattempts);
}
