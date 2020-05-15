#include "vdevs.h"
#include "type_defs.h"


mimas_dev_t     mimas_devices;
vdevs_t         devList;


const pwm_cfg_limits_t pwm_def_mimits_c = {.minV = 0u, .midV = 0x7FFF, .maxV = 0xFFFFu };
const pwm_cfg_t pwm_def_cfg_c={ .lim = {pwm_def_mimits_c, pwm_def_mimits_c, pwm_def_mimits_c, pwm_def_mimits_c, pwm_def_mimits_c, pwm_def_mimits_c, pwm_def_mimits_c, pwm_def_mimits_c} , .grpCfg.gctrl = 1, .grpCfg._16bits=1, .grpCfg.bswap=1 };

pwm_out_dev_t* getMimasPwmDevices( void)
{
    return(&mimas_devices.pwms[0]);
}
streamer_vdev_u* getMimasStreamerDevices(void)
{
    return(&mimas_devices.streamers[0]);
}
void init_mimas_vdevs(void)
{
    memset((void*)&devList, 0, sizeof(vdevs_t));
    devList.min_addr = 0xFFFF;
    devList.max_addr = 0;
    devList.next_free_addr= 0;
    memset((void*)&mimas_devices, 0, sizeof(mimas_dev_t));
    mimas_devices.pwms[0].cfg = pwm_def_cfg_c;
    mimas_devices.pwms[1].cfg = pwm_def_cfg_c;
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
        if(devList.devs[i].dev_com.dev_type == unused_dev)return(i);
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

    devList.devs[idx].dev_com.start_address = wsdev->com.start_address;
    if(wsdev->com.start_address == 0xFFFF)
    {
        wsdev->com.start_address = devList.next_free_addr;
    }
    tmpAddr = wsdev->com.start_address;

    devList.devs[idx].dev_com.dev_type = ws_pix_dev;
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

    devList.devs[idx].sub_dev_cnt = out_need;
    devList.devs[idx].uni_count = uni_need;
    devList.devs[idx].pixel_count =  wsdev->pixel_count;
    devList.devs[idx].pix_devs = (pix_dev_pt*)calloc(out_need, sizeof(void*));
    ws_pix_vdev_t* ws = &mimas_devices.streamers[0].ws_pix;
    if(devList.min_addr>tmpAddr)devList.min_addr = tmpAddr;
    for(i=0;i<MIMAS_STREAM_OUT_CNT;i++)
    {
        if(ws->com.dev_type == unused_dev)
        {
            devList.devs[idx].pix_devs[res]=  ws;

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
    tmpAddr = tmpAddr==0 ?0:tmpAddr-1;
    devList.devs[idx].dev_com.end_address = tmpAddr;
    if(devList.max_addr<tmpAddr)devList.max_addr = tmpAddr;
    devList.next_free_addr = tmpAddr+1;
    return(idx);
}

int build_dev_pwm(pwm_vdev_t* pwmdev, pwm_cfg_t*  cfg)
{
    int idx;

    idx = findFreeDevListSlot();
    if(idx == -1)return(-1);
    volatile int k , i, offset, need,res;
    pwm_out_dev_t* p;
    addressing_t tmpAddr;
    uint16_t best_choise  = 0;
    uint16_t best_start_idx = 0;
    uint16_t free_bm;
    need = pwmdev->ch_count;
    uint16_t tst_bm = BIT16(need)-1u;
    uint16_t t;
    free_bm =~( (uint16_t)(mimas_devices.pwms[0].usage_bm) | (uint16_t)(mimas_devices.pwms[1].usage_bm<<8));


    for(i=0;i< MIMAS_PWM_OUT_CNT;i++)
    {
        t = free_bm & tst_bm;
        t>>=i;
        for(k=0;k<need;k++)
        {
            if(!(t & 1))break;
            t>>=1;
        }
        if(k>=need)
        {
            best_choise = need;
            best_start_idx = i;
            break;
        }
        if(k>best_choise)
        {
            best_choise = k;
            best_start_idx = i;
        }
        tst_bm<<=1;
    }
    if(best_choise==0)
    {
        return(-2);
    }
    devList.devs[idx].dev_com = pwmdev->com;
    if(pwmdev->com.start_address == 0xFFFF)
    {
        pwmdev->com.start_address = devList.next_free_addr;
        devList.next_free_addr++;
    }
    tmpAddr = pwmdev->com.start_address;
    offset = pwmdev->com.start_offset;
    devList.devs[idx].dev_com.vdevIdx = idx;
    devList.devs[idx].dev_com.dev_type = pwm_dev;
    devList.devs[idx].dev_com.start_address = tmpAddr;
    devList.devs[idx].dev_com.end_address = tmpAddr;
    devList.devs[idx].chann_count = pwmdev->ch_count;
    if(tmpAddr<devList.min_addr)devList.min_addr = tmpAddr;
    if(tmpAddr>devList.max_addr)devList.max_addr = tmpAddr;

    devList.devs[idx].sub_dev_cnt = 0; /*init at 0, update during allocation */
    devList.devs[idx].uni_count = 1;
    devList.devs[idx].pwm_vdev = (pwm_vdev_t*)malloc(sizeof(pwm_vdev_t));
    devList.devs[idx].pwm_vdev->chan_data = (pwm_ch_data_t*)calloc(need, sizeof(void*));
    devList.devs[idx].pwm_vdev->lims = (pwm_cfg_limits_t*)calloc(need, sizeof(void*));
    devList.devs[idx].pwm_vdev->com.dev_type = pwm_dev;
    devList.devs[idx].pwm_vdev->com.start_address = offset;
    devList.devs[idx].sub_dev_cnt = 1;
    k = best_start_idx%MIMAS_PWM_OUT_PER_GRP_CNT;
    for(i=0;i<best_choise;i++)
    {
        res = k & (BIT16(MIMAS_PWM_OUT_PER_GRP_CNT) - 1u);
        p = &mimas_devices.pwms[ (i+ best_start_idx)/MIMAS_PWM_OUT_PER_GRP_CNT];
        if(p->com.dev_type == unused_dev)
        {
            p->com.dev_type = pwm_dev;
        }
        if(cfg!=NULL)
        {
            p->cfg.lim[res] = cfg->lim[i];
            p->cfg.grpCfg.bswap = cfg->grpCfg.bswap;
            p->cfg.grpCfg._16bits = cfg->grpCfg._16bits;
        }
        p->ch_data[res].bswap = p->cfg.grpCfg.bswap;
        p->ch_data[res]._16bits = p->cfg.grpCfg._16bits;
        devList.devs[idx].pwm_vdev->chan_data[i] = &p->ch_data[res];
        devList.devs[idx].pwm_vdev->lims[i] = &p->cfg.lim[res];
        p->usage_bm|=BIT8(res);

        offset+=p->cfg.grpCfg._16bits?2:1;
        devList.devs[idx].pwm_vdev->ch_count++;
        k++;
    }
    offset--;
    devList.devs[idx].pwm_vdev->com.end_address = offset;
    if( (i+best_start_idx) > MIMAS_PWM_OUT_PER_GRP_CNT) devList.devs[idx].sub_dev_cnt++;
    *pwmdev = *devList.devs[idx].pwm_vdev;
    return(idx);
    // old code that finds as many free channels as possible even if they are not consequent
    for(i=0;i<MIMAS_PWM_GROUP_CNT;i++)
    {
        devList.devs[idx].sub_dev_cnt++;
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
                    if(cfg!=NULL)
                    {
                        p->cfg.lim[k] = cfg->lim[res];
                    }
                    devList.devs[idx].pwm_vdev->chan_data[res++] = &p->ch_data[k];
                    p->usage_bm|=BIT8(k);
                    devList.devs[idx].pwm_vdev->com.dev_type = pwm_dev;
                    devList.devs[idx].pwm_vdev->com.start_address = tmpAddr;
                    offset+=p->cfg.grpCfg._16bits?2:1;
                    devList.devs[idx].pwm_vdev->ch_count++;
                }
                if(need==0)break;
                m<<=1;
            }
        }
        if(need==0)break;
    }
    pwmdev->com.start_offset = offset;// autoupdate offset to next free(?) dmx channel

    /*
    for(i=0;i<16;i++)
    {
        printf("free %d, need %d , and at %d:%d\n", \
        __builtin_popcount(free_bm),__builtin_popcount(tst_bm) ,i,__builtin_popcount(free_bm & tst_bm));
        if(( free_bm & tst_bm) == tst_bm)break;
        if( (free_bm & tst_bm) < best_choise )
        {
            best_start_idx = i;
            best_choise = ( free_bm & tst_bm) ;
        }
        tst_bm<<=1;
    }
*/

    return(idx);
}
/*
uint16_t getNextDmxChanAtAddr(addressing_t adr)
{
    uint16_t res = 0;
    int i;
    for(i=0;i<MAX_VDEV_CNT;i++)
    {
        if(devList.devs[i].dev_com.dev_type != unused_dev)
        {
            if((devList.devs[i].dev_com.start_address == adr) && ( (devList.devs[i].dev_com.dev_type ==pwm_dev) || (devList.devs[i].dev_com.dev_type == dmx_out_dev)  ))
            {
              if(devList.devs[i].pwm_vdev.com.start_offset + devList.devs[i].pwm_vdev.ch_count  * devList.devs[i].pwm_vdev.)
            }
        }
    }
}
*/
int findVDevAtAddr(addressing_t adr)
{
    int i;
    for(i=0;i<MAX_VDEV_CNT;i++)
    {
        if(devList.devs[i].dev_com.dev_type != unused_dev)
        {
            if((devList.devs[i].dev_com.end_address>= adr) && (devList.devs[i].dev_com.start_address<= adr))return(i);
        }
    }
    return(-1);
}


int findVDevsAtAddr(addressing_t adr, int* idxs)
{
    int i, count;
    count = 0;
    for(i=0;i<MAX_VDEV_CNT;i++)
    {
        if(devList.devs[i].dev_com.dev_type != unused_dev)
        {
            if((devList.devs[i].dev_com.end_address>= adr) && (devList.devs[i].dev_com.start_address<= adr))
            {
                idxs[count] = i;
                count++;
            }
        }
    }
    return(count);
}


int findVDevsOfType(vdevs_e typ, int* idxs)
{
    int i, count;
    count = 0;
    for(i=0;i<MAX_VDEV_CNT;i++)
    {
        if(devList.devs[i].dev_com.dev_type == typ)
        {
            idxs[count++] = i;
        }
    }
    return(count);
}

#define PEER_IP_ONLY 1
int vDevSetPeer(uint64_t peer,int vDevId)
{
mimas_out_dev_t* vDev  = &devList.devs[vDevId];

#ifdef PEER_IP_ONLY
peer = (peer>>=16) & 0xFFFFFFFFul;
#endif
    if(vDev->peer_id != peer)
    {
        if(vDev->peer_state == 0)
        {
            vDev->peer_state = 1;
            printf("PeerId Set : vDev %d  id %lX\n", vDevId,  peer);
        }
        else
        {
            printf("PeerId change : vDev %d old id %lX new id %lX\n", vDevId, (uint64_t)vDev->peer_id, peer);
        }
        vDev->peer_id = peer;
    }
}

int getPwmCfg(pwm_vdev_t* vDev, uint8_t ch, pwm_cfg_t** cfg)
{
    if(vDev->com.dev_type!=pwm_dev)return(-2); // error wrong vDev type
    pwm_out_dev_t* pt = GET_PWMS_PTR;
    if(pt!=NULL)
    {
       *cfg = &(pt[vDev->chan_data[ch]->phyGrp].cfg);
       return(0);
    }
    else
    {
        *cfg = NULL;
        return(-1);
    }
}

int setPwmLimCfg(pwm_vdev_t* vDev, uint8_t ch, pwm_cfg_t* cfg)
{
    if(vDev->com.dev_type!=pwm_dev)return(-2); // error wrong vDev type
    pwm_out_dev_t* pt = GET_PWMS_PTR;
    if(pt!=NULL)
    {
       //pt[vDev->chan_data[ch]->phyGrp].cfg.lim[vDev->chan_data[ch]->phyIdx] = cfg->lim[ch];
       *vDev->lims[ch] = cfg->lim[ch];
       return(0); // success
    }
    return(-1); // internal error
}

int setPwmVal(pwm_vdev_t* vDev, uint8_t ch, uint16_t val)
{
    if(vDev->com.dev_type!=pwm_dev)return(-2); // error wrong vDev type
    vDev->chan_data[ch]->chV = val;
    return(0);
}

uint16_t pwmMapValue(pwm_vdev_t* vDev, uint8_t ch, uint8_t* val)
{
    if(vDev->com.dev_type!=pwm_dev)return(-2); // error wrong vDev type
    uint32_t tv;
    if(vDev->chan_data[ch]->_16bits!=0)
    {
        if(vDev->chan_data[ch]->bswap) tv = bswap_16(*val);
        else  tv = *val;
    }
    else
    {
        tv = ((uint16_t)(*val)) *256;
    }
    tv *= (uint32_t)(vDev->lims[ch]->maxV - vDev->lims[ch]->minV);
    tv/=0xFFFF;
    tv+=vDev->lims[ch]->minV;
    return((uint16_t)tv);
}

inline int8_t getPwmGrp(pwm_vdev_t* vDev, uint8_t ch)
{
   if(vDev->com.dev_type!=pwm_dev)return(-1);
   return(vDev->chan_data[ch]->phyGrp);
}
inline int8_t getPwmIdx(pwm_vdev_t* vDev, uint8_t ch)
{
   if(vDev->com.dev_type!=pwm_dev)return(-1);
   return(vDev->chan_data[ch]->phyIdx);
}

void initServo(servo_t* s)
{
    float d;
    d = (float)(s->cfg.maxV - s->cfg.midV);
    s->cfg.stepHi = d/500.0f;
    d = (float)(s->cfg.midV - s->cfg.minV);
    s->cfg.stepLo = d/500.0f;
    memset((void*)&s->drv, 0, sizeof(servo_drive));
}

uint16_t servoGetPwmV(servo_t*s, uint16_t p)
{
   uint16_t r;
    if(p<500)
    {
        r = s->cfg.minV + (uint16_t)((float)p * s->cfg.stepLo);
    }
    else
    {
        p-=499;
        r = s->cfg.midV + (uint16_t)((float)p * s->cfg.stepHi);
    }
    return(r);
}
