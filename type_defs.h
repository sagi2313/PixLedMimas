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

#include <time.h>
#include <linux/wireless.h>
#include <linux/if_ether.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <bcm2835.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint-gcc.h>
#include <stdint.h>
#include "sys_defs.h"
#include "mimas_cfg.h"
//#include "protocolCommon.h"

#ifndef  BIT64
#define BIT64(B)  ( (uint64_t)( ((uint64_t)1ULL)<<((uint64_t)(B)) ) )
#endif

#ifndef  BIT32
#define BIT32(B)  ( (uint32_t)( ((uint32_t)1U)<<((uint32_t)(B)) ) )
#endif

#ifndef  BIT16
#define BIT16(B)  ( (uint16_t)( ((uint16_t)1U)<<((uint16_t)(B)) ) )
#endif

#ifndef  BIT8
#define BIT8(B)  ( (uint8_t)( ((uint32_t)1U)<<((uint32_t)(B)) )  & 0xFF)
#endif

typedef enum
{
    stream_proto_nrz_e,
    stream_proto_spi_e
}stream_proto_type_e;

typedef enum
{
    rgb_map_e=0,
    grb_map_e,
    rbg_map_e,
    gbr_map_e,
    brg_map_e,
    bgr_map_e,
    wrgb_map_e,
    wgrb_map_e,
    wrbg_map_e,
    wgbr_map_e,
    wbrg_map_e,
    wbgr_map_e,
    rgbw_map_e,
    grbw_map_e,
    rbgw_map_e,
    gbrw_map_e,
    brgw_map_e,
    bgrw_map_e,
}color_mapping_e;

typedef enum
{
    pix_0_vec, // undefined, use default
    pix_1_vec,
    pix_2_vec,
    pix_3_vec,
    pix_4_vec,
    pix_5_vec
}pixel_col_vec_e;


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

#pragma pack(1)
typedef struct
{
    uint8_t dmx_data[512];
}dmx_uni_out_t;


typedef struct
{
    uint8_t mimas_header[6];
    union{
    dmx_uni_out_t dmxp[UNI_PER_OUT];
    uint8_t       raw_buf[UNI_PER_OUT * 512];
    uint8_t       raw_unis[UNI_PER_OUT][512];
    };
}mimaspack_t;

typedef union
{
    uint8_t raw;
    struct{
    uint8_t clk_spd:5;
    uint8_t rst_dur:3;
    };
}stream_cfg_t;

typedef struct
{
    uint8_t             *wrPt[UNI_PER_OUT];
    uint16_t            uniLenLimit[UNI_PER_OUT]; // limit of bytes to use per universe, set by config
    uint16_t            dlen;        // current total length of pixel data written in outBuffer
    uint16_t            mappedLen;   //
    uint8_t             fillMap;
    uint8_t             fullMap;
    color_mapping_e     colMap;
    stream_proto_type_e proto;
    stream_cfg_t        cfg;
    mimaspack_t         mpack;
}out_def_t;
#pragma pack()
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
	addressing_t	startNet;           /*minimum configured address */
	addressing_t    endNet;             /*maximum configured address */
	addressing_t    min_uni;            /*minimum detected address */
	uint8_t         universes_count;    /*total configured universe count */
	uint8_t         active_unis;        /*total detected univese count */
	uint8_t			cur_seq;
	uint8_t         prev_seq;
	uint8_t         sync_missing_cnt;
    uint64_t		curr_map;
	uint64_t		expected_full_map;
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
    uint8_t         intLimit; // intensity limit
    uint8_t         current_if_idx;
    node_interfaces_t ifs;
    char            nodeName[16];
    char            userName[16];
    char            longName[64];
    out_def_t       *outs[MIMAS_STREAM_OUT_CNT];
    uint32_t        all;
    uint32_t        miss;
    uint32_t        packetsCnt;
    float           fps;
    uint32_t        frames;
    struct timespec last_rx;
    struct timespec last_pac_proc;
    struct timespec last_mimas_ref;
}node_t;

#include "artNet.h"
#include "sacn.h"

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

typedef enum
{
    msg_typ_socket_data=1,
    msg_typ_socket_ntfy,
    msg_typ_sys_event,
    msg_typ_pwm_cmd
}msg_type_e;

typedef enum
{
    sys_ev_socket_timeout,
    sys_ev_socket_closed,
    sys_ev_socket_opened,
}sys_ev_e;

typedef struct
{
    msg_type_e  mtyp;
    sys_ev_e ev_type;
    uint32_t    data1;
}sys_event_msg_t;

typedef enum
{
    pwm_set_gper_cmd = 1,
    pwm_set_gctrl_cmd =2,
    pwm_set_div_cmd = 4,
    pwm_set_ch_per_cmd = 8,
    pwm_set_ch_ctrl_cmd = 16,
    pwm_override_artnet = 32
}pwm_cmd_type_e;
#define ALL_PWM_CMDS (63)

typedef struct
{
    uint8_t         startUpdIdx:3;
    uint8_t         UpdChCount:3;
    uint8_t         needUpdate:1;
    uint8_t         gctrl:1;
    uint8_t         div;
    uint16_t        gper;
    uint16_t        ch_pers[MIMAS_PWM_OUT_PER_GRP_CNT];
    uint8_t         ch_ctrls[MIMAS_PWM_OUT_PER_GRP_CNT];
    uint8_t         valid_ch_bm;
    uint32_t         sleep_time[MIMAS_PWM_OUT_PER_GRP_CNT];
    uint32_t         sleep_count[MIMAS_PWM_OUT_PER_GRP_CNT];
}pwm_group_data_t;

typedef struct{
    msg_type_e      mtyp;
    pwm_cmd_type_e  cmd;
    uint8_t         grp_bm;
    uint8_t         start_ch[MIMAS_PWM_GROUP_CNT];
    uint8_t         ch_count[MIMAS_PWM_GROUP_CNT];
    pwm_group_data_t data[MIMAS_PWM_GROUP_CNT];
}pwm_cmd_msg_t;

typedef struct
{
    msg_type_e  mtyp;
    void        *rq_owner;
    void        *datapt;
}sockdat_ntfy_t;

typedef struct
{
    msg_type_e          mtyp;
    struct sockaddr_in  sender;
    uint32_t            length;
    any_prot_pack_t     pl; //payload
}sock_data_msg_t;

typedef struct
{
    union
    {
        msg_type_e              genmtyp;
        sock_data_msg_t         artn_msg;
        sys_event_msg_t         sys_ev;
        pwm_cmd_msg_t           pwm_msg;
        sockdat_ntfy_t          dataNtfy;
    };
}peer_pack_t   __attribute__ ((aligned(64)));

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
/*
typedef enum
{
    dev_unknown=0,
    dev_pixels,
    dev_pwm,
    dev_dmx

}device_type_e;


typedef struct
{
    //mimas_device_t* mimasdev;
    uint16_t        startCh;
    uint16_t        endCh;
    uint16_t        channel_count;
    void*           write_func;
    void*           refresh_func;
    color_mapping_e colMap;
}pixel_dev_t;


typedef struct
{
    //mimas_device_t* mimasdev;
    uint16_t        startCh;
    uint16_t        endCh;
    uint16_t        channel_count;
}pwm_dev_t;



typedef struct  art_map__t* artmap_t;

typedef struct art_map__t
{

    device_type_e   dev_type;
    uint16_t        startCh;
    uint16_t        endCh;
    uint16_t        channel_count;
    union{
        pwm_dev_t   pwm_dev;
        pixel_dev_t pix_dev;
    };
    union{
        uint16_t        dev_id;
        struct{
            uint16_t    outId:3;
            uint16_t    outCh:5;
            uint16_t    devSel:8;
        };
    };
    artmap_t        nxt;
};
*/

#include "rq.h"
typedef struct post_box_t* postbox;
typedef struct
{
    node_t*          artnode;
    postbox          artPB;
}app_node_t;


#endif // TYPE_DEFS_H_INCLUDED
