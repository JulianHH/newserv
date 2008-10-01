#include "../dbapi2/dbapi2.h"
#include "../core/error.h"
#include "../core/hooks.h"
#include "trusts.h"

DBAPIConn *trustsdb;
static int tgmaxid;
static int loaderror;
int trustsdbloaded;

void trusts_reloaddb(void);
void createtrusttables(int migration);

int trusts_loaddb(void) {
  trustsdb = dbapi2open(NULL, "trusts");
  if(!trustsdb) {
    Error("trusts", ERR_WARNING, "Unable to connect to db -- not loaded.");
    return 0;
  }

  createtrusttables(0);

  trusts_reloaddb();
  return 1;
}

void createtrusttables(int migration) {
  char *groups, *hosts;

  if(migration) {
    groups = "migration_groups";
    hosts = "migration_hosts";
  } else {
    groups = "groups";
    hosts = "hosts";
  }

  trustsdb->createtable(trustsdb, NULL, NULL,
    "CREATE TABLE ? (id INT PRIMARY KEY, name VARCHAR(?), trustedfor INT, mode INT, maxperident INT, maxseen INT, expires INT, lastseen INT, lastmaxuserreset INT, createdby VARCHAR(?), contact VARCHAR(?), comment VARCHAR(?))",
    "Tdddd", groups, TRUSTNAMELEN, NICKLEN, CONTACTLEN, COMMENTLEN
  );
  trustsdb->createtable(trustsdb, NULL, NULL, "CREATE TABLE ? (groupid INT, host VARCHAR(?), max INT, lastseen INT, PRIMARY KEY (groupid, host))", "Td", hosts, TRUSTHOSTLEN);
}

static void trusts_freedb(void) {
  trusts_freeall();

  trustsdbloaded = 0;
  tgmaxid = 0;
}

static void loadhosts_data(const DBAPIResult *result, void *tag) {
  if(!result) {
    loaderror = 1;
    return;
  }

  if(!result->success) {
    Error("trusts", ERR_ERROR, "Error loading hosts table.");
    loaderror = 1;

    result->clear(result);
    return;
  }

  if(result->fields != 4) {
    Error("trusts", ERR_ERROR, "Wrong number of fields in hosts table.");
    loaderror = 1;

    result->clear(result);
    return;
  }

  while(result->next(result)) {
    unsigned int groupid;
    char *host;
    unsigned int maxseen, lastseen;
    trustgroup *tg;

    groupid = strtoul(result->get(result, 0), NULL, 10);

    tg = tg_getbyid(groupid);
    if(!tg) {
      Error("trusts", ERR_WARNING, "Orphaned trust group host: %d", groupid);
      continue;
    }

    maxseen = strtoul(result->get(result, 2), NULL, 10);
    lastseen = (time_t)strtoul(result->get(result, 3), NULL, 10);
    host = result->get(result, 1);

    if(!th_add(tg, host, maxseen, lastseen))
      Error("trusts", ERR_WARNING, "Error adding host to trust %d: %s", groupid, host);
  }

  result->clear(result);

  if(!loaderror) {
    trustsdbloaded = 1;
    triggerhook(HOOK_TRUSTS_DB_LOADED, NULL);
  }
}

static void loadhosts_fini(const DBAPIResult *result, void *tag) {
  Error("trusts", ERR_INFO, "Finished loading hosts.");
}

static void loadgroups_data(const DBAPIResult *result, void *tag) {
  if(!result) {
    loaderror = 1;
    return;
  }

  if(!result->success) {
    Error("trusts", ERR_ERROR, "Error loading group table.");
    loaderror = 1;

    result->clear(result);
    return;
  }

  if(result->fields != 12) {
    Error("trusts", ERR_ERROR, "Wrong number of fields in groups table.");
    loaderror = 1;

    result->clear(result);
    return;
  }

  while(result->next(result)) {
    unsigned int id;
    sstring *name, *createdby, *contact, *comment;
    unsigned int trustedfor, mode, maxperident, maxseen;
    time_t expires, lastseen, lastmaxuserreset;

    id = strtoul(result->get(result, 0), NULL, 10);
    if(id > tgmaxid)
      tgmaxid = id;

    name = getsstring(result->get(result, 1), TRUSTNAMELEN);
    trustedfor = strtoul(result->get(result, 2), NULL, 10);
    mode = atoi(result->get(result, 3));
    maxperident = strtoul(result->get(result, 4), NULL, 10);
    maxseen = strtoul(result->get(result, 5), NULL, 10);
    expires = (time_t)strtoul(result->get(result, 6), NULL, 10);
    lastseen = (time_t)strtoul(result->get(result, 7), NULL, 10);
    lastmaxuserreset = (time_t)strtoul(result->get(result, 8), NULL, 10);
    createdby = getsstring(result->get(result, 9), NICKLEN);
    contact = getsstring(result->get(result, 10), CONTACTLEN);
    comment = getsstring(result->get(result, 11), COMMENTLEN);

    if(name && createdby && contact && comment) {
      if(!tg_add(id, name->content, trustedfor, mode, maxperident, maxseen, expires, lastseen, lastmaxuserreset, createdby->content, contact->content, comment->content))
        Error("trusts", ERR_WARNING, "Error adding trustgroup %d: %s", id, name->content);
    } else {
      Error("trusts", ERR_ERROR, "Error allocating sstring in group loader, id: %d", id);
    }

    freesstring(name);
    freesstring(createdby);
    freesstring(contact);
    freesstring(comment);
  }

  result->clear(result);  
}

static void loadgroups_fini(const DBAPIResult *result, void *tag) {
  Error("trusts", ERR_INFO, "Finished loading groups, maximum id: %d.", tgmaxid);
}

void trusts_reloaddb(void) {
  trusts_freedb();

  loaderror = 0;

  trustsdb->loadtable(trustsdb, NULL, loadgroups_data, loadgroups_fini, NULL, "groups");
  trustsdb->loadtable(trustsdb, NULL, loadhosts_data, loadhosts_fini, NULL, "hosts");
}

void trusts_closedb(void) {
  if(!trustsdb)
    return;

  trustsdb->close(trustsdb);
  trustsdb = NULL;
}
