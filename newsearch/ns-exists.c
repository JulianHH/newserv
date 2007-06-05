/*
 * exists functionality 
 */

#include "newsearch.h"

#include <stdio.h>
#include <stdlib.h>

void *exists_exe(struct searchNode *thenode, void *theinput);
void exists_free(struct searchNode *thenode);

struct searchNode *exists_parse(int type, int argc, char **argv) {
  struct searchNode *thenode;

  if (type != SEARCHTYPE_CHANNEL) {
    parseError = "exists: this function is only valid for channel searches.";
    return NULL;
  }

  if (!(thenode=(struct searchNode *)malloc(sizeof (struct searchNode)))) {
    parseError = "malloc: could not allocate memory for this search.";
    return NULL;
  }

  thenode->returntype = RETURNTYPE_BOOL;
  thenode->localdata = NULL;
  thenode->exe = exists_exe;
  thenode->free = exists_free;

  return thenode;
}

void *exists_exe(struct searchNode *thenode, void *theinput) {
  chanindex *cip = (chanindex *)theinput;

  if (cip->channel == NULL)
    return (void *)0;
  
  return (void *)1;
}

void exists_free(struct searchNode *thenode) {
  free(thenode);
}

