#include "bm_handling.h"
#include "utils.h"

void clearBM(bm_t* bm)
{
    int t = ~(0);
    //printf("Clearing %u bytes\n",BMSIZE(bm->elements));
    memset(&(bm->bm), t, BMSIZE(bm->elements));
    bm->reserved = 0;
}

bm_t* createBm(uint32_t elements)
{
  if(elements<1)return(NULL);
  uint32_t siz = sizeof(bm_t) + BMSIZE(elements);
  bm_t* bm = (bm_t*)malloc(siz);
  if(bm!=NULL)
  {
    bm->elements = elements;
    clearBM(bm);
  }
  prnFinf(log_any,"Created BM %u bytes in size, blockSize = %llu\n",siz,blockSize);
  return(bm);
}

void free_bm(bm_t* bm)
{
  if(bm!=NULL)
  {
    free(bm);
  }
}
 /*updates the BM at given position 'elemIdx', with value 'val'
 returns previous value of updated position*/
taken_e updateBM(bm_t *bm, taken_e val, uint64_t elemIdx)
{
    blkTyp mask;
    blkTyp *dwptr = &bm->bm;
    dwptr+=(elemIdx / blockSize);
    mask = ( ((blkTyp)1) <<(elemIdx % blockSize));
    if(val == bm_reserved_e)
    {
        if((*dwptr) & mask)
        {
            bm->reserved++;
            *dwptr &= ~mask;
            return(bm_free_e);
        }
        return(bm_reserved_e);
    }
    else
    {
        if(((*dwptr) & mask) == (blkTyp)0)
        {
            *dwptr|=mask;
            bm->reserved--;
            return(bm_reserved_e);
        }
        return(bm_free_e);
    }
}

taken_e getBMValue(bm_t *bm, uint64_t elemIdx)
{
  blkTyp mask=0;
  mask = ( ((blkTyp)1) <<(elemIdx % blockSize));
  blkTyp *dwptr = &bm->bm;
  dwptr+=(elemIdx / blockSize);
  return((*dwptr & mask)?bm_free_e:bm_reserved_e);
}
