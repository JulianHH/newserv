include ../build.mk

LDFLAGS+=-lc

.PHONY: all
all: sqlite.so sqlite-dbapi2.so

libsqlite3.o: libsqlite3.c libsqlite3/sqlite3.c
	$(CC) $(CFLAGS) -c libsqlite3.c -fPIC -w

sqlite.so: sqlite.o libsqlite3.o

sqlite-dbapi2.so: sqlite-dbapi2.o

sqlite-dbapi2.o: sqlite-dbapi2.c ../dbapi2/dbapi2-adapter.inc
