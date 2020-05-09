#ifndef VDEVS_H_INCLUDED
#define VDEVS_H_INCLUDED
#include "type_defs.h"
#include "mimas_cfg.h"
typedef enum
{
    unused_dev,
    ws_pix_dev,
    dmx_out_dev,
    pwm_dev
}vdevs_e;

typedef struct
{
    uint16_t        pixel_count;
    color_mapping_e col_map;
    uint8_t         pix_per_uni;
    addressing_t    start_address;
    uint8_t         out_start_id;
}ws_pix_vdev_t;

typedef struct
{
    uint16_t        channel_count;
    addressing_t    start_address;
    uint8_t         out_start_id;
}dmx_vdev_t;


typedef struct
{
    vdevs_e dev_type;
    ws_pix_vdev_t*  pix_dev;
    dmx_vdev_t*     dmx_dev;
}mimas_streamer_out_dev_t;

typedef struct
{
    uint16_t    gperiod;
    uint8_t     gctrl;
    uint8_t     div;
    uint16_t    chPeriod[4];
    uint8_t     chCtrl[4];
}mimas_pwm_out_dev_t;

typedef struct
{
    mimas_streamer_out_dev_t streamers[MIMAS_STREAM_OUT_CNT];
    mimas_pwm_out_dev_t;
}mimas_dev_t;

void build_dev_ws(ws_pix_vdev_t* wsdev);

#endif // VDEVS_H_INCLUDED
