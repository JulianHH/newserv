/*
 * ip functionality 
 */

#include "newsearch.h"

#include <stdio.h>
#include <stdlib.h>

void *ip_exe(struct searchNode *thenode, int type, void *theinput);
void ip_free(struct searchNode *thenode);

struct searchNode *ip_parse(int type, int argc, char **argv) {
  struct searchNode *thenode;

  if (type != SEARCHTYPE_NICK) {
    parseError = "ip: this function is only valid for nick searches.";
    return NULL;
  }

  thenode=(struct searchNode *)malloc(sizeof (struct searchNode));

  thenode->returntype = RETURNTYPE_STRING;
  thenode->localdata = NULL;
  thenode->exe = ip_exe;
  thenode->free = ip_free;

  return thenode;
}

void *ip_exe(struct searchNode *thenode, int type, void *theinput) {
  nick *np = (nick *)theinput;

  if (type != RETURNTYPE_STRING) {
    return (void *)1;
  }

  return IPtostr(np->ipaddress);
}

void ip_free(struct searchNode *thenode) {
  free(thenode);
}

