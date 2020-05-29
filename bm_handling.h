#ifndef BM_HANDLING_H_INCLUDED
#define BM_HANDLING_H_INCLUDED
#include <stdint-gcc.h>
#include <stdlib.h>
typedef uint64_t blkTyp;

typedef enum
{
  bm_reserved_e=0,
  bm_free_e=1
}taken_e;

typedef struct
{
uint32_t elements;
uint32_t reserved;
blkTyp*  bm;
}bm_t;


// size of block in bits: 1 byte block returns 8

#define   blockSize ((uint64_t)(sizeof(blkTyp) * 8))

// number of blocks required to cover E elements, rounded up for multiples of blocksize

#define   BMBLOCKS( E ) ( (uint32_t) ( E / blockSize ) + ((E % blockSize)?1:0) )

// sizeof whole BM in bytes

#define   BMSIZE( E ) (uint32_t) (( (uint32_t) BMBLOCKS( E ) ) *  (uint32_t) sizeof(blkTyp))





taken_e updateBM(bm_t *bm, taken_e val, uint64_t elemIdx);

taken_e getBMValue(bm_t *bm, uint64_t elemIdx);

void clearBM(bm_t* bm);

bm_t* createBm(uint32_t elements);

void free_bm(bm_t* bm);

#endif // BM_HANDLING_H_INCLUDED
