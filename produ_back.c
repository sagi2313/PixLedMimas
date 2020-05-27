
#define MMLEN_MMAX   40
#define MILIS   (1000000ul)

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
    post_box_t*         pb = artn->artPB;
    int mysock =        n->sockfd;
    mimas_state_t       mSt;
    int                 addr_len, ret;
    pid_t               pid;
    int                 batch=devList.mapped_addresses;
    sm_state_e          sm_state_cache;
    fl_t                nodes;
    fl_t                cnode;
    fl_t                cn;
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

    #define MMLEN batch

    memset(&ntfyCons,0,sizeof(ntfyCons));
    memset(packs,0,sizeof(packs));
    ntfyCons.mtyp = msg_typ_socket_ntfy;
    ntfyCons.rq_owner = sock_pb;
    nodes = NULL;
    cnode = NULL;
    con_node = NULL;
    msgcnt = 0;
    setSockTimout(n->sockfd, 0);
    while(1)
    {
        if((n->sm.DataOk == 1) && (n->sm.prev_state!=working_e))
        {
            setSockTimout(n->sockfd,500);
        }
        timeout.tv_sec = 0l;
        timeout.tv_nsec = (15ul * MILIS);
        sm_state_cache = n->sm.state;


        i=0;
        if(nodes!=NULL)
        {
            cnode = nodes;
            while(++i < msgcnt)
            {
                cnode = cnode->nxt;
            }
            con_node = cnode->nxt;
            if(cnode->nxt!=NULL)
            {
                cnode->nxt = NULL;
            }
            prnLLdetail(nodes, "sent");
            ntfyCons.datapt = nodes;
            post_msg(&sock_pb->rq,&ntfyCons,sizeof(sockdat_ntfy_t));
            nodes_avail = msg_need - i;
        }
        if(con_node)
        {
            cnode = con_node;
            while(cnode->nxt)cnode = cnode->nxt;
        }

        msg_need = msgRezerveMultiNB(pkt_pb,&nodes, MMLEN - nodes_avail);
        msg_need += nodes_avail;
        if(con_node != NULL)
        {
            cnode->nxt = nodes;
            nodes = con_node;
        }
        cnode = nodes;
        for(i=0;i<msg_need;i++)
        {
            pktPtr = cnode->item.pl.msg;
            cnode->pb = pkt_pb;
            if(pktPtr == NULL) break;
            pktPtr->mtyp = msg_typ_socket_data;
            iovecs[i].iov_base = &pktPtr->pl.art;
            msgs[i].msg_hdr.msg_name = &pktPtr->sender;
            msgs[i].msg_hdr.msg_namelen = sizeof(struct sockaddr_in);
            cnode = cnode->nxt;
        }
        prnLLdetail(nodes, "nextReady");
        msg_need = i;
        nodes_avail = msg_need;
        msg_need = MIN(nodes_avail, batch);

        msgcnt = recvmmsg(n->sockfd, msgs, msg_need, MSG_WAITALL, &timeout);
        time(&trm.ts2);
        printf("Received %d Pkts fromSock\n",msgcnt);

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

            /*send messages */

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
            continue;
            batch = 1;
            msg_need = 1;
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
                    p = rezerveMsg(&pb->rq);
                    usleep(5);
                }while(p == NULL);
                p->genmtyp = msg_typ_sys_event;
                p->sys_ev.ev_type = sys_ev_socket_timeout;
                p->sys_ev.data1 = n->sockfd;
                msgWritten(&pb->rq);
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
                        lockFree =  pthread_spin_trylock(&pb->rq.lock) ;
                    }while((lockFree !=0  )&&(tries++<100));
                    SmReset(n, eResetSockTimeout);
                    time(&now);
                    ts = *localtime(&now);
                    strftime(tsbuf, sizeof(tsbuf), "%T", &ts);
                    printf("================\nData Timeout(EAGAIN,lock %d) %s\n================\n",lockFree,tsbuf);
                    if(lockFree==0)pthread_spin_unlock(&pb->rq.lock) ;
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
