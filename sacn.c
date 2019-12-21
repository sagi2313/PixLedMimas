#include "sacn.h"
#include <stdio.h>
#include <stdint-gcc.h>

#include <stdlib.h>
#include <inttypes.h>
#include <byteswap.h>

const uint8_t ACN_ID[12] = {0x41, 0x53, 0x43, 0x2d, 0x45, 0x31, 0x2e, 0x31, 0x37, 0x00, 0x00, 0x00};
int parse_e131(e131_pack_t* p)
{

    if(memcmp((void*)&p->root.packId[0], &ACN_ID[0], 12) != 0)return(-1);
    *(uint16_t*)&p->root.chdr = bswap_16(*(uint16_t*)&p->root.chdr);
    p->root.rootVector = bswap_32(p->root.rootVector);
    switch(p->root.rootVector)
    {
        case VECTOR_ROOT_E131_DATA:
        {
            e131_framing_layer_data_t *f= &p->dat.framing;
            e131_data_pdu_t *d = &p->dat;
            e131_dmp_layer_t *dm = &p->dat.dmp;
            *(uint16_t*)&f->chdr = bswap_16(*(uint16_t*)&f->chdr);
            f->frmDatVector =bswap_32(f->frmDatVector);
            f->uniNum = bswap_16(f->uniNum);
            f->syncAddr = bswap_16(f->syncAddr);

            *(uint16_t*)&dm->chdr = bswap_16(*(uint16_t*)&dm->chdr );
            dm->addrInc = bswap_16(dm->addrInc);
            dm->frstPropAddr = bswap_16(dm->frstPropAddr);
            dm->proValCnt = bswap_16(dm->proValCnt);
            break;
        }
        default:
        {
            printf("E131: other roort vector %u\n", p->root.rootVector);
        }

    }
    return(0);
}
