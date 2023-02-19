#include "gen_lists.h"
#include <stdlib.h>

ln_t addItem(ln_t *head, void* item)
{
    if(head == NULL)return(NULL);
    if(item == NULL)return(NULL);
    ln_t n;
    if(*head == NULL)
    {
        *head  =  (ln_t)calloc(1, sizeof(lnode_t));
        (*head)->data = item;
        return(*head);
    }
    else
    {
        n = *head;
    }
    while(n->nxt!=NULL)n = n->nxt;
    n->nxt = (ln_t)malloc(sizeof(lnode_t));
    n = n->nxt;
    n->nxt = NULL;
    n->data = item;
    return(n);
}

int remItem(ln_t *head, void* item)
{
    if(head == NULL)return(-1); // bad input
    if(*head == NULL)return(-2); // empty list
    ln_t p = *head;
    if(item == p->data)
    {
        p = p->nxt;
        free(*head);
        return(0);
    }
    ln_t n = p->nxt;

    while(n)
    {
        if(n->data==item)
        {
            p->nxt = n->nxt;
            free(n);
            return(0);
        }
        p = n;
        n = n->nxt;
    }
    return(-3); // item not found
}

ln_t findItem(ln_t n, void* item, int len, int offset)
{
    if(item == NULL)return(NULL);
    uint8_t* pt1;
    uint8_t* pt2 = (uint8_t*)item;
    pt2+=offset;
    while(n)
    {
        pt1 = (uint8_t*)n->data;
        pt1+=offset;
        if( memcmp(  pt1, pt2, len)==0)break;
        n = n->nxt;
    }
    return(n);
}
