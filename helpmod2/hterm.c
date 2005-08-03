#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "../lib/sstring.h"

#include "hterm.h"

#include "hgen.h"

hterm *hterm_add(hterm** ptr, const char *name, const char *desc)
{
    hterm *htrm;

    if (ptr == NULL)
        ptr = &hterms;

    if (name == NULL || desc == NULL || hterm_get(*ptr, name))
        return NULL;

    for (;*ptr && strcmp(name, (*ptr)->name->content) > 0;ptr = &(*ptr)->next);

    htrm = (hterm*)malloc(sizeof(hterm));
    htrm->name = getsstring(name, strlen(name));
    htrm->description = getsstring(desc, strlen(desc));
    htrm->usage = 0;

    htrm->next = *ptr;
    *ptr = htrm;

    return htrm;
}

hterm *hterm_get(hterm *source, const char *str)
{
    hterm *ptr = source;

    for (;ptr;ptr = ptr->next)
        if (!ci_strcmp(ptr->name->content, str))
            return ptr;
    return NULL;
}

hterm *hterm_find(hterm *source, const char *str)
{
    hterm *ptr;
    char buffer[512];

    sprintf(buffer, "*%s*", str);

    for (ptr = source;ptr;ptr = ptr->next)
        if (strregexp(ptr->name->content, buffer))
            return ptr;
    for (ptr = source;ptr;ptr = ptr->next)
        if (strregexp(ptr->description->content, buffer))
            return ptr;
    return NULL;
}

hterm *hterm_get_and_find(hterm *source, const char *str)
{
    /* search order: get source, get hterms, find source, find hterms */
    hterm *ptr;
    ptr = hterm_get(source, str);
    if (ptr != NULL)
    {
        ptr->usage++;
        return ptr;
    }
    ptr = hterm_get(hterms, str);
    if (ptr != NULL)
    {
        ptr->usage++;
        return ptr;
    }
    ptr = hterm_find(source, str);
    if (ptr != NULL)
    {
        ptr->usage++;
        return ptr;
    }
    ptr = hterm_find(hterms, str);
    if (ptr != NULL)
    {
        ptr->usage++;
        return ptr;
    }
    return NULL;
}

hterm *hterm_del(hterm** start, hterm *htrm)
{
    hterm** ptr = start;

    for (;*ptr;ptr = &(*ptr)->next)
        if (*ptr == htrm)
        {
            hterm *tmp = (*ptr)->next;
            freesstring((*ptr)->name);
            freesstring((*ptr)->description);
            free(*ptr);
            *ptr = tmp;

            return NULL;
        }

    return htrm;
}

void hterm_del_all(hterm **source)
{
    if (source == NULL)
        source = &hterms;

    while (*source)
        hterm_del(source, *source);
}

int hterm_count(hterm* ptr)
{
    int i = 0;
    for (;ptr;ptr = ptr->next)
        i++;
    return i;
}
