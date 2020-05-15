#ifndef VDEVS_H_INCLUDED
#define VDEVS_H_INCLUDED
#include "type_defs.h"
#include "mimas_cfg.h"

#include <byteswap.h>
#define MAX_VDEV_CNT    (MIMAS_PWM_OUT_CNT + MIMAS_STREAM_OUT_CNT)

typedef enum
{
    unused_dev = 0,
    ws_pix_dev,
    dmx_out_dev,
    pwm_dev
}vdevs_e;

typedef struct
{
    vdevs_e           dev_type;
    addressing_t      start_address;
    union{
    addressing_t      end_address;
    uint16_t          start_offset;
    };
    uint8_t           vdevIdx;
}vdev_common_t;

typedef struct
{
    vdev_common_t   com;
    uint16_t        pixel_count;
    color_mapping_e col_map;
    uint8_t         pix_per_uni;
    uint8_t         out_start_id;
}ws_pix_vdev_t;

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
    uint8_t         gctrl:1;
    uint8_t         _16bits:1;
    uint8_t         bswap:1;
}pwm_grp_cfg_t;

typedef struct
{
    pwm_cfg_limits_t    lim[MIMAS_PWM_OUT_PER_GRP_CNT];
    pwm_grp_cfg_t       grpCfg;
}pwm_cfg_t;

typedef struct
{
    uint16_t        chV;
    union
    {
        uint8_t chCtrl;
        struct
        {
            uint8_t enabled:1;
            uint8_t reversed:1;
            uint8_t _16bits:1;
            uint8_t bswap:1;
            uint8_t phyIdx:3;
            uint8_t phyGrp:1;
        };
    };
}pwm_ch_data_t;

typedef struct
{
    vdev_common_t   com;
    uint8_t         usage_bm;
    uint8_t         div;
    uint16_t        gperiod;
    pwm_cfg_t       cfg;
    pwm_ch_data_t   ch_data[MIMAS_PWM_OUT_PER_GRP_CNT];
}pwm_out_dev_t;


typedef struct
{
    vdev_common_t       com;
    uint8_t             ch_count;
    pwm_ch_data_t**     chan_data;
    pwm_cfg_limits_t**  lims;
}pwm_vdev_t;

typedef ws_pix_vdev_t* pix_dev_pt;

typedef struct
{
    vdev_common_t       dev_com;
    uint64_t            peer_id:48;
    uint64_t            peer_state:2; /* never set=0, active = 1 */
    uint64_t            peer_dc:14;
    union
    {
        vdev_common_t*      sub_com;
        pix_dev_pt*         pix_devs;
        dmx_vdev_t*         dmx_dev;
        pwm_vdev_t*         pwm_vdev;
    };

    uint8_t           sub_dev_cnt;
    uint8_t           uni_count;
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
    addressing_t min_addr;
    addressing_t max_addr;
    addressing_t next_free_addr;
    mimas_out_dev_t devs[MAX_VDEV_CNT];
}vdevs_t;



extern vdevs_t devList;
extern mimas_dev_t     mimas_devices;
void init_mimas_vdevs(void);
pwm_out_dev_t* getMimasPwmDevices( void);
streamer_vdev_u* getMimasStreamerDevices( void);
int build_dev_pwm(pwm_vdev_t* pwmdev, pwm_cfg_t*  cfg);
int build_dev_ws(ws_pix_vdev_t* wsdev);
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

//#define GET_VDEV_TYPE( D )  ((vdevs_e)D)
#endif // VDEVS_H_INCLUDED
