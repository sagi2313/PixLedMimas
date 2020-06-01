#include "vdevs.h"
#include "type_defs.h"
#include "bm_handling.h"

mimas_dev_t     mimas_devices;
vdevs_t         devList;


const pwm_cfg_limits_t pwm_def_limits_c = {.minV = 0u, .midV = 0x7FFF, .maxV = 0xFFFFu };
const pwm_ch_data_t    pwm_def_chan_c =  {.chV = 0, .lims = pwm_def_limits_c, .chCtrl = 0};
const pwm_cfg_t pwm_def_cfg_c={ .chCfg =
       {pwm_def_chan_c, pwm_def_chan_c, pwm_def_chan_c, pwm_def_chan_c,
       pwm_def_chan_c, pwm_def_chan_c, pwm_def_chan_c, pwm_def_chan_c} ,
        .ch_count=-1, .hwStartIdx=-1, .com.vDevIdx = -1,
        .com.dev_type = pwm_dev, .com.start_address = 0xFFFF, .com.end_address = 0};

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
    devList.last_used_idx= 0;
    memset((void*)&mimas_devices, 0, sizeof(mimas_dev_t));
    int i, j;
    for(j = 0;j<MIMAS_PWM_GROUP_CNT;j++)
    {
        for(i=0;i<MIMAS_PWM_OUT_PER_GRP_CNT;i++)
        {
            mimas_devices.pwms[j].ch_data[i] = pwm_def_chan_c;
            mimas_devices.pwms[j].ch_data[i].phyGrp = j;
            mimas_devices.pwms[j].ch_data[i].phyIdx = i;
        }
    }
    devList.glo_uni_map = createBm(32768ul);
    devList.tmp_uni_map = createBm(32768ul);
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
        wsdev->pix_per_uni = DEF_PIX_PER_UNI;
    }
    uint32_t uni_need;
    uint32_t out_need, res;
    addressing_t tmpAddr;
    int idx, i , j, k;
    int hwIdx=0;
    idx = findFreeDevListSlot();
    if(idx == -1)return(-1);

    if((wsdev->colCnt == pix_0_vec) || (wsdev->colCnt > pix_5_vec))  wsdev->colCnt = PIX_VEC_CNT_DEF;
    if(wsdev->pix_per_uni == 0) wsdev->pix_per_uni = (512u /((uint32_t)(wsdev->colCnt)));

    uni_need = (wsdev->pixel_count  / wsdev->pix_per_uni );

    while((uni_need * wsdev->pix_per_uni) < wsdev->pixel_count )uni_need++;

    if(wsdev->com.start_address == 0xFFFF)
    {
        wsdev->com.start_address = devList.next_free_addr;
    }
    tmpAddr = wsdev->com.start_address;

    out_need = uni_need/UNI_PER_OUT;
    if(( out_need * UNI_PER_OUT ) < uni_need)
    {
        out_need++;
    }
    j = wsdev->pixel_count * (uint32_t)wsdev->colCnt;

    for(i=0;i<uni_need;i++)
    {
        k = MIN(j, wsdev->pix_per_uni * (uint32_t)wsdev->colCnt);
        if(checkfDmxRangeIsFree(tmpAddr+i, 0,k)==0)
        {
            printf("Universe %d is not free for this pixel device, aborting\n", tmpAddr+i);
            return(-5);
        }
        j-=k;
    }
    devList.devs[idx].dev_com.dev_type = ws_pix_dev;
    devList.devs[idx].dev_com.start_address = wsdev->com.start_address;
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
            devList.devs[idx].pix_devs[hwIdx]=  ws;

            ws->com.start_address = tmpAddr;
            tmpAddr+=UNI_PER_OUT;
            ws->com.end_address =tmpAddr-1;
            ws->com.dev_type = ws_pix_dev;
            ws->out_start_id = i;
            ws->col_map = wsdev->col_map;
            ws->pixel_count = wsdev->pix_per_uni * UNI_PER_OUT;
            ws->pix_per_uni = wsdev->pix_per_uni;
            ws->com.vDevIdx = idx;
            ws->colCnt = wsdev->colCnt;
            ws->col_map = wsdev->col_map;
            memset(&ws->com.vdsm,0,sizeof(vdev_sm_t));
            ws->com.vdsm.expected_full_map = BIT64(UNI_PER_OUT) - 1u;
            out_need--;
            hwIdx++;
        }
        if(out_need==0)break;
        ws++;
    }
    tmpAddr = tmpAddr==0 ?0:tmpAddr-1;
    devList.devs[idx].dev_com.end_address = tmpAddr;
    if(devList.max_addr<tmpAddr)devList.max_addr = tmpAddr;
    devList.next_free_addr = tmpAddr+1;
    tmpAddr = devList.devs[idx].dev_com.start_address;

    j = wsdev->pixel_count * (uint32_t)wsdev->colCnt;

    for(i=0;i<uni_need;i++)
    {
        k = MIN(j, wsdev->pix_per_uni * (uint32_t)wsdev->colCnt);
        addAddrUsage(tmpAddr++,0, k);
        j-=k;
    }

    if(devList.last_used_idx<idx)devList.last_used_idx = idx;
    vdev_sm_t* vdsm = &devList.devs[idx].dev_com.vdsm;
    memset((void*)vdsm, 0, sizeof(vdev_sm_t));
    vdsm->active_unis = 0;
    vdsm->curr_map = 0ul;
    vdsm->expected_full_map = BIT64(uni_need)-1ul;
    vdsm->min_uni = devList.devs[idx].dev_com.start_address;
    vdsm->unis_cfg = uni_need;
    return(idx);
}

int build_dev_pwm(pwm_vdev_t* pwmdev, pwm_cfg_t* cfg)
{
    int idx;

    idx = findFreeDevListSlot();
    if(idx == -1)return(-1);
    volatile int k , i, offset, need,hwIdx;
    pwm_out_dev_t* p;
    addressing_t tmpAddr;
    uint16_t best_choise  = 0;
    uint16_t best_start_idx = 0;
    uint16_t free_bm;
    need = cfg->ch_count;
    if(need<1)return(-2);

    uint16_t tst_bm;
    uint8_t startS, endS;
    uint16_t t;
    uint16_t free_dmx;
//if request is for any group, hwChannel request is ignored
    if( (cfg->hwGrpIdx == PWM_GRP_ALL)||(cfg->hwGrpIdx == 0) )
    {
        tst_bm = BIT16(need)-1u;
        free_bm =~( (uint16_t)(mimas_devices.pwms[0].usage_bm) | (uint16_t)(mimas_devices.pwms[1].usage_bm<<8));
        startS = 0;
        endS = 16;
    }
    else
    {
        if(need>8)need = 8; // for simplicity, a vDev can exist only inside a single HWdev, not span across more
        tst_bm = BIT16(need)-1u;
        if(cfg->hwStartIdx == -1) // means autoselect hwChannel, in requested group
        {
            switch(cfg->hwGrpIdx)
            {
                case PWM_GRP_A:
                {
                    free_bm =~( (uint16_t)(mimas_devices.pwms[0].usage_bm) | 0xFF00);
                    startS = 0;
                    endS = 8;
                    break;
                }
                case PWM_GRP_B:
                {
                    startS = MIMAS_PWM_OUT_PER_GRP_CNT;
                    endS = startS + MIMAS_PWM_OUT_PER_GRP_CNT;
                    tst_bm  <<= MIMAS_PWM_OUT_PER_GRP_CNT;
                    free_bm = (uint16_t)(mimas_devices.pwms[1].usage_bm);
                    free_bm <<= MIMAS_PWM_OUT_PER_GRP_CNT;
                    free_bm|=(BIT16(startS) - 1u);
                    free_bm =~(free_bm);
                    break;
                }
            }
        }
        else // requested specific hwStart Channel
        {
            tst_bm<<=cfg->hwStartIdx;
            switch(cfg->hwGrpIdx)
            {
                case PWM_GRP_A:
                {
                    free_bm =( (uint16_t)(mimas_devices.pwms[0].usage_bm) | 0xFF00);
                    if(tst_bm & free_bm)return(-3);
                    best_start_idx = cfg->hwStartIdx;
                    tst_bm = 0;
                    break;
                }
                case PWM_GRP_B:
                {
                    tst_bm <<= MIMAS_PWM_OUT_PER_GRP_CNT;
                    free_bm =( (uint16_t)(mimas_devices.pwms[1].usage_bm) | 0x00FF);
                    if(tst_bm & free_bm)return(-4);
                    best_start_idx = cfg->hwStartIdx + MIMAS_PWM_OUT_PER_GRP_CNT;
                    tst_bm = 0;
                    break;
                }
            }
            startS = 0;
            endS = 0;
            best_choise = need;
        }
    }

    for(i=startS;i < endS;i++)
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
        if(tst_bm == 0)break;
    }
    if(best_choise==0)
    {
        return(-2);
    }
    //devList.devs[idx].dev_com = pwmdev->com;
    //if(pwmdev->com.start_address == 0xFFFF)
    if(cfg->com.start_address == 0xFFFF)
    {
        tmpAddr = devList.next_free_addr;// artnet address
        devList.next_free_addr++;
    }
    else
    {
        tmpAddr = cfg->com.start_address;
    }

    devList.devs[idx].dev_com.vDevIdx = idx;
    devList.devs[idx].dev_com.dev_type = pwm_dev;
    devList.devs[idx].chann_count = cfg->ch_count;
    devList.devs[idx].sub_dev_cnt = 1; /*init at 0, update during allocation */
    devList.devs[idx].uni_count = 1;
    devList.devs[idx].pwm_vdev = (pwm_vdev_t*)malloc(sizeof(pwm_vdev_t));

    devList.devs[idx].dev_com.start_address = tmpAddr;
    devList.devs[idx].dev_com.end_address = tmpAddr;
    if(tmpAddr<devList.min_addr)devList.min_addr = tmpAddr;
    if(tmpAddr>devList.max_addr)devList.max_addr = tmpAddr;


    //if(pwmdev->com.start_offset == 0xFFFF)
    if(cfg->com.start_offset == 0xFFFF)
    {
        // findNextFree DMX, write it back to pwmdev->com.start_offset
        offset = getNextFreeChAtAddr(tmpAddr);
    }
    else
    {
        k = checkfDmxChIsFree(tmpAddr, cfg->com.start_offset);
        if(k==0)
        {
            offset = getNextFreeChAtAddr(tmpAddr);
        }
        else
        {
            offset = cfg->com.start_offset;
        }
    }

    devList.devs[idx].pwm_vdev->com.dev_type = pwm_dev;
    devList.devs[idx].pwm_vdev->com.vDevIdx = idx;
    devList.devs[idx].pwm_vdev->com.start_address = tmpAddr;
    devList.devs[idx].pwm_vdev->com.end_address  = tmpAddr;
    devList.devs[idx].pwm_vdev->com.start_offset = offset;
    devList.devs[idx].pwm_vdev->hwStartIdx = best_start_idx;
    k = best_start_idx%MIMAS_PWM_OUT_PER_GRP_CNT;
    for(i=0;i<best_choise;i++)
    {
        hwIdx = k & (BIT16(MIMAS_PWM_OUT_PER_GRP_CNT) - 1u);
        p = &mimas_devices.pwms[ (i+ best_start_idx)/MIMAS_PWM_OUT_PER_GRP_CNT];
        if(cfg!=NULL)
        {
            p->ch_data[hwIdx].lims = cfg->chCfg[i].lims;
            t = p->ch_data[hwIdx].chCtrl & 0xf;
            p->ch_data[hwIdx].chCtrl = ((cfg->chCfg[i].chCtrl & 0xF0) | t);
        }
        p->usage_bm|=BIT8(hwIdx);

        offset+=p->ch_data[hwIdx]._16bits?2:1;
        devList.devs[idx].pwm_vdev->ch_count++;
        k++;
    }
    offset--;
    devList.devs[idx].pwm_vdev->com.end_offset = offset;
    *pwmdev = *devList.devs[idx].pwm_vdev;
    pwmdev->com.end_offset = offset;
    cfg->com.dev_type = pwm_dev;
    cfg->com.vDevIdx = idx;
    //if( (i+best_start_idx) > MIMAS_PWM_OUT_PER_GRP_CNT) devList.devs[idx].sub_dev_cnt++;
    addAddrUsage(devList.devs[idx].dev_com.start_address,devList.devs[idx].pwm_vdev->com.start_offset, offset);
    if(devList.last_used_idx<idx)devList.last_used_idx = idx;
    vdev_sm_t* vdsm = &devList.devs[idx].dev_com.vdsm;
    memset((void*)vdsm, 0, sizeof(vdev_sm_t));
    vdsm->active_unis = 0;
    vdsm->curr_map = 0ul;
    vdsm->expected_full_map = 1ul;
    vdsm->min_uni = devList.devs[idx].dev_com.start_address;
    vdsm->unis_cfg = 1;

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
    if( (devList.min_addr > adr) ||(devList.max_addr < adr) ) return(-1);
    int i;
    for(i=0;i<=devList.last_used_idx;i++)
    {
        if(i==MAX_VDEV_CNT)break;
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
    if( (devList.min_addr > adr) ||(devList.max_addr < adr) ) return(0);
    count = 0;
    for(i=0;i<=devList.last_used_idx;i++)
    {
        if(i==MAX_VDEV_CNT)break;
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
    for(i=0;i<=devList.last_used_idx;i++)
    {
        if(i==MAX_VDEV_CNT)break;

        if(devList.devs[i].dev_com.dev_type == typ)
        {
            if(idxs!=NULL)
            {
                idxs[count] = i;
                //if(count<(MAX_VDEV_CNT-1))idxs[count+1]=-1;
            }
            count++;
        }
    }
    return(count);
}

#define PEER_IP_ONLY 1
int vDevSetPeer(uint64_t peer,int vDevId)
{
mimas_out_dev_t* vDev  = &devList.devs[vDevId];
uint64_t peer_mask;
#ifdef PEER_IP_ONLY
peer_mask = 0xFFFFFFFF0000ul;
#else
 peer_mask = 0xFFFFFFFFFFFFul;
#endif
    peer>>=16;
    if( (vDev->peer_id &peer_mask) != (peer & peer_mask))
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


uint16_t pwmMapValue(pwm_vdev_t* vDev, uint8_t ch, uint8_t* val)
{
    if(vDev->com.dev_type!=pwm_dev)return(-2); // error wrong vDev type
    uint32_t tv;
    int chId = (vDev->hwStartIdx + ch);
    pwm_out_dev_t* p = &GET_PWMS_PTR[chId/MIMAS_PWM_OUT_PER_GRP_CNT];
    chId%=MIMAS_PWM_OUT_PER_GRP_CNT;
    if(p->ch_data[chId]._16bits!=0)
    {
        if(p->ch_data[chId].bswap) tv = bswap_16(*(uint16_t*)val);
        else  tv = *val;
    }
    else
    {
        tv = ((uint16_t)(*val)) *256;
    }
    uint16_t ulim = p->ch_data[chId].lims.maxV;
    if(ulim>p->gperiod)ulim = p->gperiod;
    tv *= (uint32_t)(ulim - p->ch_data[chId].lims.minV);
    tv/=0xFFFF;
    tv+=p->ch_data[chId].lims.minV;
    return((uint16_t)tv);
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

int checkDmxCollission(ln_t lst, uint16_t Start,  uint16_t End)
{
    dmx_chan_range_t* rng;
    while(lst)
    {
        rng = (dmx_chan_range_t*)lst->data;
        if (
            (Start>=rng->Start) && (Start <= rng->Start) ||
            (End>=rng->Start) && (End <= rng->Start)
        )return(1); //collision
        lst =lst->nxt;
    }
    return(0); //ok
}

int addAddrUsage(addressing_t artAdr, uint16_t start,  uint16_t end)
{
    ln_t node;
    dmx_chan_usage_t *AddrNode;
    dmx_chan_range_t  *DmxNode = (dmx_chan_range_t  *)malloc( sizeof(dmx_chan_range_t));
    DmxNode->End = end;
    DmxNode->Start = start;

    node = findItem(devList.addr_usage, (void*)&artAdr, sizeof(addressing_t), 0);
    if(node)
    {
        AddrNode = (dmx_chan_usage_t *)node->data;
        int col = checkDmxCollission(AddrNode->ranges, start, end);
        if(col)
        {
            free(DmxNode);
            return(1); //error
        }
        addItem(&AddrNode->ranges, DmxNode);
    }
    else
    {
        AddrNode = (dmx_chan_usage_t*)malloc(sizeof(dmx_chan_usage_t));
        addItem(&devList.addr_usage,AddrNode);
        AddrNode->address = artAdr;
        AddrNode->items=0;
        AddrNode->ranges=NULL;
        AddrNode->nxt_free=0;
       // node = addItem(devList.addr_usage,AddrNode);
        addItem(&AddrNode->ranges,DmxNode);
        devList.mapped_addresses++;
    }

    AddrNode->items++;
    AddrNode->nxt_free=end+1;
    return(0); //success
}

uint16_t getNextFreeChAtAddr(addressing_t adr)
{
    if(devList.addr_usage == NULL)return(0);
    ln_t node;
    node = findItem(devList.addr_usage, (void*)&adr, sizeof(addressing_t), 0);
    if(node == NULL)return(0);
    dmx_chan_usage_t *AddrNode = (dmx_chan_usage_t *)node->data;
    return(AddrNode->nxt_free);
}


/* return 0 if address is NOT free, otherwise 1 */
int checkfDmxChIsFree(addressing_t adr, uint16_t chan)
{
    if(devList.addr_usage == NULL)return(1);
    ln_t node;
    node = findItem(devList.addr_usage, (void*)&adr, sizeof(addressing_t), 0);
    if(node == NULL)return(1);
    dmx_chan_usage_t *AddrNode = (dmx_chan_usage_t *)node->data;
    return(!checkDmxCollission( AddrNode->ranges, chan, chan));
}

/* return 0 if address range is NOT free, otherwise 1 */
int checkfDmxRangeIsFree(addressing_t adr, uint16_t Start, uint16_t End)
{
    if(devList.addr_usage == NULL)return(1);
    ln_t node;
    node = findItem(devList.addr_usage, (void*)&adr, sizeof(addressing_t), 0);
    if(node == NULL)return(1);
    dmx_chan_usage_t *AddrNode = (dmx_chan_usage_t *)node->data;
    return(!checkDmxCollission( AddrNode->ranges, Start, End));
}

/*
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
*/
/*
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
*/
/*
int setPwmVal(pwm_vdev_t* vDev, uint8_t ch, uint16_t val)
{
    if(vDev->com.dev_type!=pwm_dev)return(-2); // error wrong vDev type
    vDev->chan_data[ch]->chV = val;
    return(0);
}
*/
/*
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
*/

/*
void makeDmxBM(uint16_t start,  uint16_t end, dmx_chan_usage_t* n)
{
    uint8_t* pt = &n->bm[start/8];
    if(start == end)
    {
        *pt|=1<< start%8;
        return;
    }
    uint16_t cnt = end-start+1;
    uint8_t  msk = 1<< start%8;
    while(cnt)
    {
        while( msk )
        {
            *pt|=msk;
            msk<<=1;
            if(--cnt == 0 )break;
        }
        msk=1;
        pt++;
    }
}

void addAddrUsage(addressing_t artAdr, uint16_t start,  uint16_t end)
{

    dmx_chan_usage_t node;
    node.address = artAdr;
    makeDmxBM(start,end,&node);
    ln_t item = findItem(devList.addr_usage,&node, sizeof(addressing_t), 0);
    if(item)
    {
        //check existing usage, and merge or fail
        dmx_chan_usage_t node2 = *(dmx_chan_usage_t*)item->data;
        uint8_t  *pt1,*pt2;
        pt1 = &node2.bm[start/8];
        *pt1 &= ~(BIT8(start%8)-1u);
        pt2 = &node.bm[start/8];
        uint16_t bcnt = end-start+1;
        uint16_t at= start ;
        if(end%8)bcnt--;
        if(start%8)
        {
            if(*pt2 & *pt1)return;
            at+= (8-(start%8));
            bcnt--;
            pt1++;
            pt2++;
        }
        while(bcnt)
        {
            if(*pt2 & *pt1)
            {
                return;
            }
            bcnt--;
            pt1++;
            pt2++;
            at+=8;
        };
        if(end>at)
        {
            if(*pt1 & *pt2)return;
        }
        dmx_chan_usage_t* upd = (dmx_chan_usage_t*)item->data;
        for(at=start/8;at< 1+(end/8);at++)
        {
            upd->bm[at]|= node.bm[at];
        }
    }
    else
    {
        item = addItem(devList.addr_usage, &node);
        if(devList.addr_usage==NULL)devList.addr_usage = item;
    }
}
*/
