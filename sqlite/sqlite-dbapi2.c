#define DBAPI2_ADAPTER_NAME "sqlite"
#define USE_DBAPI_SQLITE

#include "../dbapi2/dbapi2-adapter.inc"

MODULE_VERSION("");

void _init() {
  if(sqliteconnected())
    registeradapterprovider();
}

void _fini() {
  deregisteradapterprovider();
}

