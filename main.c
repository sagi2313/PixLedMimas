#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include "rq.h"
#include "protocolCommon.h"
#include "utils.h"
#include "type_defs.h"
#include <bcm2835.h>
#include "mimas_cfg.h"
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
//#include <net/if.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>
#include "recorder.h"
#include <signal.h>
#include <sched.h>
#include <sys/ucontext.h>
#include <ucontext.h>
#include <execinfo.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <string.h>
#include "vdevs.h"
#include "mimas_cmd_defs.h""
#define gettid syscall(SYS_gettid)

#include <byteswap.h>
volatile int upd_pwm = 0;

out_def_t           outs[MIMAS_STREAM_OUT_CNT ];
device_type_e       outputs_dev_map[64] = {dev_unknown};
peer_pack_t         rq_data[RQ_DEPTH];
peer_pack_t         pwm_rq[PWM_Q_DEPTH];
trace_msg_t         ev_q[EV_Q_DEPTH];
post_box_t*         ev_pb = NULL;
post_box_t*         pwm_pb = NULL;

int sock_sec;
app_node_t prods[2];
app_node_t* anetp=&prods[0];
/*app_node_t* ascnp=&prods[1];*/
int altBind(int sockfd);
//recFile_t rf;


enum{
idle_e=0,
packet_proc_e,
packet2packet_e
}times_e;
struct timespec timers[3];
struct timespec tmp;
uint32_t timeloop, proc_cnt=0;
uint64_t timesum;
uint64_t timesum_mimas;
uint64_t timesum_proc;
clock_t t1,t2, t3, p1,p2;


void print_trace (void)
{
  void *array[10];
  size_t size;
  char **strings;
  size_t i;

  size = backtrace (array, 10);
  strings = backtrace_symbols (array, size);

  printf ("Obtained %zd stack frames.\n", size);

  for (i = 0; i < size; i++)
     printf ("%s\n", strings[i]);

  free (strings);
}
void print_trace_gdb() {
    char pid_buf[30];
    sprintf(pid_buf, "%d", getpid());
    char name_buf[512];
    name_buf[readlink("/proc/self/exe", name_buf, 511)]=0;

    int child_pid = fork();
    if (!child_pid) {
        dup2(2,1); // redirect output to stderr
        fprintf(stdout,"stack trace for %s pid=%s\n",name_buf, pid_buf);
        execlp("gdb", "gdb", "--batch", "-n", "-ex", "thread", "-ex", "bt", name_buf, pid_buf, NULL);
        //if gdb failed to start
        abort();
    } else {
        waitpid(child_pid,NULL,0);
    }
 }
void fault_handler(int signo, siginfo_t *info, void *extra)
{
	printf(" =========== Signal %d received =========== \n", signo);
	printf("siginfo address=%x\n",info->si_addr);

	ucontext_t *p=(ucontext_t *)extra;
	int val = p->uc_mcontext.arm_pc;
	printf("address = %x\n\nBACKTRACE:\n",val);
	//print_trace();
	print_trace_gdb();
	abort();
}

void setHandler(void (*handler)(int,siginfo_t *,void *))
{
    printf("Setting up SIG_HAN\n");
	struct sigaction action;
	action.sa_flags = SA_SIGINFO;
	action.sa_sigaction = handler;

	if (sigaction(SIGFPE, &action, NULL) == -1) {
		perror("sigfpe: sigaction");
		_exit(1);
	}
	if (sigaction(SIGSEGV, &action, NULL) == -1) {
		perror("sigsegv: sigaction");
		_exit(1);
	}
	if (sigaction(SIGILL, &action, NULL) == -1) {
		perror("sigill: sigaction");
		_exit(1);
	}
	if (sigaction(SIGBUS, &action, NULL) == -1) {
		perror("sigbus: sigaction");
		_exit(1);
	}
	//SIGALRM
	if (sigaction(SIGALRM, &action, NULL) == -1) {
		perror("sigbus: sigaction");
		_exit(1);
	}
    printf("Done setting up SIG_HAN\n");
}


void prnDev(int idx)
{
    int i;
    if(idx!=-1)
    {
        switch(devList.devs[idx].dev_com.dev_type)
        {
            case unused_dev:
            {
                break;
            }
            case ws_pix_dev:
            {
                    if(devList.devs[idx].dev_com.start_address == 0xFFFF)
                    {
                        printf("Address %u assigned automatically for device %d\n", devList.devs[idx].pix_devs[0]->com.start_address, idx);
                        devList.devs[idx].dev_com.start_address = devList.devs[idx].pix_devs[0]->com.start_address;
                    }
                    printf("Dev %d pixelCount %u, uniCount  %u subDevices %u startAddr %u, endAddr %u\n", \
                    idx, devList.devs[idx].pixel_count, devList.devs[idx].uni_count, devList.devs[idx].sub_dev_cnt, devList.devs[idx].dev_com.start_address, \
                    devList.devs[idx].dev_com.end_address);
                    for(i=0;i< devList.devs[idx].sub_dev_cnt;i++)
                    {
                        ws_pix_vdev_t* pxd = devList.devs[idx].pix_devs[i];
                        printf("subDev %d, startAddr %u, endAddr %u, pixCount %u, pixPerUni %u, devId %u\n",  \
                        i,pxd->com.start_address, pxd->com.end_address, pxd->pixel_count, pxd->pix_per_uni, pxd->out_start_id);

                    }
                break;
            }
            case dmx_out_dev:
            {
                break;
            }
            case pwm_dev:
            {
                break;
            }
            default:break;
        }

    }
}

void make_a_dev()
{
    int res, i;
    pwm_cfg_t cfg;
    pwm_vdev_t pdev;
    pdev.com.start_address = 57;
    pdev.com.start_offset = 0;
    pdev.ch_count = 3;
    for(i=0;i<pdev.ch_count;i++)
    {
        cfg.chCfg[i].lims.minV = 1450u;//9000u;
        cfg.chCfg[i].lims.maxV = 6500u;//26000u;
        cfg.chCfg[i].chCtrl = PWM_CH_EN |  PWM_CH_16B | PWM_CH_BSW;
    }
    cfg.chCfg[2].lims = pwm_def_limits_c;
    cfg.chCfg[3].chCtrl = PWM_CH_EN |  PWM_CH_16B | PWM_CH_BSW;

    res = build_dev_pwm(&pdev, &cfg);
    pdev.com.start_address = 57;
/*
    pdev.ch_count = 1;
    cfg.chCfg[0].lims = pwm_def_limits_c;
    cfg.chCfg[0].chCtrl = PWM_CH_EN |  PWM_CH_16B | PWM_CH_BSW;
    res = build_dev_pwm(&pdev, &cfg);*/
/*    pdev.ch_count = 12;
    cfg.chCfg[0].lims = pwm_def_limits_c;
    cfg.chCfg[0].lims.maxV = 26000u;
    res = build_dev_pwm(&pdev, &cfg);
prnDev(res);*/
    //pdev.ch_count = 2;
   // build_dev_pwm(&pdev);
   // build_dev_pwm(&pdev);
   // build_dev_pwm(&pdev);

    ws_pix_vdev_t   vd;
    vd.pixel_count = 1500;
    vd.pix_per_uni = 150;
    vd.col_map = grb_map_e;
    vd.com.start_address = 17;
    res =build_dev_ws(&vd);
    vd.com.start_address = 14;
    res =build_dev_ws(&vd);
    vd.com.start_address = 51;
    res =build_dev_ws(&vd);
    prnDev(res);
}

void InitOuts(void)
{
    int i, j;
    make_a_dev();
    uint8_t *pt;
    for(i=0;i< (MIMAS_STREAM_OUT_CNT  * MIMAS_DEV_CNT);i++)
    {
        anetp->artnode->outs[i] = &outs[i];
        outs[i].colMap = grb_map_e;
        outs[i].mappedLen = 0;
        for(j=0; j < UNI_PER_OUT; j++)
        {
            outs[i].uniLenLimit[j] = (PIX_PER_UNI * CHAN_PER_PIX);
            outs[i].mappedLen += outs[i].uniLenLimit[j];
        }
        outs[i].dlen = 0;
        outs[i].fillMap = 0;
        outs[i].fullMap = 0;
        pt = &outs[i].mpack.dmxp[0].dmx_data[0];
        for(j=0;j<UNI_PER_OUT;j++)
        {
            outs[i].wrPt[j] = pt;
            pt+=outs[i].uniLenLimit[j];
        }
        if(i < (MIMAS_STREAM_OUT_CNT  * MIMAS_DEV_CNT))
        {
            outputs_dev_map[i] = dev_pixels;
        }
        else
        {
            outputs_dev_map[i] = dev_pwm;
        }
    }
    outputs_dev_map[8] = dev_pwm;
}


int sendOutToMimas(int oSel)
{
    return(mimas_store_packet(oSel,(uint8_t*)&outs[oSel].mpack,outs[oSel].dlen));
}

void* consumer(void* d)
{
    int                 devCnt, devIdx[MAX_VDEV_CNT];
    uint32_t idx, i,    attention;
    peer_pack_t         ppack;
    peer_pack_t *p =    &ppack;
    uint64_t            peer_id;
    whole_art_packs_rec_t rec;
    app_node_t *artn = (app_node_t*)d;
    post_box_t* pb = artn->artPB;
    node_t *me = artn->artnode;
    sm_t* sm = &me->sm;

    int  rc;
    uint16_t start_bm;
    fl_t cn;
    art_net_pack_t      *ap;
    peer_pack_t         *pp;
    int mark, lowMark;
    lowMark = RQ_DEPTH;
    art_resp_e  art_res;
    gen_addres_u    raw_addr;
    uint8_t runi, oSel, sUni; //relative universe
    int which = PRIO_PROCESS;
    int ret;
    cpu_set_t cpuset;
    pthread_t thread;
    thread = pthread_self();
    timeloop=0;
    timesum =0;
    timesum_mimas=0;
    timesum_proc=0;
    trace_msg_t trm[5];
    trm[cons_msg_pop].ev = cons_msg_pop;
    trm[cons_msg_proced].ev = cons_msg_proced;
    trm[cons_pack_full].ev = cons_pack_full;
    trm[cons_mimas_refreshed].ev = cons_mimas_refreshed;
    trm[cons_mimas_start].ev = cons_mimas_start;
    uint32_t popcnt=0;
    uint16_t pwmv ;
    mimas_out_dev_t *cdev;
    trace_msg_t *trms[5];
    trms[0]=&trm[0];
    trms[1]=&trm[1];
    trms[2]=&trm[2];
    trms[3]=&trm[3];
    trms[4]=&trm[4];
 #define handle_error_en(en, msg) \
               do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

    //CPU_ZERO(&cpuset);

    /*for (i = 0; i < 8; i++)*/
    //CPU_SET(3, &cpuset);
/*    ret = pthread_getaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (ret != 0)handle_error_en(ret, "pthread_getaffinity_np");
    cpuset.__bits[0] = 3;
    ret = pthread_setaffinity_np(thread,sizeof(cpu_set_t), &cpuset);

    if (ret != 0) handle_error_en(ret, "pthread_setaffinity_np");
*/
   /* Check the actual affinity mask assigned to the thread */

   ret = pthread_getaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
   if (ret != 0)
       handle_error_en(ret, "pthread_getaffinity_np");

   printf("Get returned by pthread_getaffinity_np() contained:\n");
   int j =1;
   for (i = 0; i < 8; i++)
   {
       //if (CPU_ISSET(i, &cpuset))
       if((cpuset.__bits[0] & j) !=0)
           printf("CPU %d, ", i);
        j<<=1;
    }

    attention = 0;
    pid_t tid = gettid;
    printf("CONSUMER TID = %u\n", (uint32_t)tid);
    ret = setpriority(which, tid,  CONS_PRIO_NICE );
    printf("Consumer set_nice to (%i) returns %d\n",CONS_PRIO_NICE, ret);
    mimas_state_t mSt;
    struct timespec ts;
    int cause = 0;
    clock_getres(CLOCK_PROCESS_CPUTIME_ID, &timers[1]);
    printf("ClockRes = %lusec, %u nsec\n", timers[1].tv_sec, timers[1].tv_nsec);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &timers[idle_e]);
    t1 = clock();

    while(1)
    {
        cdev = NULL;

        do
        {
            usleep(2);
            ret = getCopyMsg(&pb->rq_head, p);
        }while(ret);
        clock_gettime(CLOCK_REALTIME, &trms[0]->ts);
        switch(p->mtyp)
        {
            case msg_typ_socket_data:
            {
                trms[0]->msg_cnt = popcnt++;
                post_msg(&ev_pb->rq_head, trms[0],sizeof(trace_msg_t));
                break;
            }
            case msg_typ_sys_event:
            {
                switch(p->sys_ev.ev_type)
                {
                    case sys_ev_socket_timeout:
                    {
                        printf("Socket TimeOut received on sock %d\n", p->sys_ev.data1);
                        break;
                    }
                    default:
                    {
                        printf("Socket event %d received on sock %d\n", (uint32_t)p->sys_ev.ev_type, p->sys_ev.data1);
                        break;
                    }
                }

                continue;
                break;
            }
            default:
            {
                printf("Unknown msgType received : %u\n", (uint32_t)p->mtyp);
                continue;
                break;
            }
        }

        peer_id = (*(uint64_t*)&p->sender.sin_port) & 0xFFFFFFFFFFFF;
        ap =  &p->pl.art; //&pp->pl.art;
        art_res = ArtNetDecode(ap);
        //printf("Cons: got msg, artres = %u, uni = %u\n", art_res, ap->ArtDmxOut.a_net.SubUni.subuni_full);
        switch(ap->ArtDmxOut.head.OpCode.OpCode)
        {
            case 	OpPoll:
            {
                printf("================\nGot POLL from %s\n",inet_ntoa(p->sender.sin_addr));
                make_artnet_resp(p);
                //ap->ArtDmxOut.head.id[0] = '\0';
                //msgRead(&pb->rq_head);
                //msgRelease(pb,cn); // return this node to unused pile
                continue;
                break;
            }
            case	OpPollReply:
            case	OpDiagData:
            case	OpCommand:
            case	OpNzs:
            case	OpAddress:
            case	OpInput:
            case	OpTodReq:
            case	OpTodData:
            case	OpTodCtrl:
            case	OpRdm:
            case	OpRdmSub:
            case	OpVidSetup:
            case	OpVidPalette:
            case	OpVidData:
            case	OpOther:
            {
                //putNode(&RQ, cn);
                //ap->ArtDmxOut.head.id[0] = '\0';
                printf("================\nArtErr unknown OpCode %x\n================\n", ap->ArtDmxOut.head.OpCode.OpCode);
                //msgRelease(pb,cn);
                //msgRead(&pb->rq_head);
                continue; // get next Packet
                break;
            }
            case    OpSync:
            {
                //putNode(&RQ, cn);
                //msgRelease(pb,cn);
                //msgRead(&pb->rq_head);
                switch(sm->state)
                {
                    case	out_of_sync_e:
                    case    wait_sync_e:
                    {
                        sm->hasSync = 1;
                        sm->sync_missing_cnt = 0;
                        printf("================\nUsing Sync packets ...\n");
                        continue; // get next Packet
                        break;
                    }
                    case working_e:
                    {
                        if(sm->hasSync == 0)
                        {
                            printf("================\ndetected sync packet while working. Resetting SM ...\n================\n");
                            SmReset(me, eResetArtOther);
                            continue; // get next Packet
                        }
                        sm->sync_missing_cnt = 0;
                        if(sm->syncState == no_sync_yet)
                        {
                            sm->syncState = sync_ok;
                        }
                        else
                        {
                            continue;
                        }
                        break;
                    }
                    default:
                    {
                        printf("================\nSync received in state %d\n", sm->state);
                        if(sm->hasSync == 0)
                        {
                            printf("================\ndetected sync packet in wrong state. Resetting SM ...\n================\n");
                            SmReset(me, eResetArtOther);
                            continue; // get next Packet
                        }
                        sm->syncState = sync_ok;
                        sm->sync_missing_cnt = 0;
                        continue; // get next Packet
                    }
                }
                break;
            }
            case OpDmx:
            {
                raw_addr.anet = ap->ArtDmxOut.a_net;
                clock_gettime(CLOCK_REALTIME, &trms[1]->ts);
                me->last_pac_proc = trms[1]->ts;
                trms[1]->art_addr = raw_addr.addr;
                post_msg(&ev_pb->rq_head, trms[1],sizeof(trace_msg_t));
                // check if incoming universe falls in the range we handle
                // TODO: align sm->startNet, sm->endNet with devList and config
                if((ap->ArtDmxOut.a_net.SubUni.subuni_full < (sm->startNet & 0xFF)) || \
                    (ap->ArtDmxOut.a_net.SubUni.subuni_full > (sm->endNet & 0xFF)))
                {
                    continue; // dont care about this universe, get next Packet
                }
                // adjust min_uni if required
                runi = raw_addr.addr - sm->startNet;
                if(sm->min_uni > raw_addr.addr)
                {
                    sm->min_uni = raw_addr.addr;
                }
                switch(sm->state)
                {
                    case	out_of_sync_e:
                    {
                        //if we got a universe for second time, and its the minimum we probably have them all, so we prepare for next state
                        if( (sm->expected_full_map & BIT64(runi)) && \
                            (sm->min_uni == raw_addr.addr) )
                        {
                            for(i=0;i<MIMAS_STREAM_OUT_CNT;i++)
                            {
                                outs[i].dlen = 0;
                                outs[i].fullMap = sm->expected_full_map & (BIT64(UNI_PER_OUT)  - 1u);
                                sm->expected_full_map>>=UNI_PER_OUT;
                            }
                            start_bm = 0;
                            sm->expected_full_map = 0llu;
                            sm->state = wait_sync_e;
                            printf("================\nGoing wait_sync_e\n");
                            sm->DataOk = 1;
                            sm->active_unis = 0;
                            /*dont break, fall through to wait sync state */
                        }
                        else
                        {
                            sm->expected_full_map|=BIT64(runi);
                            continue;  // don't process  output, got get packets
                            break;
                        }
                    }
                    case	wait_sync_e:
                    {
                        // if we got minimum known uni for a second time move to working state
                        if( (sm->min_uni == raw_addr.addr) && (sm->expected_full_map & BIT64(runi)) )
                        {
                            sm->state = working_e;
                            time_t now;
                            time(&now);
                            struct tm ts = *localtime(&now);
                            char tsbuf[80];
                            strftime(tsbuf, sizeof(tsbuf), "%T", &ts);
                            printf("================\nGoing working_e, %u active unis at %s\n", sm->active_unis, tsbuf);
                            sm->curr_map = 0llu;
                            /*dont return nod yet to pile, it will be used in working state, and returned there.
                             * also dont break, fall through to working state*/
                        }
                        else
                        {
                            if((sm->expected_full_map & BIT64(runi) )== 0llu)sm->active_unis++;
                            sm->expected_full_map|=BIT64(runi);
                            continue;
                            break;
                        }
                    }
                    case working_e:
                    {
                        devCnt = findVDevsAtAddr(raw_addr.addr, devIdx);

                        oSel = runi / UNI_PER_OUT;
                        sUni = runi % UNI_PER_OUT;


                        // if Sync packet is not present in stream and we got minimum uni, it must be a new frame, so reset bitmasks, and get fresh data
                        if(sm->hasSync == 0)
                        {
                            outs[oSel].fillMap|=BIT8(sUni);
                            if((sm->curr_map & BIT64(runi)))    // if this uni was already mapped in output, it will be overwritten by new packet, from previous 2 lines
                            {
                                fl_head_t info;
                                if(sm->expected_full_map & BIT64(runi))
                                {
                                    get_pb_info(&pb->rq_head, &info);
                                    printf("got a second runi %u. head = %u, tail = %u, cnt = %u, hS = %u, tS = %u\n", runi, info.head, info.tail, info.count, info.headStep, info.tailStep);
                                }
                                else
                                {
                                    sm->expected_full_map = 0;
                                    sm->state = out_of_sync_e;
                                    start_bm = 0;
                                    sm->curr_map = 0llu;
                                    for(i=0;i<MIMAS_STREAM_OUT_CNT;i++)
                                    {
                                        outs[i].dlen = 0;
                                        outs[i].fillMap = 0;
                                    }
                                    continue;
                                }
                            }
                            else
                            {
                                sm->curr_map|=BIT64(runi);
                                outs[oSel].dlen+=outs[oSel].uniLenLimit[sUni];
                            }
                        }
                        else
                        {
                            // if this is the first time we receive this universe in this frame do some extras
                            if(sm->syncState == sync_consumed) sm->syncState = no_sync_yet;
                            if((sm->curr_map & BIT64(runi)) == 0ul)
                            {
                                outs[oSel].dlen+=outs[oSel].uniLenLimit[sUni];
                                sm->curr_map|=BIT64(runi);
                                outs[oSel].fillMap|=BIT8(sUni);
                            }
                            else
                            {
                                if(++sm->sync_missing_cnt  > (2 * sm->active_unis))
                                {
                                    SmReset(me, eResetFrErr);
                                }
                            }
                        }
                        //if(outputs_dev_map[oSel] == dev_pixels)
                        for(i=0;i<devCnt;i++)
                        {
                             cdev = &devList.devs[devIdx[i]];
                             vDevSetPeer(peer_id, devIdx[i]);

                            if (cdev!=NULL)
                            {
                                switch(GET_VDEV_TYPE(*cdev))
                                {
                                    case ws_pix_dev:
                                    {
                                        mapColor(&ap->ArtDmxOut.dmx,&outs[oSel],sUni);
                                        if( (outs[oSel].fullMap>0) && (outs[oSel].fillMap == outs[oSel].fullMap) )
                                        {
                                            rc = sendOutToMimas(oSel);
                                            clock_gettime(CLOCK_REALTIME, &trms[2]->ts);
                                            if(rc == 0)
                                            {
                                                start_bm|=BIT32(oSel);
                                                post_msg(&ev_pb->rq_head, trms[2],sizeof(trace_msg_t));
                                            }
                                            else
                                            {
                                                start_bm&=(0x3FF & (~BIT32(oSel)));
                                                outs[oSel].fillMap = 0;
                                                printf("Error %d from mimas sending %d output\n",rc,oSel);
                                            }
                                        }
                                        break;
                                    }
                                    case pwm_dev:
                                    {
                                        static int msg_posts=0;
                                        struct timespec ts;
                                        clock_gettime(CLOCK_REALTIME, &ts);
                                        post_msg(&pwm_pb->rq_head, p, sizeof(peer_pack_t));

                                        //printf("%05lu:%09lu  pwm_msg_posts = %d\n",ts.tv_sec, ts.tv_nsec, ++msg_posts);
                                        break;
                                    }
                                }

                            }
                        } //cdevs for loop end
                        /*else if(outputs_dev_map[oSel] == dev_pwm)
                        {
                            for(i=0;i<2;i++)
                            {
                                pwm_v[4 * i] = (ap->ArtDmxOut.dmx[i]);
                                pwm_v[4 * i]  *= 17;
                                pwm_v[4 * i] +=2400;
                               // printf("%s : %u ",i==0?"Pan":"Tilt", pwm_v[4*i]);

                            }

                            //printf("\n");
                            mimas_store_pwm_val(PWM_GRP_ALL, 0, pwm_v,8);
                        }*/
                        break; // go process out
                    } // working state inside OpDMX closes
                    break;
                } // switch(sm->state) closes
            break;
            }   //OpCodeDMX closes
            default:
            {
                //msgRead(&pb->rq_head);
            }
        } // switch OpCode closes
        // here starts output Handling
        if(sm->hasSync == 1)
        {
            switch(sm->syncState)
            {
                case no_sync_yet:
                case sync_consumed:
                {
                    continue;
                    break;
                }
                case sync_ok:
                {
                    break;
                }
            }
        }
        if(sm->curr_map == sm->expected_full_map)
        {
            clock_gettime(CLOCK_REALTIME, &trms[3]->ts);
            mimas_refresh_start_stream(start_bm,0x00C0);
            clock_gettime(CLOCK_REALTIME, &trms[4]->ts);
            post_msg(&ev_pb->rq_head, trms[3],sizeof(trace_msg_t));
            post_msg(&ev_pb->rq_head, trms[4],sizeof(trace_msg_t));
            me->last_mimas_ref = trms[3]->ts;
            start_bm = 0;
            sm->curr_map = 0llu;
            for(i=0;i<MIMAS_STREAM_OUT_CNT;i++)
            {
                outs[i].dlen = 0;
                outs[i].fillMap = 0;
            }
            sm->syncState = sync_consumed;
        }
    }
}
#include <time.h>
//#define ALL_EVENTS
//#define EVENT_DIFFS
void* one_sec(void* d)
{
    /*time_t     now;
    struct tm  ts;*/
    char       tsbuf[8192];
    memset(tsbuf, 0, 8192);
    struct timespec timeZero, last_ts, sleep_for[2];
    clock_gettime(CLOCK_REALTIME, &timeZero);
    usleep(  (useconds_t)500000u );
    const char* evTypes[]={"conPop  ", "conProc ", "fullPack", "mimasStart ","mimasRef", "prodRx  "};
    int idxs[64];
    int res, i, len;
    trace_msg_t trs[64];
    long mins, sec;
    long nanosec, tdiff;
    float difff;
    last_ts = timeZero;
    uint32_t rx_p, proc_p;
    rx_p=0;
    proc_p = 0;
    int print=0;
    while(1)
    {
        clock_gettime(CLOCK_REALTIME, &sleep_for[0]);
        res = get_taces(&ev_pb->rq_head, &idxs[0], 64);
        if(res>0)
        {
            print+=res;
            i=0;
            len =0;
            for(i=0;i<res;i++)
            {
                trs[i] = ev_q[idxs[i]];
            }

            free_traces(&ev_pb->rq_head, i);
            for(i=0;i<res;i++)
            {
                if(trs[i].ev == prod_rx_msgs)
                {
                    rx_p+=trs[i].msg_cnt;
                }
                else
                {
                    if(trs[i].ev == cons_msg_proced)
                    {
                        proc_p+=1;
                    }
                }
            }
#ifdef ALL_EVENTS
            for(i=0;i<res;i++)
            {
                mins = (( long)(trs[i].ts.tv_sec - timeZero.tv_sec))/60ll;
                sec = (( long)(trs[i].ts.tv_sec - timeZero.tv_sec))%60ll;
                if(trs[i].ts.tv_nsec >= timeZero.tv_nsec)
                {

                    nanosec = trs[i].ts.tv_nsec - timeZero.tv_nsec;
                    nanosec/=1000l;
                }
                else
                {
                    nanosec =  timeZero.tv_nsec - trs[i].ts.tv_nsec;
                    nanosec/=1000l;
                    nanosec = 1000000l - nanosec;
                    if(sec<1)
                    {
                        printf("error calc\n");
                    }
                    else
                    {
                        sec--;
                    }
                }
                if(trs[i].ts.tv_nsec >= last_ts.tv_nsec)
                {

                    tdiff = trs[i].ts.tv_nsec - last_ts.tv_nsec;
                    tdiff/=1000l;
                    //tdiff.tv_sec =trs[i].ts.tv_sec - last_ts.tv_sec
                }
                else
                {
                    tdiff =  last_ts.tv_nsec - trs[i].ts.tv_nsec;
                    tdiff/=1000l;
                    tdiff = 1000000l - tdiff;
                    if( (trs[i].ts.tv_sec - last_ts.tv_sec)<1)
                    {
                        printf("error2 calc\n");
                    }

                }
                difff =((float)(tdiff))/1000.0f;
                len += sprintf(&tsbuf[len], "[%04u] %02ld:%02ld.%06ld : %9s\t%u\t\tdiff %3.3f msec\n", idxs[i],   \
                mins, sec, nanosec, evTypes[trs[i].ev] , \
                (trs[i].ev == prod_rx_msgs)?trs[i].msg_cnt:(uint32_t)(trs[i].art_addr), difff);
                last_ts = trs[i].ts;
            }
            printf("%s\0",tsbuf );
#endif
        }
        else
        {
#ifdef  EVENT_DIFFS
            if((print)&&(rx_p!=proc_p))
            {
                printf("%u Events: packRx: %u, packProc %u, diff %d\n",print, rx_p, proc_p, (rx_p - proc_p));
            }
#endif
            print =0;
            uint32_t sleefor;
            clock_gettime(CLOCK_REALTIME, &sleep_for[1]);
            if(sleep_for[1].tv_sec > sleep_for[0].tv_sec)
            {
                sleefor =  (uint32_t) ((1000000000 - sleep_for[0].tv_nsec + sleep_for[1].tv_nsec)/1000u);
           }
            else
            {
                sleefor = (uint32_t) ((sleep_for[1].tv_nsec - sleep_for[0].tv_nsec)/1000u);
            }
            usleep(100000 - sleefor); // every 100mSec
        }
    }
}

#define MMLEN_MMAX   40
#define MILIS   (1000000ul)

void* producer(void* d)
{
    uint32_t  i, msg_need;
    peer_pack_t *packs[MMLEN_MMAX];
    struct mmsghdr msgs[MMLEN_MMAX];
    struct iovec iovecs[MMLEN_MMAX];
    uint_fast32_t hits[2+MMLEN_MMAX];
    peer_pack_t *p;
    app_node_t* artn = (app_node_t*)d;
    node_t*     n = artn->artnode;
    post_box_t* pb = artn->artPB;
    int mysock = n->sockfd;
    fl_t cn;
    mimas_state_t mSt;
    int addr_len;
    pid_t pid;
    int ret;
    //size_t buf_len;
    long nsec = 0l;
    artn->artnode->miss=0;
    artn->artnode->all=0;
    artn->artnode->packetsCnt=0;
    cpu_set_t cpuset;
    pthread_t thread;
    thread = pthread_self();
    struct timespec timeout;
    trace_msg_t trm;
    trm.ev = prod_rx_msgs;
    trm.seq = 0;
    int batch=1;
    sm_state_e sm_state_cache;
   // i=socket_set_blocking(n->sockfd, 0);
 #define handle_error_en(en, msg) \
               do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

    //CPU_ZERO(&cpuset);

    /*for (i = 0; i < 8; i++)*/
    //CPU_SET(3, &cpuset);
    ret = pthread_getaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
           if (ret != 0)
               handle_error_en(ret, "Producer pthread_getaffinity_np");
    cpuset.__bits[0] = 0xC;
    ret = pthread_setaffinity_np(thread,sizeof(cpu_set_t), &cpuset);

    if (ret != 0)
               handle_error_en(ret, "Producer pthread_setaffinity_np");

    ret = pthread_getaffinity_np(thread, sizeof(cpu_set_t), &cpuset);

    if (ret != 0)
               handle_error_en(ret, "Producer pthread_getaffinity_np2");
    else
    {
        printf("Affinity on producer is set to 0x%0lx\n", cpuset.__bits[0]);
    }

    pid_t tid = gettid;
    ret = setpriority(PRIO_PROCESS, tid, (PROD_PRIO_NICE ) );
    printf("Producer set_nice to (%i) returns %d\n",PROD_PRIO_NICE, ret);

    time_t     now;
    struct tm  ts;
    char       tsbuf[80];
    int totmsges=0;
    memset(msgs, 0, sizeof(msgs));
    for(i=0;i<MMLEN_MMAX;i++)
    {
        iovecs[i].iov_base = NULL;// addr of data
        iovecs[i].iov_len = sizeof(any_prot_pack_t);
        msgs[i].msg_hdr.msg_iov    = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
    }

    //p = rezerveMsg(&pb->rq_head);
    int32_t msgcnt;
    #define MMLEN batch
    //cn =msgRezerveNB(pb);

    memset(hits,0,sizeof(hits));

    memset(packs,0,sizeof(packs));
    msg_need = rezerveMsgMulti(&pb->rq_head,packs, MMLEN );
    for(i=0;i<msg_need;i++)
    {
        p = packs[i];
        if(p==NULL) break;
        p->mtyp = msg_typ_socket_data;
        iovecs[i].iov_base = &p->pl.art;
        msgs[i].msg_hdr.msg_name = &p->sender;
        msgs[i].msg_hdr.msg_namelen = sizeof(struct sockaddr_in);
    }
    msg_need = i;
    setSockTimout(n->sockfd, 0);
    while(1)
    {
        //addr_len = sizeof(struct sockaddr_in);
        //p = &rq_data[idx];

        //sock_ret = recvfrom(mysock,(void*)&p->pl, sizeof(any_prot_pack_t),0, (struct sockaddr*)&p->sender, &addr_len);

        if((n->sm.DataOk == 1) && (n->sm.prev_state!=working_e))
        {
            setSockTimout(n->sockfd,500);
        }
        timeout.tv_sec = 0l;
        timeout.tv_nsec = (25l * MILIS)/10l;
        sm_state_cache = n->sm.state;
        if(sm_state_cache!=working_e)batch=1;
        msg_need = MIN(msg_need,batch);
        msgcnt = recvmmsg(n->sockfd, msgs, msg_need, MSG_WAITALL/* MSG_WAITFORONE */, &timeout);
        time(&trm.ts2);



/*
        if(++hits[MMLEN+1] == 1000)
        {
            printf("HITS: ");
            for(i=0;i<MMLEN+1;i++)  {        printf(" %u |", hits[i]);}
            printf(" tot: %u\n", hits[i]);
            memset(hits,0,sizeof(hits));
        }
*/
        if((artn->artnode->all % 1000000) == 0)
        {
        /*
            time(&now);
            ts = *localtime(&now);
            strftime(tsbuf, sizeof(tsbuf), "%T", &ts);
            printf("%s A:%u|P:%u|M:%u\n",tsbuf, artn->artnode->all, artn->artnode->packetsCnt, artn->artnode->miss);
           */
            artn->artnode->all=0;
            artn->artnode->miss=0;
            artn->artnode->packetsCnt=0;
        }
        //if(sock_ret>0)

        if(msgcnt>0)
        {
            trm.msg_cnt = msgcnt;
            clock_gettime(CLOCK_REALTIME, &trm.ts);
            post_msg(&ev_pb->rq_head, &trm, sizeof(trace_msg_t));
            art_net_pack_t* a;
            n->last_rx = trm.ts;
            //msgRezervedPostNB(pb, cn);
            //msgWritten(&pb->rq_head);
            /*printf("Prod: got %u msgs\n", msgcnt);
            for(i=0;i<msgcnt;i++)
            {
                a = iovecs[i].iov_base ;
                printf("Slot %u, uni %u, head %s\n", i, a->ArtDmxOut.a_net.SubUni.subuni_full, a->head.id);

            }
            //printf("Prod: got %u msgs\n", msgcnt);*/
            //hits[msgcnt]++;
            /*send messages */
            msgWrittenMulti(&pb->rq_head,  msgcnt);
            do
            {
                msg_need = rezerveMsgMulti(&pb->rq_head,packs, MMLEN );
                for(i=0;i<msg_need;i++)
                {
                    p = packs[i];
                    if(p==NULL) break;
                    p->mtyp = msg_typ_socket_data;
                    iovecs[i].iov_base = &p->pl.art;
                    msgs[i].msg_hdr.msg_name = &p->sender;
                    msgs[i].msg_hdr.msg_namelen = sizeof(struct sockaddr_in);
                }
                msg_need = i;
            }while(msg_need<1);  // if we managed to get at least 1 slot proceed
            artn->artnode->all+=msgcnt;
            artn->artnode->packetsCnt+=msgcnt;
            /*totmsges+=msgcnt;
            if(totmsges >=500)
            {
                p = &rq_data[0];

                for(i=0;i<totmsges;i++)
                {
                    a = &p->pl.art;
                    printf("Slot %u, uni %u, head %s\n", i, a->ArtDmxOut.a_net.SubUni.subuni_full, a->head.id);
                    p++;
                }
            }*/
            //cn =msgRezerveNB(pb);
            //p = rezerveMsg(pb);
            //if((++pCnt%100)==0)printf("Have %u packets\n", pCnt);
            if((n->sm.state == working_e) && (sm_state_cache != working_e))
            {
                if(n->sm.active_unis & 1)
                {
                    if(n->sm.active_unis>(UNI_PER_OUT * MIMAS_STREAM_OUT_CNT))
                    {
                        batch = UNI_PER_OUT;
                    }
                    else
                    {
                        batch  = n->sm.active_unis;
                        batch++;
                        batch>>=1;
                    }
                }
                else
                {
                    if(n->sm.active_unis>(UNI_PER_OUT * MIMAS_STREAM_OUT_CNT))
                    {
                        batch = UNI_PER_OUT;
                    }
                    else
                    {
                        batch  = n->sm.active_unis/2;
                    }
                }
                printf("Batch set to %d\n", batch);
            }
        }
        else
        {
            batch = 1;
            if(msgcnt>-1)
            {
                printf("msgs = 0\n");
                artn->artnode->all++;
                artn->artnode->miss++;
                //hits[0]++;
                continue;
            }
            else
            {
                int  lockFree=0;
                int tries=0;
                int errLocal;
                struct timespec sockerrTs;
                clock_gettime(CLOCK_REALTIME, &sockerrTs);
                setSockTimout(n->sockfd, 0);
                n->sm.DataOk = 0;
                sys_event_msg_t *sysev;
                peer_pack_t* p;
                do{
                    p =rezerveMsg(&pb->rq_head);
                    usleep(5);
                }while(p == NULL);
                p->mtyp = msg_typ_sys_event;
                p->sys_ev.ev_type = sys_ev_socket_timeout;
                p->sys_ev.data1 = n->sockfd;
                msgWritten(&pb->rq_head);
                if(errno != EAGAIN)
                {

                    errLocal = show_socket_error_reason(n->sockfd);
                    printf("================\nUnhandler Socket error:\n\terrno %d, sock ret %d, fd %d, errLocal %d\n================\n", errno, msgcnt, n->sockfd, errLocal);
                    SmReset(n, eResetUnknown);
                }
                else
                {
                    do
                    {
                        lockFree =  pthread_spin_trylock(&pb->rq_head.lock) ;
                    }while((lockFree !=0  )&&(tries++<100));
                    SmReset(n, eResetSockTimeout);
                    time(&now);
                    ts = *localtime(&now);
                    strftime(tsbuf, sizeof(tsbuf), "%T", &ts);
                    printf("================\nData Timeout(EAGAIN,lock %d) %s\n================\n",lockFree,tsbuf);
                    if(lockFree==0)pthread_spin_unlock(&pb->rq_head.lock) ;
                }
                errno = 0;
                msgcnt = 0;
                mimas_all_black(&outs);
                usleep( 5000ul );
                printf("Last rx %ld.%09ld\nLast pp %ld.%09ld\nLast mr %ld.%09ld\nSock er %ld.%09ld\n================\n", \
                n->last_rx.tv_sec,n->last_rx.tv_nsec, \
                n->last_pac_proc.tv_sec,n->last_pac_proc.tv_nsec, \
                n->last_mimas_ref.tv_sec,n->last_mimas_ref.tv_nsec, \
                sockerrTs.tv_sec,sockerrTs.tv_nsec);
            }
        }
    }
}

/* initialize mainPO and eventQ*/
int initMessaging(void)
{
    memset((void*)(ev_q), 0, sizeof(ev_q));
    memset((void*)(rq_data), 0, sizeof(rq_data));
    ev_pb = createPB(EV_Q_DEPTH,"EVQ",ev_q, sizeof(ev_q));
    /*post_box_t* mainPB =createPB(EV_Q_DEPTH,"MAINPB", (void*)&rq_data[0], );
    if(mainPB)
    {
        setDefPB(mainPB);
        return(0);
    }
    return(-1);*/
    return( ev_pb!=NULL?0:-1);
}

void pmwTest();

int main(void)
{
    int res;
    setHandler(fault_handler);
    printf("BCM lib version = %u\n", bcm2835_version());
  //  bcm2835_set_debug(1);
    int rc = bcm2835_init();
    int i;
    //testLists();
    if(rc!=1)
    {
        printf("Error %d init bcm2835\n", rc);
        return(-1);
    }

    initMessaging();
    anetp->artPB = createPB(RQ_DEPTH,"ArtNetPB", (void*)&rq_data[0], sizeof(rq_data));
    /*ascnp->artPB = createPB(RQ_DEPTH,"sACNPB", (void*)&rq_data[0],  sizeof(rq_data));*/
    anetp->artnode = createNode(protoArtNet);
    /*ascnp->artnode = createNode(protosACN);*/
    init_mimas_vdevs();
    InitOuts();
    initSPI();

    do
    {
        res = initMimas();
        if(res!=0) sleep(1);
    }while(res!=0);
    mimas_all_black(&outs);
    usleep(100000);
    pthread_t pwm_tst_th;
    pthread_create(&pwm_tst_th,NULL,pmwTest,NULL);
    printf("Started PWM Test thread\n");
    rc = pthread_setname_np(pwm_tst_th, "PixLedPwmHan\0");
    if(rc)
    {
        perror("Producer1 thread rename failed");
    }
    //getInterfaces();
    //getInterfaces();
    socketStart(anetp->artnode, ARTNET_PORT);
    /*socketStart(ascnp->artnode, ACN_SDT_MULTICAST_PORT);*/
    //NodeInit(anetp, (GLOBAL_OUTPUTS_MAX * UNI_PER_OUT), 0x11);
    NodeInit(anetp, (64), 0x11);
    art_set_node(anetp->artnode);
    anetp->artnode->intLimit = 50;
    SmReset(anetp->artnode,eResetInit);

    pthread_create(&anetp->artnode->con_tid, NULL, consumer,(void*)anetp);
    rc = pthread_setname_np(anetp->artnode->con_tid, "PixLedCons\0");
    if(rc)
    {
        perror("Consumer1 thread rename failed");
    }

    pthread_create(&anetp->artnode->prod_tid, NULL, producer,(void*)anetp);
    rc = pthread_setname_np(anetp->artnode->prod_tid, "PixLedProd\0");
    if(rc)
    {
        perror("Producer1 thread rename failed");
    }
    pthread_t time_one_sec;
    pthread_create(&time_one_sec,NULL,one_sec,(void*)anetp);
    rc = pthread_setname_np(time_one_sec, "PixLedStats\0");
    if(rc)
    {
        perror("Consumer1 thread rename failed");
    }

    printf("Starting Web Server...\n");
    while(1)
    {
        webServStart();
        printf("Web Server exited, restarting...\n");
        sleep(1);
    }
    return(0);
}
/*
    in_addr_t sec_ip;
    struct ifaddrs ipres;
    sock_sec = socket_init(NULL);
    if(sock_sec>-1)
    {
        add_IP_Address("2.250.250.1");
        sec_ip =  inet_addr("2.250.250.1");
        rc = sock_bind(sock_sec, "wlan0",&sec_ip, ARTNET_PORT);
    }
*/


void pmwTest()
{
    pwm_group_data_t pwm_d[MIMAS_PWM_GROUP_CNT];
    memset((void*)pwm_d, 0, sizeof(pwm_group_data_t)* MIMAS_PWM_GROUP_CNT);
    post_box_t* pb = createPB(PWM_Q_DEPTH,"PwmPB", (void*)&pwm_rq[0], sizeof(pwm_rq));
    pwm_out_dev_t* pwms[MIMAS_PWM_GROUP_CNT];
    pwm_cmd_msg_t  pwm_msg;
    uint8_t enable_msk[MIMAS_PWM_GROUP_CNT];
    pwms[0] = getMimasPwmDevices();
    pwms[1]  = pwms[0]+1;
    pwm_pb = pb;
    peer_pack_t *pkt;
    peer_pack_t pack;
    int devIdx[MAX_VDEV_CNT];
    int devCnt, j, k;
    gen_addres_u    raw_addr;
    pwm_vdev_t *cdev;

    for(j=0;j<MIMAS_PWM_GROUP_CNT;j++)
    {
        pwm_d[j].gctrl =1;
        pwm_d[j].div = 39u;
        pwm_d[j].gper = 39999u;
        for(k=0;k<MIMAS_PWM_OUT_PER_GRP_CNT;k++)
        {
            pwm_d[j].ch_pers[k] = 4375;
            pwm_d[j].ch_ctrls[k] = 1;
        }
    }
    pwm_msg.mtyp = msg_typ_pwm_cmd;
    pwm_msg.cmd = ALL_PWM_CMDS;
    pwm_msg.grp_bm = PWM_GRP_ALL;
    pwm_msg.start_ch[0] = 0;
    pwm_msg.start_ch[1] = 0;
    pwm_msg.ch_count[0] = MIMAS_PWM_OUT_PER_GRP_CNT;
    pwm_msg.ch_count[1] = MIMAS_PWM_OUT_PER_GRP_CNT;
    pwm_msg.data[0] = pwm_d[0];
    pwm_msg.data[1] = pwm_d[1];
    post_msg(&pb->rq_head,(void*)&pwm_msg,sizeof(pwm_cmd_msg_t));

    int rc;
    struct timespec ts[4];

    pkt = &pack;
    do
    {
        do
        {
            rc = getCopyMsg(&pb->rq_head, pkt);
            //pkt = getMsg(& (*pb).rq_head);
            //if(pkt == NULL)usleep(10);
            if(rc<0)usleep(100);
        //}while(pkt == NULL);
        }while(rc<0);
        clock_gettime(CLOCK_REALTIME, &ts[0]);
        switch(pkt->mtyp)
        {
            case msg_typ_socket_data:
            {
                art_net_pack_t* dat = &pkt->pl.art;
                art_resp_e  art_res;
                art_res = ArtNetDecode(dat);
                cdev = NULL;
                //printf("Cons: got msg, artres = %u, uni = %u\n", art_res, ap->ArtDmxOut.a_net.SubUni.subuni_full);
                switch(dat->ArtDmxOut.head.OpCode.OpCode)
                {
                    case OpDmx:
                    {




                        raw_addr.anet = dat->ArtDmxOut.a_net;
                        devCnt = findVDevsAtAddr(raw_addr.addr, &devIdx);
                        uint16_t tVal;
                        uint8_t* datapt;
                        uint8_t  chId;
                        pwm_ch_data_t* cd;
                        pwm_out_dev_t* hpwm = GET_PWMS_PTR;
                       // printf("found %d pwmDevices\n", devCnt);
                        for(k=0;k<devCnt;k++)
                        {
                            cdev = devList.devs[devIdx[k]].pwm_vdev;
                            if ((cdev!=NULL) && (cdev->com.dev_type == pwm_dev))
                            {
                                // send to mimas
                                chId = cdev->hwStartIdx;
                                datapt = &dat->ArtDmxOut.dmx[cdev->com.start_offset];

                                for(j=0;j< cdev->ch_count; j++)
                                {

                                    cd = &hpwm[chId/MIMAS_PWM_OUT_PER_GRP_CNT].ch_data[chId&7];
                                    tVal = pwmMapValue(cdev , j, datapt);
                                    if(tVal!=pwm_d[cd->phyGrp].ch_pers[cd->phyIdx])
                                    {
                                        if(pwm_d[cd->phyGrp].needUpdate == 0)
                                        {
                                            pwm_d[cd->phyGrp].needUpdate = 1;
                                            //TODO: if a vDev spans across 2 hwDevs, verify chId
                                            pwm_d[cd->phyGrp].startUpdIdx = chId & (BIT8(MIMAS_PWM_OUT_PER_GRP_CNT) - 1u);
                                        }
                                        pwm_d[cd->phyGrp].ch_pers[cd->phyIdx] = tVal;
                                        pwm_d[cd->phyGrp].UpdChCount++;
                                        //printf("Dev %d, ch %d inV %u outV %u\n", k, j, bswap_16(*(uint16_t*)datapt), tVal);
                                    }
                                    //setPwmVal(cdev->pwm_vdev , j, datapt);
                                    if(cd->_16bits!=0)datapt++;
                                    datapt++;
                                    chId++;
                                }
                             }
                        }
                        clock_gettime(CLOCK_REALTIME, &ts[1]);
                        for(k=0;k<MIMAS_PWM_GROUP_CNT;k++)
                        {
                            if(pwm_d[k].needUpdate)
                            {
                                j = mimas_store_pwm_val( \
                                                BIT8(k), \
                                                pwm_d[k].startUpdIdx \
                                                ,&pwm_d[k].ch_pers[pwm_d[k].startUpdIdx], \
                                                pwm_d[k].UpdChCount);
                                if(j)
                                {
                                    printf("mimas_send_err % d in %d\n",j, __LINE__);
                                }
                                else
                                {
                                    //printf("sending PWM data to mimas vDev %d\n",j);
                                }
                                pwm_d[k].UpdChCount = 0;
                                pwm_d[k].needUpdate = 0;
                            }
                        }
                        clock_gettime(CLOCK_REALTIME, &ts[2]);
/*
                        for(k=0;k<3;k++)
                        {
                            printf("%05lu:%09lu (%d)\n",ts[k].tv_sec, ts[k].tv_nsec,k);
                        }
*/

                        break;
                    }
                    default:
                    {

                    }
                }
                break;
            }
            case msg_typ_sys_event:
            {
                break;
            }
            case msg_typ_pwm_cmd:
            {
                pwm_cmd_msg_t* msg = (pwm_cmd_msg_t*)pkt;
                int mrc;
                pwm_out_dev_t* hpwm = GET_PWMS_PTR;
                if(msg->cmd & pwm_set_gper_cmd)
                {
                    if(msg->grp_bm & PWM_GRP_A) {
                        mrc = mimas_store_pwm_period(PWM_GRP_A, msg->data[0].gper);
                        hpwm[0].gperiod = msg->data[0].gper;
                        if(mrc) printf("mimas_send_err in %d\n", __LINE__);
                    }
                    if(msg->grp_bm & PWM_GRP_B) {
                        mrc = mimas_store_pwm_period(PWM_GRP_B, msg->data[1].gper);
                        hpwm[1].gperiod = msg->data[1].gper;
                        if(mrc) printf("mimas_send_err in %d\n", __LINE__);
                    }
                }
                if(msg->cmd & pwm_set_div_cmd)
                {
                    if(msg->grp_bm & PWM_GRP_A) {;
                        mrc = mimas_store_pwm_div(PWM_GRP_A, msg->data[0].div);
                        hpwm[0].div = msg->data[0].div;
                        if(mrc) printf("mimas_send_err in %d\n", __LINE__);
                    }
                    if(msg->grp_bm & PWM_GRP_B) {
                        mrc = mimas_store_pwm_div(PWM_GRP_B, msg->data[1].div);
                        hpwm[1].div = msg->data[1].div;
                        if(mrc) printf("mimas_send_err in %d\n", __LINE__);
                    }
                }
                if(msg->cmd & pwm_set_gctrl_cmd)
                {
                    if(msg->grp_bm & PWM_GRP_A) {
                        mrc = mimas_store_pwm_gCntrol(PWM_GRP_A, msg->data[0].gctrl);
                        hpwm[0].gEnable = msg->data[0].gctrl;
                        if(mrc) printf("mimas_send_err in %d\n", __LINE__);
                    }
                    if(msg->grp_bm & PWM_GRP_B) {
                        mrc = mimas_store_pwm_gCntrol(PWM_GRP_B, msg->data[1].gctrl);
                        hpwm[1].gEnable = msg->data[1].gctrl;
                        if(mrc) printf("mimas_send_err in %d\n", __LINE__);
                    }
                }
                if(msg->cmd & pwm_set_ch_per_cmd)
                {
                    if(msg->grp_bm & PWM_GRP_A) {
                        mrc = mimas_store_pwm_val(PWM_GRP_A, msg->start_ch[0],&msg->data[0].ch_pers[msg->start_ch[0]],msg->ch_count[0]);
                        // need update hwpwm
                        if(mrc) printf("mimas_send_err in %d\n", __LINE__);
                        }
                    if(msg->grp_bm & PWM_GRP_B) {
                        mrc = mimas_store_pwm_val(PWM_GRP_B, msg->start_ch[1],&msg->data[1].ch_pers[msg->start_ch[1]],msg->ch_count[1]);
                        // need update hwpwm
                        if(mrc) printf("mimas_send_err in %d\n", __LINE__);
                    }
                }
                if(msg->cmd & pwm_set_ch_ctrl_cmd)
                {
                    if(msg->grp_bm & PWM_GRP_A) {
                        mrc = mimas_store_pwm_chCntrol(PWM_GRP_A, msg->start_ch[0],&msg->data[0].ch_ctrls[msg->start_ch[0]],msg->ch_count[0]);
                        // need update hwpwm
                        if(mrc) printf("mimas_send_err in %d\n", __LINE__);
                    }
                    if(msg->grp_bm & PWM_GRP_B) {
                        mrc = mimas_store_pwm_chCntrol(PWM_GRP_B, msg->start_ch[1],&msg->data[1].ch_ctrls[msg->start_ch[1]],msg->ch_count[1]);
                        // need update hwpwm
                        if(mrc) printf("mimas_send_err in %d\n", __LINE__);
                    }
                }
                break;
            }
            default:
            {
                printf("unknown msg_typ\n");
            }
        }
       // usleep(5000ul);
    }while(1);
}
