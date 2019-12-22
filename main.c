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

#include <sys/ucontext.h>
#include <ucontext.h>
#include <execinfo.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/socket.h>
#include <string.h>
#define gettid syscall(SYS_gettid)

fl_head_t RQ;
fl_head_t UQ;
mimaspack_t         mpacks[GLOBAL_OUTPUTS_MAX];
out_def_t           outs[GLOBAL_OUTPUTS_MAX];
peer_pack_t         rq_data[RQ_DEPTH];
trace_msg_t         ev_q[EV_Q_DEPTH];
post_box_t*         ev_pb = NULL;


int sock_sec;
app_node_t prods[2];

app_node_t* anetp=&prods[0];
app_node_t* ascnp=&prods[1];
int altBind(int sockfd);

recFile_t rf;


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
    printf("Done setting up SIG_HAN\n");
}

void InitOuts(void)
{
    int i, j;
    uint8_t *pt;
    for(i=0;i<GLOBAL_OUTPUTS_MAX;i++)
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
        outs[i].mimaPack = &mpacks[i];
        outs[i].fillMap = 0;
        outs[i].fullMap = 0;
        pt = &outs[i].mimaPack->dmxp[0].dmx_data[0];
        for(j=0;j<UNI_PER_OUT;j++)
        {
            outs[i].wrPt[j] = pt;
            pt+=outs[i].uniLenLimit[j];
        }
    }
}
#include <sched.h>
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



int sendOutToMimas(int oSel)
{
    mimas_state_t mSt;
    mSt =mimas_get_state();
    if(mSt.sys_rdy==0)
    {
        //printf("WARNING(start): mimas not ready\n");
        do{
             //mimas_prn_state(&mSt);
             if(mSt.idle==1)
             {
                mimas_prn_state(&mSt);
                MIMAS_RESET
                printf("WARNING(start): mimas reset\n");
                bcm2835_delayMicroseconds(10000ull);
             }
             bcm2835_delayMicroseconds(2500ull);
             mSt =mimas_get_state();
         }while( (mSt.sys_rdy == 0) || (mSt.idle ==1));
         //printf("(start): mimas ready\n");
         //mimas_prn_state(&mSt);
    }
    //mimas_send_packet(oSel,(uint8_t*)outs[oSel].mimaPack,outs[oSel].dlen);
    return(mimas_store_packet(oSel,(uint8_t*)outs[oSel].mimaPack,outs[oSel].dlen));
}

void* consumer(void* d)
{
    uint32_t idx, i, attention;
    peer_pack_t *p;
    whole_art_packs_rec_t rec;
    app_node_t *artn = (app_node_t*)d;
    post_box_t* pb = artn->artPB;
    node_t *me = artn->artnode;
    sm_t* sm = &me->sm;
    int synced = 0;
    int  rc;
    uint8_t start_bm;
    int sync_missing;
    fl_t cn;
    art_net_pack_t      *ap;
    peer_pack_t         *pp;
    int mark, lowMark;
    lowMark = RQ_DEPTH;
    art_resp_e  art_res;

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
           printf("    CPU %d\n", i);
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
    //art_resp_e ArtNetDecode(const peer_pack_t* peerPack)
    while(1)
    {
    /*
        if(timeloop == 100)
        {
            double d = (double)timesum;
            double m = (double)timesum_mimas;
            d = d/(double)(CLOCKS_PER_SEC * 100l);
            m = m/(double)(CLOCKS_PER_SEC * 100l);
            //printf("pack2pack avg = %u\n", (uint32_t)timesum);
            printf("pack2pack avg = %.4f, mimas = %.5f\n", d, m);
            timesum = 0ul;
            timesum_mimas=0ul;
            timeloop =0u;
        }*/
        /*
        if(proc_cnt>=32000)
        {
            double d = (double)timesum_proc;
            d = d/(double)(CLOCKS_PER_SEC * (long)32);
            printf("proc avg = %.5f msec\n", d);
            proc_cnt=0;
            timesum_proc=0;
        }*/
    /*
        while (clock_gettime(CLOCK_REALTIME, &ts) == -1)
               perror("clock_gettime");
        //rc =sem_wait(&UQ.sem);
        ts.tv_sec+=1;
        cn = msgRxBLTimed(pb,&ts, &cause);
        //cn = msgRxBL(pb);
        if(cn == NULL)
        {
            printf("Error, consumer received NULL msg: cause = %d\n", cause);
            do{
            while (clock_gettime(CLOCK_REALTIME, &ts) == -1)
               perror("clock_gettime");
               ts.tv_sec+=10;
                cn = msgRxBLTimed(pb,&ts, &cause);
                printf("still waiting...cause = %d\n", cause);
                //cn = msgRxBL(pb);
            }while(cn==NULL);
            //continue;
        }
        */
        trms[0]->msg_cnt = popcnt++;
        do
        {
            p = getMsg(&pb->rq_head);
        }while(p == NULL);

        clock_gettime(CLOCK_REALTIME, &trms[0]->ts);
        post_msg(&ev_pb->rq_head, trms[0],sizeof(trace_msg_t));
        //p1 = clock();
       /* switch(cn->item.hdr.msgtype)
        {
            case art_rq_msg_e:
            {
                idx = cn->item.pl.itemId;
                break;
            }
            //case sys_msg_e:
            //case def_msg_e:
            default:
            msgRelease(pb, cn);
            printf("Error unknown msg type in consumer, dropping\n");
            continue;
        }*/
        /*
        rc = postGetPileCount(pb);
        if(rc <0)
        {
            perror("SemCnt err");
        }
        else
        {
            mark = rc;
            if(mark<lowMark)
            {
                lowMark = mark;
                printf("LoMark %d\n", lowMark);
            }
            else
            {
                attention = 0;
            }
            if(mark<(RQ_DEPTH / 4))
            {
                attention = 1;
            }
        }
        if(attention) printf("Low on Q pile, %u\n", lowMark);
*/
        //getFree(&UQ, cn, idx);
        //if(cn!=NULL)        {

            //pp = &rq_data[idx];
            ap =  &p->pl.art; //&pp->pl.art;
            art_res = ArtNetDecode(ap);
            //printf("Cons: got msg, artres = %u, uni = %u\n", art_res, ap->ArtDmxOut.a_net.SubUni.subuni_full);
            switch(ap->ArtDmxOut.head.OpCode.OpCode)
            {
                case 	OpPoll:
				{
                    make_artnet_resp(p);
					ap->ArtDmxOut.head.id[0] = '\0';
					msgRead(&pb->rq_head);
					//msgRelease(pb,cn); // return this node to unused pile
                    printf("Got POLL from %s\n",inet_ntoa(pp->sender.sin_addr));
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
                    ap->ArtDmxOut.head.id[0] = '\0';
					printf("ArtErr unknown OpCode %x\n", ap->ArtDmxOut.head.OpCode.OpCode);
					//msgRelease(pb,cn);
					msgRead(&pb->rq_head);
					continue; // get next Packet
					break;
				}
				case    OpSync:
				{
                    //putNode(&RQ, cn);
                    //msgRelease(pb,cn);
                    msgRead(&pb->rq_head);
                    switch(sm->state)
					{
                        case	out_of_sync_e:
                        case    wait_sync_e:
                        {
                            sm->hasSync = 1;
                            printf("Using Sync packets ...\n");
                            continue; // get next Packet
                            break;
                        }
                        case working_e:
                        {
                            synced = 1;
                            sync_missing = 0;
                            if(sm->hasSync == 0)
                            {
                                sm->hasSync = 1;
                                printf("detected sync packet while working ...\n");
                                continue; // get next Packet
                            }
                            break;
                        }
                        default:
                        {
                            printf("Sync received in state %d\n", sm->state);
                            synced = 1;
                            continue; // get next Packet
                        }
                    }
                    break;
				}
				case OpDmx:
				{
                    clock_gettime(CLOCK_REALTIME, &trms[1]->ts);
                    trms[1]->art_addr = ap->ArtDmxOut.a_net.SubUni.subuni_full;
                    post_msg(&ev_pb->rq_head, trms[1],sizeof(trace_msg_t));
                    // check if incoming universe falls in the range we handle
                    if((ap->ArtDmxOut.a_net.SubUni.subuni_full < (sm->startNet & 0xFF)) || \
                        (ap->ArtDmxOut.a_net.SubUni.subuni_full > (sm->endNet & 0xFF)))
                    {
                        //putNode(&RQ, cn);
                        //msgRelease(pb,cn);
                        msgRead(&pb->rq_head);
                        continue; // dont care about this universe, get next Packet
                        break;
                    }
                    // adjust min_uni if required
                    runi = ap->ArtDmxOut.a_net.SubUni.subuni_full - (sm->startNet &0xFF);
                    if(sm->min_uni > ap->ArtDmxOut.a_net.SubUni.subuni_full)
					{
						sm->min_uni = ap->ArtDmxOut.a_net.SubUni.subuni_full;
					}
					switch(sm->state)
					{
                        case	out_of_sync_e:
                        {
                            //if we got a universe for second time, and its the minimum we probably have them all, so we prepare for next state
                            if( (sm->expected_full_map & BIT64(runi)) && \
                                (sm->min_uni == ap->ArtDmxOut.a_net.SubUni.subuni_full) )
                            {
                                for(i=0;i<GLOBAL_OUTPUTS_MAX;i++)
                                {
                                    outs[i].dlen = 0;
                                    outs[i].fullMap = sm->expected_full_map & (BIT64(UNI_PER_OUT)  - 1u);
                                    sm->expected_full_map>>=UNI_PER_OUT;
                                }
                                start_bm = 0;
                                sm->expected_full_map = 0llu;
                                sm->state = wait_sync_e;
                                printf("Going wait_sync_e\n");
                                sm->DataOk = 1;
                                setSockTimout(me->sockfd,1000);
                                sm->active_unis = 0;
                                /*dont break, fall through to wait sync state */
                            }
                            else
                            {
                                //putNode(&RQ, cn); // return this node to unused pile
                                //msgRelease(pb,cn);
                                msgRead(&pb->rq_head);
                                sm->expected_full_map|=BIT64(runi);
                                continue;  // don't process  output, got get packets
                                break;
                            }
                        }
                        case	wait_sync_e:
						{
							// if we got minimum known uni for a second time move to working state
							if( (sm->min_uni == ap->ArtDmxOut.a_net.SubUni.subuni_full) && (sm->expected_full_map & BIT64(runi)) )
							{
								sm->state = working_e;
								printf("Going working_e, %u active unis\n", sm->active_unis);
								sm->curr_map = 0llu;
								/*dont return nod yet to pile, it will be used in working state, and returned there.
								 * also dont break, fall through to working state*/
							}
							else
							{
								//putNode(&RQ, cn); // return this node to unused pile
								//msgRelease(pb,cn);
								msgRead(&pb->rq_head);
								if((sm->expected_full_map & BIT64(runi) )== 0llu)sm->active_unis++;
								sm->expected_full_map|=BIT64(runi);
								continue;
								break;
							}
						}
						case working_e:
						{
                            oSel = runi / UNI_PER_OUT;
                            sUni = runi % UNI_PER_OUT;
                            // if Sync packet is not present in stream and we got minimum uni, it must be a new frame, so reset bitmasks, and get fresh data
                            if(sm->hasSync == 0)
                            {
                            /*
                                if(runi == 0)
                                {
                                    sm->curr_map = 0llu;
                                    outs[oSel].fillMap = 0;
                                    outs[oSel].dlen = 0;
                                }
*/
                               // printf("currMap %lu runi = %u oSel = %u\n",sm->curr_map, runi, oSel);
                                outs[oSel].fillMap|=BIT8(sUni);
                                mapColor(&ap->ArtDmxOut.dmx,&outs[oSel],sUni);
                                //putNode(&RQ,cn); // done with note, send to unused pile
                                //msgRelease(pb,cn);
                                msgRead(&pb->rq_head);
                                if((sm->curr_map & BIT64(runi)))
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
                                        for(i=0;i<GLOBAL_OUTPUTS_MAX;i++)
                                        {
                                            outs[i].dlen = 0;
                                            outs[i].fillMap = 0;
                                        }
                                        synced = 0;
                                        continue;
                                    }
                                }
                                else
                                {
                                    sm->curr_map|=BIT64(runi);
                                    outs[oSel].dlen+=outs[oSel].uniLenLimit[sUni];
                                }
                                //timesum_proc+=clock()-p1;
                                //proc_cnt++;
                            }
                            else
                            {
                                // if this is the first time we receive this universe in this frame do some extras
                                if((sm->curr_map & BIT64(runi)) == 0ul)
                                {
                                    outs[oSel].dlen+=outs[oSel].uniLenLimit[sUni];
                                    sm->curr_map|=BIT64(runi);
                                    outs[oSel].fillMap|=BIT8(sUni);
                                }
                                else
                                {
                                    if(++sync_missing > (5 * sm->active_unis))
                                    {
                                        //putNode(&RQ,cn); // done with note, send to unused pile
                                        //msgRelease(pb,cn);
                                        msgRead(&pb->rq_head);
                                        SmReset(me, eResetFrErr);
                                    }
                                }
                                mapColor(&ap->ArtDmxOut.dmx,&outs[oSel],sUni);
                                //putNode(&RQ,cn); // done with note, send to unused pile
                                //msgRelease(pb,cn);
                                msgRead(&pb->rq_head);

                                if(synced == 0 )continue; // just for sanity, synced can't normally be '1' at this point
                            }

                            if( (outs[oSel].fullMap>0) && (outs[oSel].fillMap == outs[oSel].fullMap) )
                            {
                                rc = sendOutToMimas(oSel);
                                if(rc == 0)
                                {
                                    start_bm|=BIT8(oSel);
                                    clock_gettime(CLOCK_REALTIME, &trms[2]->ts);
                                    post_msg(&ev_pb->rq_head, trms[2],sizeof(trace_msg_t));
                                }
                                else
                                {
                                    start_bm&=0xFF & (~BIT8(oSel));
                                    outs[oSel].fillMap = 0;
                                    printf("Error %d from mimas sending %d output\n",rc,oSel);
                                }
                            }
                            break; // go process out
						} // working state inside OpDMX closes
						break;
                    } // switch(sm->state) closes
                break;
                }   //OpCodeDMX closes
                default:
                {
                    msgRead(&pb->rq_head);
                }
            } // switch OpCode closes
// here starts output Handling

            if(sm->curr_map == sm->expected_full_map)
            {
                //bcm2835_delayMicroseconds(3000ull);
                mSt =mimas_get_state();
                if(mSt.sys_rdy==0)
                {
                    while( (mSt.sys_rdy == 0) || (mSt.idle ==1))
                    {
                        //usleep(50);
                         bcm2835_delayMicroseconds(10ull);
                         //mimas_prn_state(&mSt);
                         mSt =mimas_get_state();
                         if(mSt.idle==1) // means mimas is busy
                         {
                            mimas_prn_state(&mSt);
                            MIMAS_RESET
                            printf("WARNING(start): mimas reset\n");
                            bcm2835_delayMicroseconds(1000ull);
                         }
                         bcm2835_delayMicroseconds(250ull);
                         mSt =mimas_get_state();
                     }
                     //printf("(start): mimas ready\n");
                     //mimas_prn_state(&mSt);
                }
                //mimas_start_stream(start_bm,0);
                //__atomic_add_fetch(&me->frames, 1, __ATOMIC_RELAXED);

                clock_gettime(CLOCK_REALTIME, &trms[3]->ts);

                mimas_refresh_start_stream(start_bm,0);
                clock_gettime(CLOCK_REALTIME, &trms[4]->ts);
                post_msg(&ev_pb->rq_head, trms[3],sizeof(trace_msg_t));
                post_msg(&ev_pb->rq_head, trms[4],sizeof(trace_msg_t));

                //clock_gettime( CLOCK_PROCESS_CPUTIME_ID, &tmp);
                //timesum+=(tmp.tv_nsec - timers[idle_e].tv_nsec);
/*
                t2 = clock();
                timesum += (uint64_t)(t2-t1);
                timesum_mimas += (uint64_t)(t2-t3);
                t1 = t2;
                timeloop++;
                */
                start_bm = 0;
                sm->curr_map = 0llu;
                for(i=0;i<GLOBAL_OUTPUTS_MAX;i++)
                {
                    outs[i].dlen = 0;
                    outs[i].fillMap = 0;
                }
                synced = 0;
                //timers[idle_e].tv_sec = tmp.tv_sec;

            }

       // } // cn!=NULL closes

    }
}
#include <time.h>
//#define ALL_EVENTS
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
    uint32_t idx, i, k;
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
    ssize_t sock_ret;
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
    setSockTimout(n->sockfd, 1000);
    //cn =msgRezerveNB(pb);

    memset(hits,0,sizeof(hits));

    memset(packs,0,sizeof(packs));
    k = rezerveMsgMulti(&pb->rq_head,packs, MMLEN );
    for(i=0;i<k;i++)
    {
        p = packs[i];
        if(p==NULL) break;
        iovecs[i].iov_base = &p->pl.art;
    }
    k = i;
    while(1)
    {
        //addr_len = sizeof(struct sockaddr_in);
        //p = &rq_data[idx];

        //sock_ret = recvfrom(mysock,(void*)&p->pl, sizeof(any_prot_pack_t),0, (struct sockaddr*)&p->sender, &addr_len);
        timeout.tv_sec = 0;
        timeout.tv_nsec = 40ul * MILIS;
        sm_state_cache = n->sm.state;
        if(sm_state_cache!=working_e)batch=1;
        msgcnt = recvmmsg(n->sockfd, msgs, k, MSG_WAITALL/* MSG_WAITFORONE */, &timeout);
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
            msgWrittenMulti(&pb->rq_head,  msgcnt);
            do
            {
                k = rezerveMsgMulti(&pb->rq_head,packs, MMLEN );
                for(i=0;i<k;i++)
                {
                    p = packs[i];
                    if(p==NULL) break;
                    iovecs[i].iov_base = &p->pl.art;
                }
                k = i;
            }while(k<1);

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
                    if(n->sm.active_unis>(UNI_PER_OUT * GLOBAL_OUTPUTS_MAX))
                    {
                        batch = UNI_PER_OUT;
                    }
                    else
                    {
                        batch  = n->sm.active_unis;
                    }
                }
                else
                {
                    if(n->sm.active_unis>(UNI_PER_OUT * GLOBAL_OUTPUTS_MAX))
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
                setSockTimout(n->sockfd, 10000);
                n->sm.DataOk = 0;
                if(errno == EAGAIN)
                {
                    int  lockFree=0;
                    int tries=0;
                    do{
                        lockFree =  pthread_spin_trylock(&pb->rq_head.lock) ;
                    }while((lockFree !=0  )&&(tries++<100));

                    time(&now);
                    SmReset(n, eResetSockTimeout);
                    ts = *localtime(&now);
                    strftime(tsbuf, sizeof(tsbuf), "%T", &ts);
                    printf("EAGAIN %s : lock %d\n",tsbuf,lockFree);
                    errno=0;
                    if(lockFree==0)pthread_spin_unlock(&pb->rq_head.lock) ;
                    //usleep(1);
                    //pthread_yield();
                    msgcnt = 0;
                    i = close(n->sockfd);
                    if(i != 0) perror("close Socket:");
                    else
                    do{
                    i = socketStart(n,ARTNET_PORT);
                    if(i)sleep(1);
                    }while(i!=0);
                    continue;
                }
                else
                {
                    int errLocal;
                    errLocal = show_socket_error_reason(n->sockfd);
                    printf("errno %d, sock ret %d, fd %d, idx %d, errLocal %d\n", errno, sock_ret, n->sockfd, idx, errLocal);
                    if(errno == 11)
                    {
                        SmReset(n, eResetSockTimeout);
                    }
                    else
                    {

                        SmReset(n, eResetUnknown);
                    }
                    errno = 0;
                    setSockTimout(n->sockfd,0);
                    mimas_all_black(&outs);
                    bcm2835_delayMicroseconds( 6000ull );
                    mSt = mimas_get_state();
                    mimas_prn_state(&mSt);
                    i = 0;
                    while(((mSt.clk_rdy==0)||(mSt.sys_rdy==0))&&(mSt.idle == 0))
                    {
                        bcm2835_delayMicroseconds( 1000ull );
                        mSt = mimas_get_state();
                        if(++i>30)
                        {
                            printf("mimas maybe Stuck!\n");
                            mimas_prn_state(&mSt);
                            break;
                        }
                    }
                    if((mSt.clk_rdy==0)||(mSt.sys_rdy==0))
                    {
                        printf("mimas needs reset...\n");
                        mimas_reset();
                        bcm2835_delayMicroseconds(1000ull);
                        mimas_prn_state(NULL);
                    }
                    mimas_prn_state(NULL);
                }
            }
        }
    }
}
/*
#include "gen_lists.h"
void testLists(void)
{
    volatile int a,b,c;
    int rc;
    a = 3;
    b = 5;
    c = 122;
    ln_t h = addItem(NULL,&a);
    addItem(h, &b);
    addItem(h, &c);
    rc = remItem(h, &b);
}
*/
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
    in_addr_t sec_ip;
    struct ifaddrs ipres;

    memset((void*)(ev_q), 0, sizeof(ev_q));
    memset((void*)(rq_data), 0, sizeof(rq_data));
    ev_pb = createPB(EV_Q_DEPTH,"EVQ",ev_q);
    post_box_t* mainPB =createPB(EV_Q_DEPTH,"MAINPB", (void*)&rq_data[0]);
    if(mainPB)
    {
        setDefPB(mainPB);
    }
    msg_t msg;
    msg_t* resp;
    msg.hdr.msgid = def_msg_e;
    msg.hdr.msgLen = 4;
    msg.pl.itemId = 100;
    /*fl_t mh = msgPostBL(NULL,&msg);
    resp = msgRxBL(NULL);
    msgRelease(NULL,mh);
    */
/*
    memset((void*)&rf, 0, sizeof(recFile_t));
    rf.maxLen = 10ul * MBYTES;
    strcat(rf.fname, "artRec1");
    i = createRecFile(&rf);
    */

    anetp->artPB = createPB(RQ_DEPTH,"ArtNetPB", (void*)&rq_data[0]);
    ascnp->artPB = createPB(RQ_DEPTH,"sACNPB", (void*)&rq_data[0]);
    anetp->artnode = createNode(protoArtNet);
    ascnp->artnode = createNode(protosACN);
    InitOuts();


    initSPI();
    do{
       res = initMimas();
      if(res!=0) sleep(3);
    }while(res!=0);
    mimas_all_black(&outs);
    //getInterfaces();
    //getInterfaces();
    socketStart(anetp->artnode, ARTNET_PORT);
    socketStart(ascnp->artnode, ACN_SDT_MULTICAST_PORT);
    NodeInit(anetp, (GLOBAL_OUTPUTS_MAX * UNI_PER_OUT), 0x11);
    art_set_node(anetp->artnode);
    anetp->artnode->intLimit = 50;
    SmReset(anetp->artnode,eResetInit);
/*
    sock_sec = socket_init(NULL);
    if(sock_sec>-1)
    {
        add_IP_Address("2.250.250.1");
        sec_ip =  inet_addr("2.250.250.1");
        rc = sock_bind(sock_sec, "wlan0",&sec_ip, ARTNET_PORT);
    }
*/
    pthread_create(&anetp->artnode->con_tid, NULL, consumer,(void*)anetp);
    rc = pthread_setname_np(anetp->artnode->con_tid, "ArtNetCons\0");
    if(rc)
    {
        perror("Consumer1 thread rename failed");
    }

    pthread_create(&anetp->artnode->prod_tid, NULL, producer,(void*)anetp);
    rc = pthread_setname_np(anetp->artnode->prod_tid, "ArtNetProd\0");
    if(rc)
    {
        perror("Producer1 thread rename failed");
    }

    size_t addr_len;
    int sock_ret;
    pthread_t time_one_sec;
    pthread_create(&time_one_sec,NULL,one_sec,(void*)anetp);
    printf("Starting Web Server...\n");
    while(1)
    {
    /*
        addr_len = sizeof(struct sockaddr_in);
        sock_ret = recvfrom(sock131,(void*)&e131p, sizeof(e131_pack_t),0, &acnpeer.peer, &addr_len);
        if(sock_ret>0)
        {
            if(0==parse_e131(&e131p))
            {
                outs[0].dlen = 450;
                memcpy(outs[0].wrPt[0], e131p.dat.dmp.propVals, 450);
                if((rc = mimas_store_packet(0,(uint8_t*)outs[0].mimaPack,outs[0].dlen))!=0)
                {
                    printf("mimas_store_packet@%u ERROR %d\n", 0, rc);
                }
                else{
                    mimas_refresh_start_stream(1,0);
                }
            }
        }
    */

        webServStart();

        printf("Web Server exited, restarting...\n");
        sleep(1);
    }
    return(0);
}
