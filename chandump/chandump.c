#include <stdio.h>

#include "../core/schedule.h"
#include "../channel/channel.h"

void *dumpsched;

void dodump(void *arg) {
  chanindex *c;
  int i;

  FILE *fp = fopen("chandump.txt.1", "w");
  if(!fp)
    return;

  for(i=0;i<CHANNELHASHSIZE;i++)
    for(c=chantable[i];c;c=c->next)
      if(c->channel && !IsSecret(c->channel))
        fprintf(fp, "%s %d%s%s\n", c->name->content, c->channel->users->totalusers, (c->channel->topic&&c->channel->topic->content)?" ":"", (c->channel->topic&&c->channel->topic->content)?c->channel->topic->content:"");

  fclose(fp);

  rename("chandump.txt.1", "chandump.txt");
} 

void _init() {
  dumpsched = (void *)schedulerecurring(time(NULL), 0, 300, &dodump, NULL);
}

void _fini() {
  deleteschedule(dumpsched, &dodump, NULL);
}
