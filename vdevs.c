#include "vdevs.h"
#include "type_defs.h"


mimas_dev_t     mimas_devices;
mimas_out_dev_t devList[MAX_VDEV_CNT];
addressing_t    next_free_addr=0;

void init_mimas_vdevs(void)
{
    memset((void*)devList, 0, MAX_VDEV_CNT * sizeof(mimas_out_dev_t));
    memset((void*)&mimas_devices, 0, sizeof(mimas_dev_t));
    next_free_addr = 0;
    int i;
    for(i=0;i<MIMAS_PWM_OUT_PER_GRP_CNT;i++)
    {
        mimas_devices.pwms[0].ch_data[i].phyGrp = 0;
        mimas_devices.pwms[0].ch_data[i].phyIdx = i;
        mimas_devices.pwms[1].ch_data[i].phyGrp = 1;
        mimas_devices.pwms[1].ch_data[i].phyIdx = i;
    }
}
int findFreeDevListSlot(void)
{
    int i;
    for(i=0;i<MAX_VDEV_CNT;i++)
    {
        if(devList[i].dev_com.dev_type == unused_dev)return(i);
    }
    return(-1);
}
int build_dev_ws(ws_pix_vdev_t* wsdev)
{
    if((wsdev->pix_per_uni==0)||(wsdev->pix_per_uni>170))
    {
        wsdev->pix_per_uni = PIX_PER_UNI;
    }
    uint32_t uni_need = wsdev->pixel_count/wsdev->pix_per_uni;
    uint32_t out_need, res;
    addressing_t tmpAddr;
    int idx;
    idx = findFreeDevListSlot();
    if(idx == -1)return(-1);

    devList[idx].dev_com.start_address = wsdev->com.start_address;
    if(wsdev->com.start_address == 0xFFFF)
    {
        wsdev->com.start_address = next_free_addr;
    }
    tmpAddr = wsdev->com.start_address;

    devList[idx].dev_com.dev_type = ws_pix_dev;
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
    res = 0;

    devList[idx].sub_dev_cnt = out_need;
    devList[idx].uni_count = uni_need;
    devList[idx].pixel_count =  wsdev->pixel_count;
    devList[idx].pix_devs = (pix_dev_pt*)calloc(out_need, sizeof(void*));
    ws_pix_vdev_t* ws = &mimas_devices.streamers[0].ws_pix;
    for(i=0;i<MIMAS_STREAM_OUT_CNT;i++)
    {
        if(ws->com.dev_type == unused_dev)
        {
            devList[idx].pix_devs[res]=  ws;

            ws->com.start_address = tmpAddr;
            tmpAddr+=UNI_PER_OUT;
            ws->com.end_address =tmpAddr-1;
            ws->com.dev_type = ws_pix_dev;
            ws->out_start_id = i;
            ws->col_map = wsdev->col_map;
            ws->pixel_count = wsdev->pix_per_uni * UNI_PER_OUT;
            ws->pix_per_uni = wsdev->pix_per_uni;
            out_need--;
            res++;
        }
        if(out_need==0)break;
        ws++;
    }
    devList[idx].dev_com.end_address = tmpAddr-1;
    next_free_addr = tmpAddr;
    return(idx);
}

int build_dev_pwm(pwm_vdev_t* pwmdev)
{
    int idx;
    idx = findFreeDevListSlot();
    if(idx == -1)return(-1);
    addressing_t tmpAddr;
    devList[idx].dev_com = pwmdev->com;
    if(pwmdev->com.start_address == 0xFFFF)
    {
        pwmdev->com.start_address = next_free_addr;
    }
    tmpAddr = pwmdev->com.start_address;
    pwmdev->com.dev_type = pwm_dev;
    devList[idx].dev_com.dev_type = pwm_dev;
    devList[idx].chann_count = pwmdev->ch_count;
    int i, need;
    need = pwmdev->ch_count;
    int res = 0;
    int offset = pwmdev->com.start_offset;
    devList[idx].sub_dev_cnt = 0; /*init at 0, update during allocation */
    devList[idx].uni_count = 1;
    i = sizeof(pwm_vdev_t);
    printf("sizeof(pwm_vdev_t) = %d\n",i);
    devList[idx].pwm_dev = (pwm_vdev_t*)malloc( i);
    devList[idx].pwm_dev->chan_data = (pwm_ch_data_t*)calloc(need, sizeof(void*));
    pwm_out_dev_t* p;
    for(i=0;i<MIMAS_PWM_GROUP_CNT;i++)
    {
        devList[idx].sub_dev_cnt++;
        p = &mimas_devices.pwms[i];
        if(p->usage_bm!=0xFF)
        {
            if(p->com.dev_type == unused_dev)
            {
                p->com.dev_type = pwm_dev;
            }
            int j, k, m;
            m = 1;
            /* try to find free channels*/
            for(k=0;k<MIMAS_PWM_OUT_PER_GRP_CNT;k++)
            {
                if((p->usage_bm & m) == 0)
                {
                    need--;
                    devList[idx].pwm_dev->chan_data[res++] = &p->ch_data[k];
                    p->usage_bm|=BIT8(k);
                    devList[idx].pwm_dev->com.dev_type = pwm_dev;
                    devList[idx].pwm_dev->com.start_address = tmpAddr;
                    devList[idx].pwm_dev->com.start_offset = offset++;
                    devList[idx].pwm_dev->ch_count++;
                }
                if(need==0)break;
                m<<=1;
            }
        }
        if(need==0)break;
    }


    return(idx);
}


int findVDevAtAddr(addressing_t adr)
{
    int i;
    for(i=0;i<MAX_VDEV_CNT;i++)
    {
        if(devList[i].dev_com.dev_type != unused_dev)
        {
            if((devList[i].dev_com.start_address>= adr) && (devList[i].dev_com.start_address<= adr))return(i);
        }
    }
    return(-1);
}
