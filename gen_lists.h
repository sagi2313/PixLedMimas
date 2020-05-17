#ifndef GEN_LISTS_H_INCLUDED
#define GEN_LISTS_H_INCLUDED

#include "type_defs.h"
typedef struct _lnode_t *ln_t;
typedef struct _lnode_t
{
    void*   data;
    ln_t      nxt;
}lnode_t;


ln_t addItem(ln_t *head, void* item);

int remItem(ln_t *head, void* item);
ln_t findItem(ln_t n, void* item, int len, int offset);
#endif // GEN_LISTS_H_INCLUDED
