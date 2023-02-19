#ifndef SACN_H_INCLUDED
#define SACN_H_INCLUDED
#include <stdint-gcc.h>

// GLOBAL TYPES
#define VECTOR_ROOT_E131_DATA_C                 0x04
#define VECTOR_ROOT_E131_DRAFTDATA_C            0x03
#define VECTOR_ROOT_E131_EXTENDED_C             0x08

#define VECTOR_E131_DATA_PACKET                 0x02
#define VECTOR_DMP_SET_PROPERTY                 0x02

#define VECTOR_E131_EXTENDED_SYNCHRONIZATION_C  0x01
#define VECTOR_E131_EXTENDED_DISCOVERY_C        0x02

#define E131_E131_UNIVERSE_DISCOVERY_INTERVAL       /*10 seconds*/
#define E131_NETWORK_DATA_LOSS_TIMEOUT              /*2.5 seconds*/

#define E131_DISCOVERY_UNIVERSE 64214

#define VECTOR_UNIVERSE_DISCOVERY_UNIVERSE_LIST_C 0x01

#define ACN_SDT_MULTICAST_PORT 5568

extern const uint8_t ACN_ID[];

#pragma pack(1)

typedef union
{
    uint16_t    raw_hdr;
    struct
    {
        uint16_t    len:12;
        uint16_t    flags:4;
    };
}comm_hdr_t;


typedef struct
{
    uint16_t    preambuleSz;
    uint16_t    postambuleSz;
    uint8_t     packId[12];
    comm_hdr_t  chdr;
    uint32_t    rootVector;
    uint8_t     cid[16];
}e131_root_layer_t;

// DATAPACKET TYPES

typedef struct
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

typedef struct
{
    comm_hdr_t      chdr;
    uint32_t        frmDatVector;
    char            srcName[32];
    uint8_t         priority;
    uint8_t         seqNum;
    uint16_t        uniNum;
}e131_framing_layer_draftdata_t;

typedef struct
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

typedef struct
{
    comm_hdr_t      chdr;
    uint8_t    		Vector;
    uint8_t         addr_data_typ;
    uint16_t        startCode;
    uint16_t        addrInc;
    uint16_t        proValCnt;
    uint8_t         propVals[512];
}e131_draftdmp_layer_t;

// SYNC
typedef struct
{
    comm_hdr_t      chdr;
    uint32_t        frmSyncVector;
    uint8_t         seqNum;
    uint16_t        syncAddr;
    uint16_t        reserved;

}e131_framing_layer_sync_t;

// UNI DISCOVERY
typedef struct
{
    comm_hdr_t      chdr;
    uint32_t        frmDiscoVector;
    uint8_t         srcName[64];
    uint32_t        reserved;

}e131_framing_layer_uni_disco_t;

typedef struct
{
    comm_hdr_t      chdr;
    uint32_t        DiscoVector;
    uint8_t         page;
    uint8_t         last;
    uint8_t         uniList[1024];
}e131_uni_disco_layer_t;

typedef struct
{
    e131_framing_layer_data_t framing;
    e131_dmp_layer_t          dmp;

}e131_data_pdu_t;

typedef struct
{
    e131_framing_layer_uni_disco_t framing;
    e131_uni_disco_layer_t          uni_disco;
}e131_uni_disco_pdu_t;

typedef struct
{
    e131_framing_layer_draftdata_t framing;
    e131_draftdmp_layer_t          dmp;

}e131_draftdata_pdu_t;

typedef struct
{
    e131_root_layer_t root;
    union
    {
        struct{
        comm_hdr_t                  hdr;
        uint32_t                    genVec32;
        };
        e131_data_pdu_t             dat;
        e131_draftdata_pdu_t		ddat; // draft data
        e131_uni_disco_pdu_t        uni_disco;
        e131_framing_layer_sync_t   sync;
    };
}e131_pack_t;

#pragma pack()
typedef enum
{
sacn_err_e = -1,
sacn_data_e,
sacn_ddata_e,
sacn_disco_e,
sacn_sync_e,

}sacn_rc_e;

sacn_rc_e parse_e131(e131_pack_t* p);
int sacn_socket_open(void);
int sacnModMembers(int sock, uint16_t firstAdr, uint8_t adrCnt, int optName);
#endif // SACN_H_INCLUDED
