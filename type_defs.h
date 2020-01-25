#ifndef TYPE_DEFS_H_INCLUDED
#define TYPE_DEFS_H_INCLUDED


#include <stdio.h>
#include <stdint-gcc.h>
#include <sys/types.h>
#include <unistd.h>

#include <asm/socket.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netpacket/packet.h>



#include <linux/wireless.h>
#include <linux/if_ether.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <bcm2835.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>


//#include "protocolCommon.h"

#ifndef  BIT64
#define BIT64(B)  ( (uint64_t)( ((uint64_t)1ULL)<<((uint64_t)(B)) ) )
#endif

#ifndef  BIT32
#define BIT32(B)  ( (uint32_t)( ((uint32_t)1ULL)<<((uint32_t)(B)) ) )
#endif

#ifndef  BIT8
#define BIT8(B)  ( (uint8_t)( ((uint32_t)1ULL)<<((uint32_t)(B)) )  & 0xFF)
#endif


#define     GLOBAL_OUTPUTS_MAX 16
#define     UNI_PER_OUT 5

#define NODE_VERSION ((uint16_t)0x0001)
#define NODE_NAME_DEF   ((const char*)"PixLed\0")

#define PIX_PER_UNI     (150)
#define CHAN_PER_PIX    (3)


typedef enum
{
    rgb_map_e=0,
    grb_map_e,
    rbg_map_e,
    gbr_map_e,
    brg_map_e,
    bgr_map_e
}color_mapping_e;



extern const char* rstcauses[];/*={"Init","FrErr","ArtOther","NoSema","Socket Timeout"};*/
extern const char* smstates[];/*={"out_of_sync_e","wait_sync","wait_seq","working_e","full_frame_e","process_out","free_e","frame_err"};*/

typedef enum /*art_resp_e*/
{
	art_data_e=0,
	art_syn_pack_e,
	art_syncing_e,
	no_more_e,
	art_poll_e,
	art_sync_error_e,
	art_rx_buff_over_e,
	art_error_e	,
	net_error_e,
	addressing_net_err_e,
	addressing_uni_err_e,
	no_init_e,
	ignore_error_e
}art_resp_e;

typedef enum /*sm_state_e*/
{
	out_of_sync_e,
	wait_sync_e,
	wait_min_uni_e,
	/*wait_seq_e,*/
	working_e,
	full_frame_e,
	process_out_e,
	free_e,
	frame_err_e
}sm_state_e;

typedef enum /*sm_reset_causes_e*/
{
	eResetInit=0,
	eResetFrErr,
	eResetArtOther,
	eResetNoSema,
	eResetSockTimeout,
	eResetUnknown,
	eResetMaxCause
}sm_reset_causes_e;

typedef enum /*protoSelect_e*/
{
    protoUndef,
    protoArtNet,
    protosACN
}protoSelect_e;


typedef struct
{
    uint8_t dmx_data[512];
}dmx_uni_out_t;

typedef struct
{
    uint8_t mimas_header[4];
    dmx_uni_out_t dmxp[UNI_PER_OUT];
}mimaspack_t;

typedef struct
{
    mimaspack_t     *mimaPack;
    uint8_t         *wrPt[UNI_PER_OUT];
    uint16_t        uniLenLimit[UNI_PER_OUT]; // limit of bytes to use per universe, set by config
    uint16_t        dlen;        // current total length of pixel data written in outBuffer
    uint16_t        mappedLen;   //
    uint8_t         fillMap;
    uint8_t         fullMap;
    color_mapping_e colMap;
}out_def_t;

typedef struct
{
    uint8_t clk_rdy:1;
    uint8_t sys_rdy:1;
    uint8_t idle:1;
    uint8_t dc:5;
}mimas_state_t;

typedef struct
{
    char                if_name[32];
    char                ip_str[32];
    char                mac_str[32];
    char                ssid1[32];
    char                ssid2[32];
    in_addr_t           ip_bytes;
    struct sockaddr_in  sockaddr;
    uint8_t             mac[8];
    short               flags;
    int                 ifindex;
    char                isWireless:1;

}node_interfaces_detail_t;

typedef struct {
    node_interfaces_detail_t    ifs[16];
    uint8_t                     if_count;
    uint8_t                     curr_if_idx;
}node_interfaces_t;


typedef struct sockaddr_in sock_info_t;
typedef uint16_t addressing_t;
typedef enum
{
    no_sync_yet = 0,
    sync_ok,
    sync_consumed
}artNetSyncState_e;
typedef struct
{
	sm_state_e		state, prev_state;
	struct{
		uint16_t has_seq:1;
		uint16_t uses_seq:1;
		uint16_t duplicate:1;
		uint16_t minuniflag:1;
		uint16_t SockOpen:1;
		uint16_t DataOk:1;
		uint16_t hasSync:1;
		uint16_t usesSync:1;
		uint16_t dc:8;
	};
	addressing_t	startNet;
	addressing_t    endNet;
	uint64_t		curr_map;
	uint64_t		expected_full_map;
	uint8_t			cur_seq;
	uint8_t         prev_seq;
	uint8_t         universes_count;
	uint8_t         min_uni;
	uint8_t         active_unis;
	uint8_t         sync_missing_cnt;
	artNetSyncState_e syncState;
}sm_t; /* artnet state machine struct */

typedef struct
{
    sm_t            sm;
    uint16_t        sockfd;
    protoSelect_e   prot;
    sock_info_t     mySock;
    pthread_t       con_tid;
    pthread_t       prod_tid;
    addressing_t    art_start_uni;
    uint8_t         intLimit;
    uint8_t         universe_count;
    uint8_t         current_if_idx;
    node_interfaces_t ifs;
    char            nodeName[16];
    char            userName[16];
    char            longName[64];
    out_def_t       *outs[GLOBAL_OUTPUTS_MAX];
    uint32_t all;
    uint32_t miss;
    uint32_t packetsCnt;
    float       fps;
    uint32_t    frames;
    uint32_t    barr[8];
    struct timespec last_rx;
    struct timespec last_pac_proc;
    struct timespec last_mimas_ref;
}node_t;

#include "artNet.h"
#include "sacn.h"

#define MBYTES ( 1024ul * 1024ul)

typedef union
{
	art_net_pack_t      art;
	e131_pack_t         sacn;

}any_prot_pack_t;

typedef union
{
    art_net_net_t anet;
    addressing_t  addr;
}gen_addres_u;

typedef struct
{
    struct sockaddr_in  sender;
    any_prot_pack_t     pl; //payload
}peer_pack_t   __attribute__ ((aligned(64)));


#define EV_Q_DEPTH  8192

typedef enum
{

    cons_msg_pop,
    cons_msg_proced,
    cons_pack_full,
    cons_mimas_start,
    cons_mimas_refreshed,
    prod_rx_msgs
}event_type_e;

typedef struct
{
    uint32_t        seq;
    struct timespec ts;
    time_t          ts2;
    event_type_e    ev;
    union{
    uint16_t        art_addr;
    uint32_t        msg_cnt;
    };
}trace_msg_t;

typedef struct  pix_map__t* pixmap_t;

typedef struct pix_map__t
{
    color_mapping_e colMap;
   // addressing_t    Universe;
    uint16_t        startCh;
    uint16_t        outByteOffset;
    uint8_t         pixCount;
    uint8_t         outId;
    pixmap_t        nxt;
}pix_map_t;

#include "rq.h"
typedef struct post_box_t* postbox;
typedef struct
{
    node_t*          artnode;
    postbox          artPB;
}app_node_t;


#endif // TYPE_DEFS_H_INCLUDED
