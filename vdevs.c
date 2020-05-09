#include "vdevs.h"
#include "type_defs.h"


mimas_dev_t mimas_devices;

void build_dev_ws(ws_pix_vdev_t* wsdev)
{
    uint32_t uni_need = wsdev->pixel_count/wsdev->pix_per_uni;
    uint32_t out_need;
    if(wsdev->pixel_count -(uni_need * wsdev->pix_per_uni )>0)
    {
        uni_need++;
    }
    int i;
    out_need = uni_need/UNI_PER_OUT;
    if(( out_need * UNI_PER_OUT ) < uni_need)
    {
        out_need++;
    }
    mimas_streamer_out_dev_t* ws = &mimas_devices.streamers[0];
    for(i=0;i<MIMAS_STREAM_OUT_CNT;i++)
    {
        if(ws->dev_type == unused_dev)
        {
            ws->pix_dev = wsdev;
            ws->dev_type = ws_pix_dev;
            ws->pix_dev->out_start_id = i;
            out_need--;
        }
        if(out_need==0)break;
        ws++;
    }
}
