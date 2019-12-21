#ifndef SACN_H_INCLUDED
#define SACN_H_INCLUDED
#include <stdint-gcc.h>

// GLOBAL TYPES
#define VECTOR_ROOT_E131_DATA                   0x04
#define VECTOR_ROOT_E131_EXTENDED               0x08

#define VECTOR_E131_DATA_PACKET                 0x02
#define VECTOR_DMP_SET_PROPERTY                 0x02

#define VECTOR_E131_EXTENDED_SYNCHRONIZATION    0x01
#define VECTOR_E131_EXTENDED_DISCOVERY          0x02

#define E131_E131_UNIVERSE_DISCOVERY_INTERVAL       /*10 seconds*/
#define E131_NETWORK_DATA_LOSS_TIMEOUT              /*2.5 seconds*/

#define E131_DISCOVERY_UNIVERSE 64214

#define VECTOR_UNIVERSE_DISCOVERY_UNIVERSE_LIST 0x01

#define ACN_SDT_MULTICAST_PORT 5568

extern const uint8_t ACN_ID[];

typedef struct __attribute__((packed))
{

        uint16_t    len:12;
        uint16_t    flags:4;

}comm_hdr_t;

typedef struct __attribute__((packed))
{
    uint16_t    preambuleSz;
    uint16_t    postambuleSz;
    uint8_t     packId[12];
    comm_hdr_t  chdr;
    uint32_t    rootVector;
    uint8_t         cid[16];
}e131_root_layer_t;

// DATAPACKET TYPES

typedef struct __attribute__((packed))
{
    comm_hdr_t      chdr;
    uint32_t        frmDatVector;
    char            srcName[64];
    uint8_t         priority;
    uint16_t        syncAddr;
    uint8_t         seqNum;
    union
    {
        uint8_t     options;
        struct
        {
            uint8_t dc:5;
            uint8_t frcSync:1;
            uint8_t strmTerm:1;
            uint8_t previewData:1;
        };
    };
    uint16_t        uniNum;
}e131_framing_layer_data_t;

typedef struct __attribute__((packed))
{

    comm_hdr_t      chdr;
    uint8_t         dmpDatVector;
    uint8_t         addr_data_typ;
    uint16_t        frstPropAddr;
    uint16_t        addrInc;
    uint16_t        proValCnt;
    uint8_t         startCode;
    uint8_t         propVals[512];
}e131_dmp_layer_t;

// SYNC
typedef struct __attribute__((packed))
{
    comm_hdr_t      chdr;
    uint32_t        frmSyncVector;
    uint8_t         seqNum;
    uint16_t        syncAddr;
    uint16_t        reserved;

}e131_framing_layer_sync_t;

// UNI DISCOVERY
typedef struct __attribute__((packed))
{
    comm_hdr_t      chdr;
    uint32_t        frmDiscoVector;
    uint8_t         srcName[64];
    uint32_t        reserved;

}e131_framing_layer_uni_disco_t;

typedef struct __attribute__((packed))
{
    comm_hdr_t      chdr;
    uint32_t        DiscoVector;
    uint8_t         page;
    uint8_t         last;
    uint8_t         uniList[1024];
}e131_uni_disco_layer_t;

typedef struct  __attribute__((packed))
{
    e131_framing_layer_data_t framing;
    e131_dmp_layer_t          dmp;

}e131_data_pdu_t;

typedef struct   __attribute__((packed))
{
    e131_framing_layer_uni_disco_t framing;
    e131_uni_disco_layer_t          uni_disco;
}e131_uni_disco_pdu_t;

typedef struct  __attribute__((packed))
{
    e131_root_layer_t root;
    union
    {
        e131_data_pdu_t             dat;
        e131_uni_disco_pdu_t        uni_disco;
        e131_framing_layer_sync_t   sync;
    };
}e131_pack_t;

int parse_e131(e131_pack_t* p);
#endif // SACN_H_INCLUDED
