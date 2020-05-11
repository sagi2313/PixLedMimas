#ifndef VDEVS_H_INCLUDED
#define VDEVS_H_INCLUDED
#include "type_defs.h"
#include "mimas_cfg.h"

#define MAX_VDEV_CNT    32
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
    uint8_t         phyIdx:3;
    uint8_t         phyGrp:1;
    uint16_t        chPeriod;
    union{
        uint8_t chCtrl;
        struct{
            uint8_t enabled:1;
            uint8_t reversed:1;
        };
    };
}pwm_ch_data_t;

typedef struct
{
    vdev_common_t   com;
    uint8_t         usage_bm;
    uint8_t         div;
    uint8_t         gctrl;
    uint16_t        gperiod;
    pwm_ch_data_t   ch_data[MIMAS_PWM_OUT_PER_GRP_CNT];
}pwm_out_dev_t;


typedef struct
{
    vdev_common_t   com;
    uint8_t         ch_count;
    pwm_ch_data_t**  chan_data;
}pwm_vdev_t;

typedef ws_pix_vdev_t* pix_dev_pt;

typedef struct
{
    vdev_common_t     dev_com;

    union{
        vdev_common_t*      sub_com;
        pix_dev_pt*         pix_devs;
        dmx_vdev_t*         dmx_dev;
        pwm_vdev_t*         pwm_dev;
    };

        uint8_t           sub_dev_cnt;
        uint8_t           uni_count;
        union{
        uint16_t    pixel_count;
        uint16_t    chann_count;
    };
}mimas_out_dev_t;


typedef struct
{
    streamer_vdev_u streamers[MIMAS_STREAM_OUT_CNT];
    pwm_out_dev_t   pwms[MIMAS_PWM_GROUP_CNT];
}mimas_dev_t;

extern mimas_out_dev_t devList[];
int build_dev_pwm(pwm_vdev_t* pwmdev);
int build_dev_ws(ws_pix_vdev_t* wsdev);
int findVDevAtAddr(addressing_t adr);

#endif // VDEVS_H_INCLUDED
