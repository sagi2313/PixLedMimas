#include "gen_lists.h"


ln_t addItem(ln_t head, void* item)
{
    ln_t n;
    if(head == NULL)
    {
        n = (ln_t)malloc(sizeof(lnode_t));
    }
    else
    {
        n = head;
        while(n->nxt!=NULL)
        {n = n->nxt;
        }
        n->nxt = (ln_t)malloc(sizeof(lnode_t));
        n = n->nxt;
    }
    n->nxt = NULL;
    n->data = item;
    return(n);

}

int remItem(ln_t head, void* item)
{
    if(head == NULL)return(-1);
    ln_t n = head;
    ln_t p = NULL;
    while(n->data!=item)
    {
        p = n;
        if(n->nxt == NULL)return(-2);
        n = n->nxt;
    }
    p->nxt = n->nxt;
    free(n);
    return(0);
}
