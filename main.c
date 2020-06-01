#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <bcm2835.h>
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
//#include <net/if.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <sched.h>
#include <sys/ucontext.h>
#include <ucontext.h>
#include <execinfo.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <string.h>
#include <byteswap.h>

#include "rq.h"
#include "protocolCommon.h"
#include "utils.h"
#include "type_defs.h"
#include "mimas_cfg.h"
#include "vdevs.h"
#include "mimas_cmd_defs.h"
#include "bm_handling.h"

#define gettid syscall(SYS_gettid)




pthread_spinlock_t  prnlock=0;
volatile int upd_pwm = 0;

out_def_t           outs[MIMAS_STREAM_OUT_CNT ];
sock_data_msg_t     sock_pkt_pool[RQ_DEPTH];
sockdat_ntfy_t      sock_rq[RQ_DEPTH];
sock_data_msg_t     pix_rq[RQ_DEPTH];
sock_data_msg_t     pwm_rq[PWM_Q_DEPTH];
trace_msg_t         ev_q[EV_Q_DEPTH];
post_box_t*         sock_pb = NULL;
post_box_t*         ev_pb = NULL;
post_box_t*         pwm_pb = NULL;
post_box_t*         pix_pb = NULL;
post_box_t*         pkt_pb = NULL;
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
	print_trace();
	//print_trace_gdb();
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
    cfg.com.start_address = 57;
    cfg.com.start_offset = 0;
    cfg.ch_count = 4;
    cfg.hwGrpIdx = PWM_GRP_A;
    cfg.hwStartIdx= 0;
    for(i=0;i<cfg.ch_count;i++)
    {
        cfg.chCfg[i].lims.minV = 1450u;//9000u;
        cfg.chCfg[i].lims.maxV = 6500u;//26000u;
        cfg.chCfg[i].chCtrl = PWM_CH_EN |  PWM_CH_16B | PWM_CH_BSW;
    }
    res = build_dev_pwm(&pdev, &cfg);
    cfg.com.start_address = 57;
    cfg.com.start_offset = 0;
    cfg.ch_count = 2;
    cfg.hwGrpIdx = PWM_GRP_B;
    cfg.hwStartIdx=0;
    cfg.chCfg[0].lims = pwm_def_limits_c;
    cfg.chCfg[0].chCtrl = PWM_CH_EN |  PWM_CH_16B | PWM_CH_BSW;
    cfg.chCfg[0].lims.midV = 10;
    res = build_dev_pwm(&pdev, &cfg);

    ws_pix_vdev_t   vd;
    vd.pixel_count = 1500;
    vd.pix_per_uni = 150;
    vd.col_map = grb_map_e;
    vd.com.start_address = 17;
    res =build_dev_ws(&vd);
    vd.pixel_count = 4500;
    vd.pix_per_uni = 150;
    vd.com.start_address = 80;
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
            outs[i].uniLenLimit[j] = (DEF_PIX_PER_UNI * DEF_CHAN_PER_PIX);
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
    }
}


int sendOutToMimas(int oSel)
{
    return(mimas_store_packet(oSel,(uint8_t*)&outs[oSel].mpack,outs[oSel].dlen));
}

void* consumer(void* d)
{
    uint8_t             check_unis;
    int                 devCnt, devIdx[MAX_VDEV_CNT];
    uint32_t idx, i,    attention;
    peer_pack_t         ppack;
    peer_pack_t *pkt =  &ppack;
    uint64_t            peer_id;

    app_node_t *artn = (app_node_t*)d;
    post_box_t* pb = artn->artPB;
    node_t *me = artn->artnode;
    sm_t* sm = &me->sm;
    node_branch_t branches[5];
    node_branch_t* pwmBr = &branches[0];
    node_branch_t* pixBr = &branches[1];
    node_branch_t* localBr =&branches[2];
    node_branch_t* dropBr = &branches[3];
    node_branch_t* miscBr = &branches[4];
    int  rc;
    uint16_t start_bm;
    fl_t cn, cnn;
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
    timeloop=0;
    timesum =0;
    timesum_mimas=0;
    timesum_proc=0;
    trace_msg_t trm[5];
    trm[cons_msg_pop].ev            = cons_msg_pop;
    trm[cons_msg_proced].ev         = cons_msg_proced;
    trm[cons_pack_full].ev          = cons_pack_full;
    trm[cons_mimas_refreshed].ev    = cons_mimas_refreshed;
    trm[cons_mimas_start].ev        = cons_mimas_start;
    uint32_t popcnt=0;
    mimas_out_dev_t *cdev;
    trace_msg_t *trms[5];
    trms[0]=&trm[0];
    trms[1]=&trm[1];
    trms[2]=&trm[2];
    trms[3]=&trm[3];
    trms[4]=&trm[4];
 #define handle_error_en(en, msg) \
               do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

   /* Check the actual affinity mask assigned to the thread */

   ret = pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
   if (ret != 0)
       handle_error_en(ret, "pthread_getaffinity_np");

   printf("Get returned by pthread_getaffinity_np() contained:\n");
   int j =1;
   for (i = 0; i < 8; i++)
   {
       if((cpuset.__bits[0] & j) !=0)
           printf("CPU %d, ", i);
        j<<=1;
    }

    printf("CONSUMER TID = %u\n", (uint32_t)gettid);
    ret = setpriority(which, gettid,  CONS_PRIO_NICE );
    printf("Consumer set_nice to (%i) returns %d\n",CONS_PRIO_NICE, ret);
    struct timespec ts_now;
    long elapsed;
    int cause = 0;
    clock_getres(CLOCK_PROCESS_CPUTIME_ID, &timers[1]);
    printf("ClockRes = %lusec, %u nsec\n", timers[1].tv_sec, timers[1].tv_nsec);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &timers[idle_e]);
    t1 = clock();
    fl_t nodes;
    post_box_t* pb_owner;
    sockdat_ntfy_t* msg;
    int ticks_to_sleep;
    check_unis = 0;
    ticks_to_sleep = 6;
    clock_gettime(CLOCK_REALTIME, &trms[0]->ts);
    while(1)
    {
        cdev = NULL;

        memset((void*)branches, 0, (sizeof(node_branch_t)* 5));
        do
        {
            pkt = getMsg(&sock_pb->rq);
            if(pkt == NULL)
            {
                if(ticks_to_sleep<5)ticks_to_sleep++;
                if(ticks_to_sleep == 5)
                {
                    clock_gettime(CLOCK_REALTIME, &ts_now);
                    elapsed = nsec_diff(&ts_now, &trms[0]->ts);
                    if(elapsed > CONS_TOV)
                    {
                        clearBM(devList.tmp_uni_map);
                        clearBM(devList.glo_uni_map);
                        check_unis=0;
                        ticks_to_sleep++;
                        trms[0]->ts = ts_now;
                        prnFinf(log_con, "Elapsed ovf : %ld nsec\n", elapsed );
                    }
                }
            }
        }while(pkt == NULL);
        clock_gettime(CLOCK_REALTIME, &trms[0]->ts);
        ticks_to_sleep = 0;
        switch(pkt->genmtyp)
        {
            case msg_typ_socket_data:
            {
                msgRead(&sock_pb->rq);
                printf("Unexpected msg\n");
                continue;
                break;
            }
            case msg_typ_socket_ntfy:
            {
                trms[0]->msg_cnt = popcnt++;
                post_msg(&ev_pb->rq, trms[0],sizeof(trace_msg_t));
                nodes = pkt->dataNtfy.datapt;
                pb_owner = pkt->dataNtfy.rq_owner;
                prnLLdetail(nodes, "CONS", "ConsRcved");
                msgRead(&sock_pb->rq);
                break;
            }
            case msg_typ_sys_event:
            {
                switch(pkt->sys_ev.ev_type)
                {
                    case sys_ev_socket_timeout:
                    {
                        printf("Socket TimeOut received on sock %d\n", pkt->sys_ev.data1);
                        break;
                    }
                    default:
                    {
                        printf("Socket event %d received on sock %d\n", (uint32_t)pkt->sys_ev.ev_type, pkt->sys_ev.data1);
                        break;
                    }
                }
                msgRead(&sock_pb->rq);
                continue;
                break;
            }
            default:
            {
                printf("Unknown msgType received : %u\n", (uint32_t)pkt->genmtyp);
                msgRead(&sock_pb->rq);
                continue;
                break;
            }
        }
        cn = nodes;
        sock_data_msg_t* dt;
        int nodecnt = 0;
        if(check_unis && (devList.glo_uni_map->reserved < check_unis) )
        {
            if(devList.glo_uni_map->reserved>devList.tmp_uni_map->reserved)
            {
                prn(log_finf,log_con,"Uni decreased from %u to %u\n",devList.glo_uni_map->reserved, devList.tmp_uni_map->reserved);
                bm_t* bm = devList.glo_uni_map;
                devList.glo_uni_map = devList.tmp_uni_map ;
                devList.tmp_uni_map = bm;

            }
            clearBM(devList.tmp_uni_map);
            check_unis=0;
        }
        taken_e taken;
        while(cn)
        {
            nodecnt++;
            dt = cn->item.pl.msg;
            peer_id = (*(uint64_t*)&dt->sender);
            ap =  &dt->pl.art;
            art_res = ArtNetDecode(ap);
            switch(art_res)
            {
                case art_data_e:
                {
                    raw_addr.anet = ap->ArtDmxOut.a_net;
                    check_unis++;
                    taken = updateBM(devList.tmp_uni_map,bm_reserved_e,raw_addr.addr);
                    if(taken == bm_free_e)
                    {
                        check_unis=0;
                        prnDbg(log_con,"Added uni %d to tmp, rez = %u\n",raw_addr.addr, devList.tmp_uni_map->reserved);
                    }
                    if(devList.tmp_uni_map->reserved>devList.glo_uni_map->reserved)
                    {
                        taken = updateBM(devList.glo_uni_map,bm_reserved_e,raw_addr.addr);
                        if(taken == bm_free_e)
                        {
                            prnDbg(log_con,"Added uni %d to glo, rez = %u\n",raw_addr.addr, devList.glo_uni_map->reserved);
                            prnFinf(log_con,"Receiving %u universes\n", devList.glo_uni_map->reserved);
                        }
                    }

                    devCnt = findVDevsAtAddr(raw_addr.addr, devIdx);

                    if(devCnt <1)
                    {
                        addToBranch(dropBr,cn);
                        cn = cn->nxt;
                        continue;
                    }
                    vdevs_e devdstinct = unused_dev;
                    for(i=0;i<devCnt;i++)
                    {
                         cdev = &devList.devs[devIdx[i]];
                         vDevSetPeer(peer_id, devIdx[i]);

                        if (cdev==NULL) continue;
                        if( devdstinct & GET_VDEV_TYPE(*cdev))continue; // just get distinct devices for this pkt/address
                        switch(GET_VDEV_TYPE(*cdev))
                        {
                            case ws_pix_dev:
                            {
                                cn->item.pl.vDevId = devIdx[i];
                                addToBranch(pixBr,cn);
                                devdstinct|=ws_pix_dev;
                                break;
                            }
                            case pwm_dev:
                            {
                                cn->item.pl.vDevId = devIdx[i];
                                addToBranch(pwmBr,cn);
                                devdstinct|=pwm_dev;
                                break;
                            }
                            default:
                            {
                                prnErr(log_con,"Unknown Devtype for addr %u devIdx %u\n", raw_addr.addr, devIdx);
                                break;
                            }
                        } // end of switch devType
                    }   // end of devices loop
                    break;
                } // end of art_res switch
                case art_poll_e:
                {
                        addToBranch(localBr, cn);
                        break;
                }
                case art_syn_pack_e:
                {
                    prnErr(log_con, "A SYNC!!!!\n");
                    addToBranch(dropBr,cn);
                    break;
                }
                default:
                {
                    prnErr(log_con,"\t\t\t\tConsumed Other ArtNetPack %d :  Pkt.idx%d\n",(int)art_res,  cn->item.pl.itemId);
                    addToBranch(dropBr,cn);
                    //putNode(cn->pb, cn, &cnn);
                    //cn = cnn;
                }
            }
            cn = cn->nxt;
        } // end of cn loop
        prnDbg(log_con,"Got %d nodes in Ntfy, uni Received = %u of %u\n",nodecnt, devList.glo_uni_map->reserved, devList.glo_uni_map->elements);

        if(pwmBr->hd)
        {
            sockdat_ntfy_t pwmmsg;
            pwmmsg.mtyp = msg_typ_socket_ntfy;
            pwmmsg.rq_owner = pwmBr->hd->pb;
            pwmmsg.datapt = pwmBr->hd;
            pwmBr->lst->nxt  = NULL;
            prnLLdetail(pwmBr->hd, "CONS", "PwmLL");
            post_msg(&pwm_pb->rq,&pwmmsg,sizeof(sockdat_ntfy_t));
        }
        if(pixBr->hd)
        {
            sockdat_ntfy_t pixmsg;
            pixmsg.mtyp = msg_typ_socket_ntfy;
            pixmsg.rq_owner = pixBr->hd->pb;
            pixmsg.datapt = pixBr->hd;
            pixBr->lst->nxt  = NULL;
            prnLLdetail(pixBr->hd, "CONS", "PixLL");
            post_msg(&pix_pb->rq,&pixmsg,sizeof(sockdat_ntfy_t));
            prnDbg(log_con,"Sending pix Pkt to handler\n");
        }
        if(dropBr->hd)
        {
            dropBr->lst->nxt  = NULL;
            prnLLdetail(dropBr->hd, "DROPS", "Drops");
            j = putNodes(dropBr->hd->pb, dropBr->hd);
            prnDbg(log_con,"Dropped %d unamapped Pkts\n",j);
        }
        if(localBr->hd)
        {
            localBr->lst->nxt = NULL;
            make_artnet_resp(localBr->hd->item.pl.msg);
            j = putNodes(localBr->hd->pb, localBr->hd);
        }

    }
        //printf("Cons: got msg, artres = %u, uni = %u\n", art_res, ap->ArtDmxOut.a_net.SubUni.subuni_full)
}

void *pix_handler(void* dat)
{
    post_box_t*         pb = pix_pb;
    peer_pack_t*        pkt;
    #define handle_error_en(en, msg) \
               do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

   /* Check the actual affinity mask assigned to the thread */
   cpu_set_t cpuset;
   int ret;
   ret = pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
   if (ret != 0)
       handle_error_en(ret, "pthread_getaffinity_np");

   int j =1;
   int i;
   for (i = 0; i < 8; i++)
   {
       if((cpuset.__bits[0] & j) !=0)
           printf("CPU %d, ", i);
        j<<=1;
    }

    prnFinf(log_pix, "PIXHAN TID = %u\n", (uint32_t)gettid);
    ret = setpriority(PRIO_PROCESS, gettid,  PIXHAN_PRIO );
    prnFinf(log_pix, "PIXHAN set_nice to (%i) returns %d\n",PIXHAN_PRIO, ret);

    prnFinf(log_pix,"PixHandler started...\n");
    int* devs = NULL;
    int devCnt;
    mimas_out_dev_t** vDevs;
    mimas_out_dev_t* cdev;
    vdev_sm_t*  sm;
    pix_dev_pt      pixDev;
    int hwId=-1;
    uint16_t    mimas_start_bm=0;
    addressing_t    absAddr, relAddr;
    do
    {
        devCnt = findVDevsOfType(ws_pix_dev,NULL);
        if(devCnt<1) usleep(100000ul);
    }while(devCnt<1);
    devs = (int*)calloc(devCnt,sizeof(int));
    vDevs = (mimas_out_dev_t*)calloc(devCnt,sizeof(mimas_out_dev_t*));
    findVDevsOfType(ws_pix_dev,devs);
    for(i=0;i<devCnt;i++)
    {
        cdev = &devList.devs[devs[i]];
        vDevs[i] = cdev;
        for(j=0;j<cdev->sub_dev_cnt;j++)
        {
            pixDev = cdev->pix_devs[j];
            hwId=pixDev->out_start_id;
            outs[hwId].fullMap = pixDev->com.vdsm.expected_full_map;
        }
        //pixDev->com = cdev->dev_com;
        //sm = &cdev->dev_com.vdsm;
    }

    while(1)
    {
        do
        {
            pkt = getMsg(&pb->rq);
            usleep(2ul);
        }while(pkt == NULL);
        switch(pkt->genmtyp)
        {
            case msg_typ_socket_ntfy:
            {
                fl_t cn = pkt->dataNtfy.datapt;
                fl_t cnn;
                sock_data_msg_t* dt;
                art_net_pack_t* ap;
                post_box_t* rq_owner = pkt->dataNtfy.rq_owner;
                //msgRead(&pb->rq);
                prnDbg(log_pix,"msg_typ_socket_ntfy msg received\n");
                while(cn)
                {
                    dt = cn->item.pl.msg;
                    ap =  &dt->pl.art;
                    absAddr = ap->ArtDmxOut.a_net.raw_addr ;
                    i = findVDevAtAddr(absAddr);
                    if(i<0)
                    {
                        prnErr(log_pix,"No vDev found for addr %u\n",absAddr);
                        putNode(cn->pb,cn, &cn);
                        continue;
                    }
                    cdev = &devList.devs[i];
                    sm = &cdev->dev_com.vdsm;
                    hwId = -1;
                    for(i=0;i<cdev->sub_dev_cnt;i++)
                    {
                        pixDev = cdev->pix_devs[i];
                        relAddr = absAddr - pixDev->com.start_address;
                        if(pixDev->com.end_address >= absAddr)
                        {
                            hwId = pixDev->out_start_id;

                            if(pixDev->com.vdsm.curr_map & BIT64(relAddr))
                            {
                                prnErr(log_pix,"Duplicate Universe %u\n", absAddr);
                            }
                            pixDev->com.vdsm.curr_map|= BIT64(relAddr);
                            sm->curr_map|= BIT64(relAddr) << (i * UNI_PER_OUT);
                            outs[hwId].dlen+=pixDev->pix_per_uni * (int)pixDev->colCnt;
                            break;
                        }
                    }

                    mapColor(&ap->ArtDmxOut.dmx,&outs[hwId],relAddr);
                    prnDbg(log_pix,"Consumed item %d inPixHandler addr %u\n", cn->item.pl.itemId, absAddr);
                    putNode(cn->pb,cn, &cn);
                    if( (outs[hwId].fullMap>0) && (outs[hwId].fillMap == outs[hwId].fullMap) )
                    {
                        i = sendOutToMimas(hwId);
                        prnDbg(log_pix,"Sent data to mimas for port %d, rc = %d\n", hwId, i);
                        mimas_start_bm|=BIT8(hwId);
                    }
                    if(sm->curr_map == sm->expected_full_map)
                    {
                        prnDbg(log_pix, "Full pix frame received\n");
                        i=mimas_refresh_start_stream(mimas_start_bm,0x00C0);
                        if(i!=0)
                        {
                            prnErr(log_pix,"Sent refresh to mimas bm:%X, rc = %d\n", mimas_start_bm, i);
                        }
                        else
                        {
                            prnDbg(log_pix,"Sent refresh to mimas bm:%X\n", mimas_start_bm);
                        }
                        i=0;
                        while(mimas_start_bm)
                        {
                            if(mimas_start_bm & 1)
                            {
                               // outs[i].fillMap = 0;
                               // outs[i].dlen = 0;
                            }
                            i++;
                            mimas_start_bm>>=1;
                        }
                        sm->curr_map = 0ull;
                        for(i=0;i<cdev->sub_dev_cnt;i++)
                        {
                            pixDev = cdev->pix_devs[i];
                            pixDev->com.vdsm.curr_map = 0;
                            hwId = pixDev->out_start_id;
                            outs[hwId].fillMap = 0;
                            outs[hwId].dlen = 0;
                        }
                    }

                }
                break;
            }
            /*
            case msg_typ_socket_data:
            {
                break;
            }
            case msg_typ_sys_event:
            {
                break;
            }
            case msg_typ_pwm_cmd:
            {
                break;
            }
            */
            default:
            {
                prnErr(log_pix,"PixHandler received unhandled msgType %d\n", pkt->genmtyp);
            }
        }
        msgRead(&pb->rq);
    }
    if(devs)free(devs);
}

void* producer(void* d)
{
    uint32_t            i, msg_need;
    int32_t             msgcnt;
    sock_data_msg_t     *packs[MMLEN_MMAX];
    sockdat_ntfy_t      ntfyCons;
    struct mmsghdr      msgs[MMLEN_MMAX];
    struct iovec        iovecs[MMLEN_MMAX];
    sock_data_msg_t     *pktPtr;
    app_node_t* artn = (app_node_t*)d;
    node_t*             n = artn->artnode;
    int mysock =        n->sockfd;
    mimas_state_t       mSt;
    int                 addr_len, ret;
    pid_t               pid;
    int                 batch= 2;
    sm_state_e          sm_state_cache;
    fl_t                nodes;
    fl_t                cn;
    fl_t                cn2;
    fl_t                con_node;
    uint32_t            nodes_avail=0;
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
    #define MMLEN batch
   // i=socket_set_blocking(n->sockfd, 0);
 #define handle_error_en(en, msg) \
               do { errno = en; perror(msg); exit(EXIT_FAILURE); } while (0)

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
        prnFinf(log_any,"Affinity on producer is set to 0x%0lx\n", cpuset.__bits[0]);
    }

    pid_t tid = gettid;
    ret = setpriority(PRIO_PROCESS, tid, (PROD_PRIO_NICE ) );
    prnFinf(log_any,"Producer set_nice to (%i) returns %d\n",PROD_PRIO_NICE, ret);

    time_t     now;
    struct tm  ts;
    char       tsbuf[80];
    memset(msgs, 0, sizeof(msgs));
    for(i=0;i<MMLEN_MMAX;i++)
    {
        iovecs[i].iov_base = NULL;// addr of data
        iovecs[i].iov_len = sizeof(any_prot_pack_t);
        msgs[i].msg_hdr.msg_iov    = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
    }
    memset(&ntfyCons,0,sizeof(ntfyCons));
    memset(packs,0,sizeof(packs));
    ntfyCons.mtyp = msg_typ_socket_ntfy;
    ntfyCons.rq_owner = sock_pb;
    nodes = NULL;
    cn = NULL;
    con_node = NULL;
    msgcnt = 0;
    setSockTimout(n->sockfd, 0);
    while(1)
    {
        if((n->sm.DataOk == 1) && (n->sm.prev_state!=working_e))
        {
            setSockTimout(n->sockfd,500);
        }
        msg_need = 0;
        MMLEN = (MAX(2,RCVED_UNIS(devList)))/2;
        while(MMLEN > MMLEN_MMAX)MMLEN>>1;
        prnInf(log_prod,"MMLEN = %d\n", MMLEN);
        if(nodes_avail < MMLEN)
        {
            do{
                msg_need = msgRezerveMultiNB(pkt_pb,&con_node, MMLEN - nodes_avail);
                msg_need += nodes_avail;
                if(msg_need == 0)
                {
                    usleep(2000000ul);
                    prnErr(log_prod,"No nodes available!!\n");

                }
            }while(msg_need == 0);
            if(nodes)
            {   cn = nodes;
                while(cn->nxt)
                {
                    cn = cn->nxt;
                }
                cn->nxt =con_node;
            }
            else
            {
                nodes = con_node;
            }
            nodes_avail = msg_need;
        }
        else
        {
            msg_need = nodes_avail;
        }
        prnLLdetail(nodes, "PROD", "ReadySlots");
        cn = nodes;
        for(i=0;i<msg_need;i++)
        {
            pktPtr = cn->item.pl.msg;
            cn->pb = pkt_pb;
            if(pktPtr == NULL) break;
            pktPtr->mtyp = msg_typ_socket_data;
            iovecs[i].iov_base = &pktPtr->pl.art;
            msgs[i].msg_hdr.msg_name = &pktPtr->sender;
            msgs[i].msg_hdr.msg_namelen = sizeof(struct sockaddr_in);
            cn = cn->nxt;
        }
        if(i<msg_need)
        {
            prnErr(log_prod,"Serious ERROR: msg_need was %d, only %d were valid\n", msg_need, i);
            msg_need = i;
        }

        timeout.tv_sec = 0l;
        timeout.tv_nsec = (2ul * MILIS);
        sm_state_cache = n->sm.state;
        msgcnt = recvmmsg(n->sockfd, msgs, msg_need, MSG_WAITALL, &timeout);
        time(&trm.ts2);
        prnInf(log_prod,"Received %d/%d Pkts fromSock\n",msgcnt,msg_need);

        if((artn->artnode->all % 1000000) == 0)
        {
            artn->artnode->all=0;
            artn->artnode->miss=0;
            artn->artnode->packetsCnt=0;
        }

        if(msgcnt>0)
        {
            trm.msg_cnt = msgcnt;
            clock_gettime(CLOCK_REALTIME, &trm.ts);
            post_msg(&ev_pb->rq, &trm, sizeof(trace_msg_t));
            art_net_pack_t* a;
            n->last_rx = trm.ts;
            artn->artnode->all+=msgcnt;
            artn->artnode->packetsCnt+=msgcnt;
            cn2 = NULL;
            if(msgcnt<msg_need)
            {
                cn = nodes;
                for(i=0;i<msgcnt-1;i++)
                {
                    cn = cn->nxt;
                }
                cn2 = cn->nxt;
                cn->nxt = NULL;
            }
            nodes_avail-=msgcnt;
            ntfyCons.datapt = nodes;
            prnLLdetail(nodes, "PROD", "SendingSlots");
            post_msg(&sock_pb->rq,&ntfyCons,sizeof(sockdat_ntfy_t));
            nodes = cn2;
        }
        else
        {
            if(msgcnt == 0)
            {
                prnDbg(log_prod,"msgs = 0\n");
                artn->artnode->all++;
                artn->artnode->miss++;
            }
            else
            {
                int errLocal;
                errLocal = show_socket_error_reason(n->sockfd);
                prnErr(log_prod,"================\nUnhandler Socket error:\n\terrno %d, sock ret %d, fd %d, errLocal %d\n================\n", errno, msgcnt, n->sockfd, errLocal);
            }
        }
    }
}

/* initialize mainPO and eventQ*/
int initMessaging(void)
{
    memset((void*)(ev_q), 0, sizeof(ev_q));
    memset((void*)(sock_rq), 0, sizeof(sock_rq));
    memset((void*)(pix_rq), 0, sizeof(pix_rq));
    memset((void*)(pwm_rq), 0, sizeof(pwm_rq));
    ev_pb = createPB_RQ(EV_Q_DEPTH,"EVNTQ",ev_q, sizeof(ev_q));
    pwm_pb = createPB_RQ(PWM_Q_DEPTH,"PWMQ",pwm_rq, sizeof(pwm_rq));
    pix_pb = createPB_RQ(RQ_DEPTH,"PixelQ", (uint8_t*)&pix_rq[0], sizeof(pix_rq));
    sock_pb = createPB_RQ(RQ_DEPTH,"ArtNetQ", (uint8_t*)&sock_rq[0], sizeof(sock_rq));
    pkt_pb = createPB_LL(RQ_DEPTH,"PktPool",sock_pkt_pool,sizeof(sock_data_msg_t));

}

void pmwTest();

int main(void)
{
    int res;
    setHandler(fault_handler);
    initLogLevels(DEF_LOG_LVL);
    //setLogLevel(log_pix,log_dbg);
    prnInf(log_any,"BCM lib version = %u\n", bcm2835_version());
  //  bcm2835_set_debug(1);
    int rc = bcm2835_init();
    int i;
    //testLists();
    if(rc!=1)
    {
        prnErr(log_any,"Error %d init bcm2835\n", rc);
        return(-1);
    }

    initMessaging();
    anetp->artPB = sock_pb;
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
    prnFinf(log_pwm,"Started PWM Test thread\n");
    rc = pthread_setname_np(pwm_tst_th, "PixLedPwmHan\0");
    if(rc)
    {
        perror("Producer1 thread rename failed");
    }
    socketStart(anetp->artnode, ARTNET_PORT);
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
    pthread_create(&time_one_sec,NULL,one_sec,(void*)ev_pb);
    rc = pthread_setname_np(time_one_sec, "PixLedStats\0");
    if(rc)
    {
        perror("Consumer1 thread rename failed");
    }

    pthread_t pix_handler_th;
    pthread_create(&pix_handler_th,NULL,pix_handler,(void*)anetp);
    rc = pthread_setname_np(pix_handler_th, "PixHandler\0");
    if(rc)
    {
        perror("Consumer1 thread rename failed");
    }

    prnFinf(log_any,"Starting Web Server...\n");
    while(1)
    {
        webServStart();
        prnErr(log_any,"Web Server exited, restarting...\n");
        sleep(1);
    }
    return(0);
}

void pmwTest()
{
    prnFinf(log_pwm,"PWM Handler Starting...\n");
    pwm_group_data_t pwm_d[MIMAS_PWM_GROUP_CNT];
    memset((void*)pwm_d, 0, sizeof(pwm_group_data_t)* MIMAS_PWM_GROUP_CNT);
    post_box_t* pb = pwm_pb;
    pwm_out_dev_t* pwms[MIMAS_PWM_GROUP_CNT];
    pwm_cmd_msg_t  pwm_msg;
    uint8_t enable_msk[MIMAS_PWM_GROUP_CNT];
    pwms[0] = getMimasPwmDevices();
    pwms[1]  = pwms[0]+1;
    int devIdx[MAX_VDEV_CNT];
    int devCnt, j, k, i, gDevCnt;
    pwm_pb = pb;
    peer_pack_t *pkt;
    int* devs = NULL;
    mimas_out_dev_t**   vDevs;
    mimas_out_dev_t*    cdev;
    pwm_vdev_t*         pwmDev;
    int                 hwId=-1;
    //uint16_t        mimas_start_bm=0;
    addressing_t    absAddr, relAddr;

    do
    {
        gDevCnt = findVDevsOfType(pwm_dev,NULL);
        if(gDevCnt<1) usleep(100000ul);
    }while(gDevCnt<1);
    devs = (int*)calloc(gDevCnt,sizeof(int));
    vDevs = (mimas_out_dev_t*)calloc(gDevCnt,sizeof(mimas_out_dev_t*));
    findVDevsOfType(pwm_dev,devs);
    for(i=0;i<gDevCnt;i++)
    {
        cdev = &devList.devs[devs[i]];
        vDevs[i] = cdev;
        cdev->dev_com.vdsm.expected_full_map = 1ul;
    }


    //peer_pack_t pack;

    pwm_d[0].gctrl =1;
    pwm_d[0].div = 39u;
    pwm_d[0].gper = 59999u;
    for(k=0;k<MIMAS_PWM_OUT_PER_GRP_CNT;k++)
    {
        pwm_d[0].ch_pers[k] = 4375;
        pwm_d[0].ch_ctrls[k] = 1;
        pwm_d[0].sleep_time[k]  =  PWM_SLEEP_TM;
        pwm_d[0].sleep_count[k] =  PWM_SLEEP_TM;
    }
    pwm_d[1].gctrl =1;
    pwm_d[1].div = 2u;
    pwm_d[1].gper = 0xFFFF;
    for(k=0;k<MIMAS_PWM_OUT_PER_GRP_CNT;k++)
    {
        pwm_d[1].ch_pers[k] = 16384u;
        pwm_d[1].ch_ctrls[k] = 1;
        pwm_d[1].sleep_time[k]  =  12* PWM_SLEEP_TM;
        pwm_d[1].sleep_count[k] =  PWM_SLEEP_TM;
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
    post_msg(&pb->rq,(void*)&pwm_msg,sizeof(pwm_cmd_msg_t));

    int rc;
    struct timespec ts[4];

    pkt = NULL;
    do
    {
        if(pkt)
        {
            msgRead(& (*pb).rq);
            pkt = NULL;
        }
        do
        {
            pkt = getMsg(& (*pb).rq);
            if(pkt==NULL)
            {
                memset(&pwm_msg,0,sizeof(pwm_cmd_msg_t));
                pwm_msg.cmd  = pwm_set_ch_ctrl_cmd;
                pwm_msg.mtyp = msg_typ_pwm_cmd;
                for(j=0;j<MIMAS_PWM_GROUP_CNT;j++){   pwm_msg.start_ch[j]=0xff;}
                for(j=0;j<MIMAS_PWM_GROUP_CNT;j++)
                {
                    for(k=0;k<MIMAS_PWM_OUT_PER_GRP_CNT;k++)
                    {
                        if(pwm_d[j].sleep_count[k]==0)
                        {
                            if(pwm_d[j].sleep_time[k]!=0)
                            {
                                if (pwm_d[j].ch_ctrls[k] !=0)
                                {
                                    if(pwm_msg.start_ch[j]==0xff)
                                    {
                                        pwm_msg.start_ch[j] = k;
                                        pwm_msg.ch_count[j] = 0;
                                    }
                                    pwm_msg.ch_count[j]++;
                                    pwm_d[j].sleep_count[k] = pwm_d[j].sleep_time[k];
                                    pwm_d[j].ch_ctrls[k] = 0;
                                }
                            }
                        }
                        else
                        {
                            pwm_d[j].sleep_count[k]--;
                        }
                    }
                    if(pwm_msg.start_ch[j]!=0xff) pwm_msg.grp_bm|=BIT8(j);
                }
                if((pwm_msg.start_ch[0]!=0xFF) || (pwm_msg.start_ch[1]!=0xFF))
                {
                    post_msg(&pb->rq,(void*)&pwm_msg,sizeof(pwm_cmd_msg_t));
                }
                else
                {
                    usleep(5);
                }
            }
        }while(pkt==NULL);
        clock_gettime(CLOCK_REALTIME, &ts[0]);
        peer_pack_t* sock_data_p = pkt;

        switch(pkt->genmtyp)
        {
            case msg_typ_socket_ntfy:
            {
                fl_t cn = pkt->dataNtfy.datapt;
                fl_t cnn;
                sock_data_msg_t* dt;
                art_net_pack_t* ap;
                post_box_t* rq_owner = pkt->dataNtfy.rq_owner;
                uint16_t tVal;
                uint8_t* datapt;
                uint8_t  chId;
                prnDbg(log_pwm,"msg_typ_socket_ntfy msg received\n");
                while(cn)
                {
                    dt = cn->item.pl.msg;
                    ap =  &dt->pl.art;
                    absAddr = ap->ArtDmxOut.a_net.raw_addr ;

                    devCnt = findVDevsAtAddr(absAddr, &devIdx);

                    pwm_ch_data_t* cd;
                    pwm_out_dev_t* hpwm = GET_PWMS_PTR;
                   // printf("found %d pwmDevices\n", devCnt);
                    for(k=0;k<devCnt;k++)
                    {
                        pwmDev = devList.devs[devIdx[k]].pwm_vdev;

                        if ((pwmDev!=NULL) && (pwmDev->com.dev_type == pwm_dev))
                        {
                            relAddr = absAddr - pwmDev->com.start_address;
                            if(devList.devs[devIdx[k]].dev_com.vdsm.curr_map & BIT64(relAddr))
                            {
                                prnErr(log_pwm,"Duplicate Universe %u on DevId %d\n", absAddr, devIdx[k]);
                            }
                            devList.devs[devIdx[k]].dev_com.vdsm.curr_map |= BIT64(relAddr);
                            // send to mimas
                            chId = pwmDev->hwStartIdx;
                            datapt = &ap->ArtDmxOut.dmx[pwmDev->com.start_offset];
                            for(j=0;j< pwmDev->ch_count; j++)
                            {

                                cd = &hpwm[chId/MIMAS_PWM_OUT_PER_GRP_CNT].ch_data[chId&7];
                                tVal = pwmMapValue(pwmDev , j, datapt);
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
                                    pwm_d[cd->phyGrp].ch_ctrls[cd->phyIdx] = 1;
                                    pwm_d[cd->phyGrp].sleep_count[cd->phyIdx] = pwm_d[cd->phyGrp].sleep_time[cd->phyIdx];
                                    //printf("Dev %d, ch %d inV %u outV %u\n", k, j, bswap_16(*(uint16_t*)datapt), tVal);
                                }
                                //setPwmVal(cdev->pwm_vdev , j, datapt);
                                if(cd->_16bits!=0)datapt++;
                                datapt++;
                                chId++;
                            }
                         }
                    }
                    putNode(cn->pb,cn, &cn);
                    //prnFinf(log_pwm, "PWM data ready\n");
                }
                for(k=0;k<gDevCnt;k++)
                {
                   // devList.devs[devIdx[k]].dev_com.vdsm.curr_map  = 0;
                   vDevs[k]->dev_com.vdsm.curr_map = 0llu;
                }
                clock_gettime(CLOCK_REALTIME, &ts[1]);
                for(k=0;k<MIMAS_PWM_GROUP_CNT;k++)
                {
                    if(pwm_d[k].needUpdate)
                    {
                        j = mimas_store_pwm_val( \
                                        BIT8(k), \
                                        pwm_d[k].startUpdIdx, \
                                        &pwm_d[k].ch_pers[pwm_d[k].startUpdIdx], \
                                        pwm_d[k].UpdChCount);

                        j = mimas_store_pwm_chCntrol(BIT8(k), \
                                        pwm_d[k].startUpdIdx, \
                                        &pwm_d[k].ch_ctrls[pwm_d[k].startUpdIdx], \
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
                break;
            }
/*
            case msg_typ_socket_data:
            {

                art_net_pack_t* dat = &pack.artn_msg.pl.art;

                //art_res = ArtNetDecode(dat);
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
                                        pwm_d[cd->phyGrp].ch_ctrls[cd->phyIdx] = 1;
                                        pwm_d[cd->phyGrp].sleep_count[cd->phyIdx] = pwm_d[cd->phyGrp].sleep_time[cd->phyIdx];
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
                                                pwm_d[k].startUpdIdx, \
                                                &pwm_d[k].ch_pers[pwm_d[k].startUpdIdx], \
                                                pwm_d[k].UpdChCount);

                                j = mimas_store_pwm_chCntrol(BIT8(k), \
                                                pwm_d[k].startUpdIdx, \
                                                &pwm_d[k].ch_ctrls[pwm_d[k].startUpdIdx], \
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

                        break;
                    }
                    default:
                    {

                    }
                }
                break;
            }*/
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
                        //mrc = mimas_store_pwm_chCntrol(PWM_GRP_A, msg->start_ch[0],&msg->data[0].ch_ctrls[msg->start_ch[0]],msg->ch_count[0]);
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

/*
        switch(ap->ArtDmxOut.head.OpCode.OpCode)
        {
            case 	OpPoll:
            {
                printf("================\nGot POLL from %s\n",inet_ntoa(p->artn_msg.sender.sin_addr));
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
                post_msg(&ev_pb->rq, trms[1],sizeof(trace_msg_t));
                // check if incoming universe falls in the range we handle
                // TODO: align sm->startNet, sm->endNet with devList and config
                devCnt = findVDevsAtAddr(raw_addr.addr, devIdx);
                vdevs_e devdstinct = unused_dev;
                sockdat_ntfy_t fwdm;
                for(i=0;i<devCnt;i++)
                {
                     cdev = &devList.devs[devIdx[i]];
                     vDevSetPeer(peer_id, devIdx[i]);

                    if (cdev==NULL) continue;
                    switch(GET_VDEV_TYPE(*cdev))
                    {
                        case ws_pix_dev:
                        {
                            if((devdstinct & ws_pix_dev) == 0)
                            {
                                p->genmtyp  = msg_typ_socket_ntfy;
                                post_msg(&pix_pb->rq, p, sizeof(sockdat_ntfy_t));
                                devdstinct|=ws_pix_dev;
                            }
                            break;
                            //mapColor(&ap->ArtDmxOut.dmx,&outs[oSel],sUni);
                            if( (outs[oSel].fullMap>0) && (outs[oSel].fillMap == outs[oSel].fullMap) )
                            {
                                rc = sendOutToMimas(oSel);
                                clock_gettime(CLOCK_REALTIME, &trms[2]->ts);
                                if(rc == 0)
                                {
                                    start_bm|=BIT32(oSel);
                                    post_msg(&ev_pb->rq, trms[2],sizeof(trace_msg_t));
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
                            if((devdstinct & pwm_dev) == 0)
                            {
                                fwdm.mtyp = msg_typ_socket_ntfy;
                                fwdm.rq_owner = pb;
                                fwdm.datapt = p;

                                devdstinct|=pwm_dev;

                                while(post_msg(&pwm_pb->rq, &fwdm, sizeof(sockdat_ntfy_t))){};
                            }
                            break;
                        }
                    }
                }
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
                            //dont break, fall through to wait sync state
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
                            //dont return nod yet to pile, it will be used in working state, and returned there.also dont break, fall through to working state
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

                        vdevs_e devdstinct = unused_dev;
                        sockdat_ntfy_t fwdm;
                        for(i=0;i<devCnt;i++)
                        {
                             cdev = &devList.devs[devIdx[i]];
                             vDevSetPeer(peer_id, devIdx[i]);

                            if (cdev==NULL) continue;
                            switch(GET_VDEV_TYPE(*cdev))
                            {
                                case ws_pix_dev:
                                {
                                    if((devdstinct & ws_pix_dev) == 0)
                                    {
                                        p->genmtyp  = msg_typ_socket_ntfy;
                                        post_msg(&pix_pb->rq, p, sizeof(sockdat_ntfy_t));
                                        devdstinct|=ws_pix_dev;
                                    }
                                    break;
                                    //mapColor(&ap->ArtDmxOut.dmx,&outs[oSel],sUni);
                                    if( (outs[oSel].fullMap>0) && (outs[oSel].fillMap == outs[oSel].fullMap) )
                                    {
                                        rc = sendOutToMimas(oSel);
                                        clock_gettime(CLOCK_REALTIME, &trms[2]->ts);
                                        if(rc == 0)
                                        {
                                            start_bm|=BIT32(oSel);
                                            post_msg(&ev_pb->rq, trms[2],sizeof(trace_msg_t));
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
                                    if((devdstinct & ws_pix_dev) == 0)
                                    {
                                        fwdm.mtyp = msg_typ_socket_ntfy;
                                        fwdm.rq_owner = pb;
                                        fwdm.datapt = p;

                                        devdstinct|=pwm_dev;
                                        while(post_msg(&pwm_pb->rq, &fwdm, sizeof(sockdat_ntfy_t))){};
                                    }
                                    break;
                                }
                            }
                        }

                        continue;
                        oSel = runi / UNI_PER_OUT;
                        sUni = runi % UNI_PER_OUT;


                        // if Sync packet is not present in stream and we got minimum uni, it must be a new frame, so reset bitmasks, and get fresh data
                        if(sm->hasSync == 0)
                        {
                            outs[oSel].fillMap|=BIT8(sUni);
                            if((sm->curr_map & BIT64(runi)))    // if this uni was already mapped in output, it will be overwritten by new packet, from previous 2 lines
                            {
                                rq_head_t info;
                                if(sm->expected_full_map & BIT64(runi))
                                {
                                    get_pb_info(&pb->rq, &info);
                                    printf("got a second runi %u. head = %u, tail = %u, cnt = %u, hS = %u, tS = %u\n", \
                                    runi, info.head, info.tail, info.count, info.headStep, info.tailStep);
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
                                                post_msg(&ev_pb->rq, trms[2],sizeof(trace_msg_t));
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
                                        post_msg(&pwm_pb->rq, p, sizeof(peer_pack_t));

                                        //printf("%05lu:%09lu  pwm_msg_posts = %d\n",ts.tv_sec, ts.tv_nsec, ++msg_posts);
                                        break;
                                    }
                                }

                            }
                        } //cdevs for loop end
                   //    else if(outputs_dev_map[oSel] == dev_pwm)
                   //     {
                   //         for(i=0;i<2;i++)
                   //         {
                   //             pwm_v[4 * i] = (ap->ArtDmxOut.dmx[i]);
                   //             pwm_v[4 * i]  *= 17;
                   //             pwm_v[4 * i] +=2400;
                               // printf("%s : %u ",i==0?"Pan":"Tilt", pwm_v[4*i]);

                   //        }

                    //   //printf("\n");
                    //    mimas_store_pwm_val(PWM_GRP_ALL, 0, pwm_v,8);
                    // }
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
            post_msg(&ev_pb->rq, trms[3],sizeof(trace_msg_t));
            post_msg(&ev_pb->rq, trms[4],sizeof(trace_msg_t));
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
*/


