#include "../lib/sstring.h"
#include "events.h"
#include "schedule.h"
#include "hooks.h"
#include "modules.h"
#include "config.h"
#include "error.h"
#include "nsmalloc.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

int newserv_shutdown_pending;

void initseed();

int main(int argc, char **argv) {
  initseed();
  inithooks();
  inithandlers();
  initschedule();
  
  initsstring();
  initnsmalloc();
  
  if (argc>1) {
    initconfig(argv[1]);
  } else {  
    initconfig("newserv.conf");
  }

  /* Loading the modules will bring in the bulk of the code */
  initmodules();

  /* Main loop */
  for(;;) {
    handleevents(10);  
    doscheduledevents(time(NULL));
    if (newserv_shutdown_pending) {
      newserv_shutdown();
      break;
    }
  }  
  
  nsfreeall(POOL_SSTRING);
  nsexit();
}

/*
 * seed the pseudo-random number generator, rand()
 */
void initseed() {
  struct timeval t;

  gettimeofday(&t, NULL);
  srand(t.tv_usec);
}
