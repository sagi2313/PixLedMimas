#include "sacn.h"
#include <stdio.h>
#include <stdint-gcc.h>

#include <stdlib.h>
#include <inttypes.h>
#include <byteswap.h>

#include "utils.h"

const uint8_t ACN_ID[12] = {0x41, 0x53, 0x43, 0x2d, 0x45, 0x31, 0x2e, 0x31, 0x37, 0x00, 0x00, 0x00};

int parse_disco(e131_uni_disco_pdu_t* disc)
{
	e131_framing_layer_uni_disco_t* frm = &disc->framing;
	e131_uni_disco_layer_t *unis = &disc->uni_disco;

	unis->chdr.raw_hdr = __builtin_bswap16(unis->chdr.raw_hdr);
	prnFinf(log_sacn,"Disco Frm len %u srcName %s", frm->chdr.len, frm->srcName);
	prnFinf(log_sacn,"Disco unis len %u pageF %u, pageL %u", unis->chdr.len, unis->page, unis->last);
	uint16_t list_len = (unis->chdr.len - offsetof(e131_uni_disco_layer_t, uniList))/2;
	uint16_t* uni = &unis->uniList[0];
	if(list_len > 1024)
	{
        prnErr(log_sacn,"Disco list length error: %u\n",list_len);
        return(sacn_err_e);
	}
	for(int i = 0; i < list_len;i++)
	{
		prnFinf(log_sacn," uni %i : %u",i, __builtin_bswap16(uni[i]));
	}
	return(sacn_disco_e);
}

sacn_rc_e parse_e131(e131_pack_t* p)
{
    if(memcmp((void*)&p->root.packId[0], &ACN_ID[0], 12) != 0)return(sacn_err_e);
    p->root.chdr.raw_hdr = bswap_16(p->root.chdr.raw_hdr);
    p->root.rootVector = bswap_32(p->root.rootVector);

    switch(p->root.rootVector)
    {
        case VECTOR_ROOT_E131_DATA_C:
        {
            e131_framing_layer_data_t *f= &p->dat.framing;
            //e131_data_pdu_t *d = &p->dat;
            e131_dmp_layer_t *dm = &p->dat.dmp;

            if(dm->addr_data_typ != 0xA1)
            {
            	prnErr(log_sacn,
            	"Invalid data packet with addr_data_typ = 0x%X at offset %u, dmpVect %X from src %s, fPropAdr %x syncAdr %X, seqNum %u, uni %u dropped", \
            	dm->addr_data_typ, offsetof(e131_dmp_layer_t,addr_data_typ), dm->dmpDatVector,f->srcName, \
				dm->frstPropAddr, f->syncAddr, f->seqNum, f->uniNum);
            	return(sacn_err_e);
            }

            f->chdr.raw_hdr = bswap_16(f->chdr.raw_hdr);
            f->frmDatVector =bswap_32(f->frmDatVector);
            f->uniNum = bswap_16(f->uniNum);
            f->syncAddr = bswap_16(f->syncAddr);

            dm->chdr.raw_hdr = bswap_16(dm->chdr.raw_hdr );
            dm->addrInc = bswap_16(dm->addrInc);
            dm->frstPropAddr = bswap_16(dm->frstPropAddr);
            dm->proValCnt = bswap_16(dm->proValCnt);
            prnInf(log_sacn, "DMX data uni %u, slots %u, seq_num %u", f->uniNum, dm->proValCnt, f->seqNum);
            break;
        }
        case VECTOR_ROOT_E131_DRAFTDATA_C:
        {
        	e131_framing_layer_draftdata_t *f= &p->ddat.framing;
            //e131_data_pdu_t *d = &p->ddat;
            e131_draftdmp_layer_t *dm = &p->ddat.dmp;
            if(dm->addr_data_typ != 0xA1)
            {
            	prnErr(log_sacn,
            	"Invalid data packet with addr_data_typ = 0x%X at offset %u, dmpVect %X from src %s, seqNum %u, uni %u dropped", \
            	dm->addr_data_typ, offsetof(e131_dmp_layer_t,addr_data_typ), dm->Vector,f->srcName, \
				f->seqNum, f->uniNum);
            	return(sacn_err_e);
            }
            f->chdr.raw_hdr = __builtin_bswap16(f->chdr.raw_hdr);
            f->frmDatVector =__builtin_bswap32(f->frmDatVector);
            f->uniNum = __builtin_bswap16(f->uniNum);
            //f->syncAddr = __builtin_bswap16(f->syncAddr);

            dm->chdr.raw_hdr = __builtin_bswap16(dm->chdr.raw_hdr);
            dm->addrInc = __builtin_bswap16(dm->addrInc);
            //dm->frstPropAddr = __builtin_bswap16(dm->frstPropAddr);
            dm->proValCnt = __builtin_bswap16(dm->proValCnt);
            prnInf(log_sacn, "DMX ddata uni %u, slots %u, seq_num %u", f->uniNum, dm->proValCnt, f->seqNum);
            break;
        }
        case VECTOR_ROOT_E131_EXTENDED_C:
		{
            p->genVec32 = __builtin_bswap32(p->genVec32);
			switch(p->genVec32)
			{
				case VECTOR_E131_EXTENDED_DISCOVERY_C:
				{
					e131_uni_disco_pdu_t* disc = &p->uni_disco;
					e131_framing_layer_uni_disco_t* frm = &disc->framing;
					e131_uni_disco_layer_t *unis = &disc->uni_disco;
					//unis->DiscoVector = __builtin_bswap32(unis->DiscoVector);
					if(unis->DiscoVector != __builtin_bswap32(VECTOR_UNIVERSE_DISCOVERY_UNIVERSE_LIST_C))
					{
						prnErr(log_sacn,"VECTOR_ROOT_E131_EXTENDED_C with disco Vector %u, expected %u",\
							__builtin_bswap32(unis->DiscoVector), VECTOR_UNIVERSE_DISCOVERY_UNIVERSE_LIST_C);
						return(sacn_err_e);
					}
					return(parse_disco(disc));
					break;
				}
				case VECTOR_E131_EXTENDED_SYNCHRONIZATION_C:
				{
					e131_framing_layer_sync_t* sync = &p->sync;
					sync->syncAddr = __builtin_bswap16(sync->syncAddr);
					prnInf(log_sacn,"Sync received for Addr %u, seq %u", sync->syncAddr, sync->seqNum);
					break;
				}
				default:
				{
					prnErr(log_sacn,"VECTOR_ROOT_E131_EXTENDED_C with frame Vector %u",p->genVec32);
					return(-1);
					break;
				}
			}
			break;
		}

        default:
        {
            prnErr(log_sacn,"E131: other root vector %u\n", p->root.rootVector);
            return(sacn_err_e);
        }
    }
    return(0);
}
