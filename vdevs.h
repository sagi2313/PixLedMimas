#ifndef VDEVS_H_INCLUDED
#define VDEVS_H_INCLUDED
#include "type_defs.h"
#include "mimas_cfg.h"
#include "gen_lists.h"
#include <byteswap.h>
#define MAX_VDEV_CNT    (MIMAS_PWM_OUT_CNT + MIMAS_STREAM_OUT_CNT)
#include "bm_handling.h"


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
	addressing_t    min_uni;            /*minimum detected address */
	uint8_t         unis_cfg;           /*total configured universe count */
	uint8_t         active_unis;        /*total detected univese count */
	uint8_t			cur_seq;
	uint8_t         prev_seq;
	uint8_t         sync_missing_cnt;
    uint64_t		curr_map;
	uint64_t		expected_full_map;
	artNetSyncState_e syncState;
	struct timespec last_upd_ts;
}vdev_sm_t; /* vdev state machine struct */

typedef enum
{
    unused_dev = 0,
    ws_pix_dev = 1,
    dmx_out_dev = 2,
    pwm_dev = 4
}vdevs_e;

typedef struct
{
    vdevs_e           dev_type;
    addressing_t      start_address;
    addressing_t      end_address;
    uint16_t          start_offset;
    uint16_t          end_offset;
    vdev_sm_t         vdsm;
    int8_t            vDevIdx;
}vdev_common_t;

typedef struct
{
    vdev_common_t       com;
    uint16_t            pixel_count;
    color_mapping_e     col_map;
    pixel_col_vec_e     colCnt;
    stream_proto_type_e strm_proto;
    uint8_t             pix_per_uni;
    uint8_t             out_start_id;
}ws_pix_vdev_t;

#define PIX_COL_MAP_DEF (rgb_map_e)
#define PIX_VEC_CNT_DEF (pix_3_vec)

typedef struct
{
    vdev_common_t   com;
    uint16_t        channel_count;
    uint8_t         out_start_id;
}dmx_vdev_t;

typedef union
{
    vdev_common_t   com;
    dmx_vdev_t      dmx;
    ws_pix_vdev_t   ws_pix;
}streamer_vdev_u;

typedef struct
{
    uint16_t        minV;
    uint16_t        midV;
    uint16_t        maxV;
}pwm_cfg_limits_t;

typedef struct
{
    uint16_t        gPeriod;
    uint8_t         gDiv;
    uint8_t         gctrl:1;
}pwm_grp_cfg_t;


typedef struct
{
    uint16_t            chV;
    pwm_cfg_limits_t    lims;
    union
    {
        uint8_t     chCtrl;
        struct
        {
            uint8_t phyIdx:3;
            uint8_t phyGrp:1;
            uint8_t enabled:1;
            uint8_t reversed:1;
            uint8_t _16bits:1;
            uint8_t bswap:1;
        };
    };
}pwm_ch_data_t;


typedef struct
{
    vdev_common_t       com;
    int8_t              hwGrpIdx:MIMAS_PWM_GROUP_CNT+1;
    int8_t              hwStartIdx:4;
    int8_t              ch_count;
    pwm_ch_data_t       chCfg[MIMAS_PWM_OUT_PER_GRP_CNT];
}pwm_cfg_t;

/* PWM HW device descriptor */
typedef struct
{
    //vdev_common_t   com;
    uint8_t         gEnable:1;
    uint8_t         usage_bm;
    uint8_t         div;
    uint16_t        gperiod;
    pwm_ch_data_t   ch_data[MIMAS_PWM_OUT_PER_GRP_CNT];
}pwm_out_dev_t;

/* PWM vDev descriptor */
typedef struct
{
    vdev_common_t       com;
    uint8_t             ch_count;
    uint8_t             hwStartIdx;
}pwm_vdev_t;

typedef ws_pix_vdev_t* pix_dev_pt;

typedef struct
{
    vdev_common_t       dev_com;
    union
    {
        vdev_common_t*      sub_com;
        pix_dev_pt*         pix_devs;
        dmx_vdev_t*         dmx_dev;
        pwm_vdev_t*         pwm_vdev;
    };
    uint64_t            peer_id:48;
    uint64_t            peer_state:2; /* never set=0, active = 1 */
    uint64_t            peer_dc:14;
    uint8_t             sub_dev_cnt;
    uint8_t             uni_count;
    union
    {
        uint16_t    pixel_count;
        uint16_t    chann_count;
    };
}mimas_out_dev_t;


typedef struct
{
    streamer_vdev_u streamers[MIMAS_STREAM_OUT_CNT];
    pwm_out_dev_t   pwms[MIMAS_PWM_GROUP_CNT];
}mimas_dev_t;


typedef struct
{
    uint16_t        Start;
    uint16_t        End;
    uint32_t        owner_vdevIdx;
}dmx_chan_range_t;

typedef struct __dbg_dmx_chan_ranges_list *dbg_dmx_range;
typedef struct __dbg_dmx_chan_ranges_list
{
    dmx_chan_range_t*   data;
    dbg_dmx_range         nxt;
}dbg_dmx_chan_ranges_list;


typedef struct
{
    addressing_t    address;        // the address of this universe
    uint32_t        owner_vdevIdx;  // bitmap of vDevs getting input from this universe
    uint16_t        nxt_free;       // this is the next dmx channel free in the given universe
    //union
    //{
        //uint8_t         bm[64];
        struct
        {
            uint16_t        items; // num of items in the dmx ranges list
            union
            {
                ln_t            ranges;
                dbg_dmx_range   dbg;
            };
        };
    //};
}dmx_chan_usage_t;

typedef struct __dbg_dmx_chan_usage_list *dbg_dmx_use;
typedef struct __dbg_dmx_chan_usage_list
{
    dmx_chan_usage_t*   data;
    dbg_dmx_use         nxt;
}dbg_dmx_chan_usage_list;

typedef struct
{
    addressing_t min_addr;
    addressing_t max_addr;
    uint16_t     mapped_addresses;
    addressing_t next_free_addr;
    int_fast8_t  last_used_idx;
    mimas_out_dev_t devs[MAX_VDEV_CNT];
    union
    {
        ln_t            addr_usage;
        dbg_dmx_use     dbg_list; // full map of addresses and dmx ranges per address, that are currently reserved
    };
    bm_t*   glo_uni_map;
    bm_t*   tmp_uni_map;
}vdevs_t;

#define MAPPED_UNIS( D )    (D.mapped_addresses)
#define RCVED_UNIS( D )     (D.glo_uni_map->reserved)

extern vdevs_t devList;
extern mimas_dev_t     mimas_devices;
extern const pwm_cfg_limits_t pwm_def_limits_c;
extern const pwm_ch_data_t    pwm_def_chan_c;
extern const pwm_cfg_t pwm_def_cfg_c;

void init_mimas_vdevs(void);
pwm_out_dev_t* getMimasPwmDevices( void);
streamer_vdev_u* getMimasStreamerDevices( void);
int build_dev_pwm(pwm_vdev_t* pwmdev, pwm_cfg_t*  cfg);
int build_dev_ws(ws_pix_vdev_t* wsdev);
int build_dev_dmx(dmx_vdev_t* dmxdev);
int findVDevAtAddr(addressing_t adr);
int findVDevsAtAddr(addressing_t adr, int* idxs);
int findVDevsOfType(vdevs_e typ, int* idxs);
int vDevSetPeer(uint64_t,int);
int getPwmCfg(pwm_vdev_t* vDev, uint8_t ch, pwm_cfg_t** cfg);
int setPwmLimCfg(pwm_vdev_t* vDev, uint8_t ch, pwm_cfg_t* cfg);
int setPwmVal(pwm_vdev_t* vDev, uint8_t ch, uint16_t val);
uint16_t pwmMapValue(pwm_vdev_t* vDev, uint8_t ch, uint8_t* val);
inline int8_t getPwmGrp(pwm_vdev_t* vDev, uint8_t ch);
inline int8_t getPwmIdx(pwm_vdev_t* vDev, uint8_t ch);
int addAddrUsage(addressing_t artAdr, uint16_t start,  uint16_t End, uint32_t owner);
uint16_t getNextFreeChAtAddr(addressing_t adr);
int checkfDmxChIsFree(addressing_t adr, uint16_t chan);
int checkfDmxRangeIsFree(addressing_t adr, uint16_t Start, uint16_t End);
uint32_t getUsersOfAddr(addressing_t adr);
#define GET_VDEV_TYPE( D )  ((vdevs_e)(*((vdevs_e*)(&D))))
#define GET_PWMS_PTR  (&mimas_devices.pwms[0])


typedef struct
{
    uint16_t    minV;
    uint16_t    midV;
    uint16_t    maxV;
    uint8_t     rev;
    uint8_t     dsable;
    float       stepLo;
    float       stepHi;
}servoCfg_t;
typedef struct
{
    uint16_t sPos;
    uint16_t cPos;
    uint16_t ePos;
    uint32_t tTim;
    uint32_t cTim;
}servo_drive;

typedef struct
{
    servoCfg_t  cfg;
    servo_drive drv;
}servo_t;
/*
            uint8_t enabled:1;
            uint8_t reversed:1;
            uint8_t _16bits:1;
            uint8_t bswap:1;
*/
#define PWM_CH_EN   0x10
#define PWM_CH_RV   0x20
#define PWM_CH_16B  0x40
#define PWM_CH_BSW  0x80
//#define GET_VDEV_TYPE( D )  ((vdevs_e)D)
#endif // VDEVS_H_INCLUDED
