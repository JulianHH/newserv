/*
  nterfaced request management
  Copyright (C) 2003-2004 Chris Porter.
*/

struct request *requests;

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "requests.h"
#include "library.h"
#include "logging.h"

unsigned short total_count = 1;

/* modifies input */
int new_request(struct transport *input, int tag, char *line, int *number) {
  /* should probably check for duplicate tokens at the input side */
  char *sp, *p;
  struct request *prequest;
  struct service *service;
  int i;
  
  if(!line || !line[0] || (line[0] == ','))
    return RE_BAD_LINE;

  for(sp=line;*sp;sp++)
    if(*sp == ',')
      break;

  if(!*sp || !*(sp + 1))
    return RE_BAD_LINE;

  *sp = '\0';

  service = NULL;

  for(i=0;i<service_count;i++) {
    if(!strcmp(services[i].service->content, line)) {
      service = &services[i];
      break;
    }
  }

  for(p=sp+1;*p;p++)
    if(*p == ',')
      break;

  if(!*p || !(p + 1))
    return RE_BAD_LINE;
  
  *p = '\0';
  *number = positive_atoi(sp + 1);
  
  if(*number == -1)
    return RE_BAD_LINE;

  if (!service)
    return RE_SERVICE_NOT_FOUND;

  if (!service->transport)
    return RE_TRANSPORT_NOT_FOUND;

  prequest = (struct request *)malloc(sizeof(struct request));
  MemCheckR(prequest, RE_MEM_ERROR);
  prequest->next = NULL;
  prequest->input.transport = input;
  prequest->input.token = *number;
  prequest->input.tag = tag;
  prequest->output.transport = service->transport;
  prequest->output.tag = 0;
  prequest->output.token = total_count++;
  prequest->service = service;

  if(service->transport->on_line(prequest, p + 1)) {
    free_request(prequest);
    return RE_REQUEST_REJECTED;
  }
  
  prequest->next = requests;
  requests = prequest;

  return 0;
}


void finish_request(struct transport *output, char *data) {
  int id;
  char *p;
  struct request *cr = NULL, *lastcr = NULL;
  
  for(p=data;*p;p++)
    if(*p == ',')
      break;

  if(!*p || !(p + 1))
    return;
  
  *p = '\0';
  id = positive_atoi(data);

  if(!id || (id > 0xffff))
    return;

  for(cr=requests;cr;lastcr=cr,cr=cr->next)
    if(cr->output.token == id)
      break;

  if(!cr)
    return;

  if(lastcr) {
    lastcr->next = cr->next;
  } else {
    requests = cr->next;
  }

  /* AWOOOGA */
  cr->next = NULL;
  cr->input.transport->on_line(cr, p + 1);
  free_request(cr);
}

void free_request(struct request *rp) {
  free(rp);
}
