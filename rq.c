#include "rq.h"
#include <errno.h>
#include "utils.h"
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)
static post_box_t* defPB = NULL;


post_box_t* createPB_RQ(uint32_t count, const char* name, uint8_t* rqs, uint32_t q_size)
{
    post_box_t* pb = (post_box_t*)calloc(1, sizeof(post_box_t));
    if(pb == NULL)return(NULL);
    if(name!=NULL)strncat(pb->pbName, name, 31);
    pb->pbTyp = pb_rq_e;
    memset((void*)(&pb->rq), 0, sizeof(rq_head_t));
    pb->rq.low_mark = count;
    pb->rq.count = 0;
    pb->rq.genQ = rqs;
    pb->rq.head = (pb->rq.head - 1)& (count-1u);
    pb->rq.Q_MSK = (count-1u);
    pb->rq.slot_sz = q_size / count;
    pthread_spin_init( &pb->rq.lock,  PTHREAD_PROCESS_PRIVATE );

    return(pb);
}


post_box_t* createPB_LL(uint32_t count, const char* name, uint8_t* pool, uint32_t item_size)
{
    int i;
    post_box_t* pb = (post_box_t*)calloc(1, sizeof(post_box_t));
    if(pb == NULL)return(NULL);
    if(name!=NULL)strncat(pb->pbName, name, 31);
    pb->pbTyp = pb_ll_e;
    memset((void*)(&pb->ll), 0, sizeof(ll_pb_t));
    pb->ll.box.count = 0;
    pb->ll.box.maxCount = count;
    pb->ll.box.low_mark = count;
    pb->ll.box.head = NULL;
    pb->ll.box.tail = NULL;


    pb->ll.pile.head = (fl_t)calloc(count, sizeof(freeList_t));
    pb->ll.pile.count = count;
    pb->ll.pile.maxCount = count;
    pb->ll.pile.low_mark = count;
    fl_t n = pb->ll.pile.head;
    for(i=0;i<count-1;i++)
    {
        n->item.pl.itemId = i;
        n->item.pl.msg = &pool[i * item_size];
        n->nxt = &n[1];
        n = n->nxt;
    }
    n->item.pl.itemId = i;
    n->item.pl.msg = &pool[i * item_size];
    n->nxt = NULL;
    pb->ll.pile.tail = n;
    pthread_spin_init( &pb->ll.box.lock,  PTHREAD_PROCESS_PRIVATE );
    pthread_spin_init( &pb->ll.pile.lock,  PTHREAD_PROCESS_PRIVATE );
    return(pb);
}
// RQ API
int rezerveMsgs(int count, rq_head_t* hd)
{
    uint32_t t = hd->tail;
    if(t>hd->head)
    {
        return(MIN( t-hd->head,count ));
    }
    else
    {
        uint32_t r = (hd->Q_MSK +1u) - hd->head;
        if(r < count )
        {
            return( MIN( (r + t), count) );
        }
        return( MIN(r,count));
    }
}
peer_pack_t* rezerveMsg( rq_head_t* hd)
{
    register uint32_t count = __atomic_load_n(&hd->count,  __ATOMIC_RELAXED);
    if(count > hd->Q_MSK)return(NULL);
    return (  &hd->genQ[ hd->slot_sz * ((hd->head +1) &  hd->Q_MSK) ] );
}

/*  get a pointer to next unread msg */
peer_pack_t* getMsg(rq_head_t* hd)
{
    register uint32_t count = __atomic_load_n(&hd->count,  __ATOMIC_RELAXED);
    if(count <1)return(NULL);

    return (&hd->genQ[ ( hd->slot_sz * hd->tail )]);
}

/* mark a msg are "read" and move tail index , decrease count */
void msgRead(rq_head_t* hd)
{
    pthread_spin_lock(&hd->lock);
    register uint32_t count = __atomic_load_n(&hd->count,  __ATOMIC_RELAXED);
    if(count>0)
    {
        hd->count = __atomic_sub_fetch(&hd->count, 1, __ATOMIC_RELAXED);
    }
    else
    {
        prnErr(log_ll,"%s WTF??\n", ((char*)hd) +  MAX(sizeof(ll_pb_t), sizeof(rq_head_t) ) );
    }
    pthread_spin_unlock(&hd->lock);
    hd->tail = ((++hd->tail) & hd->Q_MSK);
    hd->tailStep++;
}

/* mark a msg are "read" and move tail index , decrease count , copy msg into buffer "pack"*/
int getCopyMsg(rq_head_t* hd, peer_pack_t* pack)
{
    register uint32_t count = __atomic_load_n(&hd->count,  __ATOMIC_RELAXED);
    if(count <1)return(-1);
    pthread_spin_lock(&hd->lock);
    hd->count = __atomic_sub_fetch(&hd->count, 1, __ATOMIC_RELAXED);
    *pack = *(peer_pack_t*)&hd->genQ[ ( hd->slot_sz * hd->tail )];
    hd->tail = ((++hd->tail) & hd->Q_MSK);
    hd->tailStep++;
    pthread_spin_unlock(&hd->lock);


    return (0);
}
/* mark 1 slot as written, meaning slot processing is done, msg will be available to receiver */
void msgWritten(rq_head_t* hd)
{
    pthread_spin_lock(&hd->lock);
    hd->count = __atomic_add_fetch(&hd->count, 1, __ATOMIC_RELAXED);
    pthread_spin_unlock(&hd->lock);
    hd->head = ((++hd->head) & hd->Q_MSK);
    hd->headStep++;
}

/* mark cnt slots as written, meaning slot processing is done, msgs will be available to receiver */
void msgWrittenMulti(rq_head_t* hd, int cnt)
{
    pthread_spin_lock(&hd->lock);
    hd->count = __atomic_add_fetch(&hd->count, cnt, __ATOMIC_RELAXED);
    pthread_spin_unlock(&hd->lock);
    hd->head = ((hd->head + cnt) & hd->Q_MSK);
    hd->headStep+=cnt;
}

/* rezerveMsgMulti:
    check how many slots up to cnt can are free to reserve in PO
    without sending messages, and return array of pointers to **p
*/
int rezerveMsgMulti(rq_head_t* hd,peer_pack_t** p, int cnt)
{
    int r=0;
    uint_fast32_t count = __atomic_load_n(&hd->count,  __ATOMIC_RELAXED);
    if((count + cnt) > hd->Q_MSK)cnt = hd->Q_MSK - count +1;
    cnt ++;
    for(r=1;r<cnt;r++)
    {
        p[r-1] = &hd->genQ[ (hd->slot_sz * ((hd->head + r) &  hd->Q_MSK)) ];
    }
    return(cnt-1);
}

int get_pb_info(rq_head_t* from, rq_head_t* to)
{
    pthread_spin_lock(&from->lock);
    *to = *from;
    pthread_spin_unlock(&from->lock);
}
void prn_pb_info(post_box_t* pb)
{
    if(pb->pbTyp == pb_rq_e)
    {
        prnInf(log_ll,"RQ name:%s head: %u, tail:%u, count: %u\n", \
        pb->pbName, pb->rq.head, pb->rq.tail, pb->rq.count);
    }
}

int post_msg(rq_head_t* hd, void* data, int len)
{
    do
    {
        pthread_spin_lock(&hd->lock);
        if(hd->count > hd->Q_MSK)
        {
            pthread_spin_unlock(&hd->lock);
            return(-1);
        }
    }while(0);
    hd->head++;
    hd->head &= hd->Q_MSK;
    memcpy((void*)(&hd->genQ[ (hd->slot_sz * hd->head)]), data, len);
    hd->count++;

    pthread_spin_unlock(&hd->lock);
    return (0);
}

int get_taces(rq_head_t* hd, int* indexes, int max_count )
{

    int *pidx = indexes;
    int i =0;
    pthread_spin_lock(&hd->lock);
    int cnt = (max_count < hd->count )?max_count:hd->count ;
    max_count = cnt;
    while(cnt)
    {
        pidx[i] = ((hd->tail + i) & hd->Q_MSK);
        cnt--;
        i++;
    }
    pthread_spin_unlock(&hd->lock);
    return(max_count);
}
void free_traces(rq_head_t* hd, int count)
{
    pthread_spin_lock(&hd->lock);
    hd->count -=count;
    hd->tail += count;
    hd->tail &= hd->Q_MSK;
    pthread_spin_unlock(&hd->lock);
}
//LL API



/* Removes the head item from the LinkedList, and returns it to caller.
 * if it is the last item in list then head and tail are set to NULL.
 */
fl_t getNode(fl_head_t* hd)
{
/*
    while( pthread_mutex_trylock(&hd->mux)!=0)
    {
      __atomic_add_fetch(&hd->collisions, 1, __ATOMIC_RELAXED);
    }
    */
    pthread_spin_lock(&hd->lock);
	//pthread_mutex_lock(&hd->mux);
	if(hd->head==NULL)
	{
		if(unlikely( hd->count!=0 ) ) /*sanity + debugging check */
		{
			prnErr(log_ll,"File %s, Line %d, Head Null, count %u in %p\n", __FILE__, __LINE__, hd->count, hd);
			hd->count = 0;
		}
		//pthread_mutex_unlock(&hd->mux);
		pthread_spin_unlock(&hd->lock);
		return(NULL);
	}
	fl_t nd=hd->head;
	if(unlikely( hd->count==0 ) )
	{
		while(nd)
		{
			hd->count++;
			nd = nd->nxt;
		}
		prnErr(log_ll,"File %s, Line %d, Head NOT Null, count was 0, but actual %u in %p\n", __FILE__, __LINE__, hd->count, hd);
	}
	nd=hd->head;

	if((--hd->count) == 0)
	{
		hd->tail = NULL;
		hd->head = NULL;
	}
	else
	{
		hd->head = nd->nxt;
	}
	//pthread_mutex_unlock(&hd->mux);
	pthread_spin_unlock(&hd->lock);
	return(nd);
}



int getNodes(fl_head_t* hd, fl_t* nodes)
{
    int count =0;
    pthread_spin_lock(&hd->lock);
	if(hd->head==NULL)
	{
		if(unlikely( hd->count!=0 ) ) /*sanity + debugging check */
		{
			prnErr(log_ll,"File %s, Line %d Head Null, count %u in %p\n",__FILE__, __LINE__,hd->count, hd);
			hd->count = 0;
		}
		pthread_spin_unlock(&hd->lock);
		return(NULL);
	}
	fl_t nd=hd->head;

	while(nd)
    {
        //hd->count++;
        nd = nd->nxt;
        count++;
    }
    if(unlikely(count!=hd->count))
    {
        prnErr(log_ll,"File %s, Line %d Head NOT Null, count was %d, but actual %u in %p\n",__FILE__, __LINE__,count, hd->count, hd);
    }
	nd=hd->head;
    hd->count = 0;
    hd->tail = NULL;
    hd->head = NULL;
	*nodes = nd;
	pthread_spin_unlock(&hd->lock);
	return(count);
}


/* Appends one node item at 'nd' to the tail of the "pile" Linked List in 'pb'
    if '*nnd' is not NULL, it is set to point to nd->nxt
    before nd->nxt is set to NULL.
    The node count in pile is returned*/
uint32_t putNode(post_box_t* pb, fl_t nd, fl_t *nnd)
{
	//__sync_fetch_and_add(&hd->count, 1ul);
	//
	uint32_t cnt = 0;
	if( unlikely(pb == NULL)) return 0;
	fl_head_t* hd = &pb->ll.pile;
	if( unlikely(hd == NULL)) return 0;
	pthread_spin_lock(&hd->lock);
	if(likely(nd!=NULL))
	{
		if(hd->tail!=NULL)
		{
			hd->tail->nxt = nd;
		}
		hd->tail = nd;
		if(nnd!=NULL)*nnd = nd->nxt;
		hd->tail->nxt = NULL;
		if(hd->head==NULL)hd->head = hd->tail;
		hd->count++;
	}

	cnt = hd->count;
	prnInf(log_ll,"PBput:%s Item %u, Rem %d\n",pb->pbName,  nd->item.pl.itemId, hd->count);
	pthread_spin_unlock(&hd->lock);
	return(cnt);
	//(void)__sync_val_compare_and_swap(&hd->head,(uint32_t)NULL,nd);
}


/* Appends a node item to the tail of the "hd" Linked List. */
uint32_t putNodes(post_box_t* pb, fl_t nd)
{
	//__sync_fetch_and_add(&hd->count, 1ul);
	//
	uint32_t cnt = 0;
	if( unlikely(pb == NULL)) return 0;
	fl_head_t* hd = &pb->ll.pile;
	if( unlikely(hd == NULL)) return 0;
	pthread_spin_lock(&hd->lock);
	if(likely(nd!=NULL))
	{
		if(hd->tail!=NULL)	hd->tail->nxt = nd;
		if(hd->head==NULL)  hd->head = nd;
		cnt++;
		while(nd->nxt)
		{
            cnt++;
            nd = nd->nxt;
		}
		hd->tail = nd;
	}
	hd->count+=cnt;
	prnDbg(log_ll,"PBputMany:%s Rem %d\n",pb->pbName, hd->count);
	pthread_spin_unlock(&hd->lock);
	return(cnt);
	//(void)__sync_val_compare_and_swap(&hd->head,(uint32_t)NULL,nd);
}

inline uint32_t getNodeCount(fl_head_t* hd)
{
/*
	uint32_t cnt;

	pthread_spin_lock(&hd->lock);
	cnt =  hd->count;
	pthread_spin_unlock(&hd->lock);
	return(cnt);*/
	return( (uint32_t)(__atomic_load_n(&hd->count,__ATOMIC_ACQ_REL)));
}


fl_t msgRxNB(post_box_t* pb)
{
    if(pb==NULL)return(NULL);
    if(0==getNodeCount(&pb->ll.box))return(NULL);
    return(getNode(&pb->ll.box));
}

fl_t msgRxBL(post_box_t* pb)
{
    if(pb==NULL)return(NULL);
    while(1)
    {
        if(0==getNodeCount(&pb->ll.box)) pthread_yield();
        else  return(getNode(&pb->ll.box));
    }
}
/*
fl_t msgRxBLTimed(post_box_t* pb, struct timespec *ts, int* err)
{
    if(pb==NULL)pb = defPB;
    *err = sem_timedwait(&pb->box.sem, ts);
    if(*err!=0)
    {
        return(NULL);
    }
    return(getNode(&pb->box));
}
*/
fl_t msgPostNB(post_box_t* pb, const msg_t* msg, uint32_t sz)
{
    if(pb==NULL)return(NULL);
    fl_t n = getNode(&pb->ll.pile);
    if(n == NULL)return(NULL);
    if(sz<1)sz = sizeof(msg_t);
    memcpy(&n->item, msg, sz);
    putNode(&pb->ll.box,  n, NULL);
    return(n);
}

fl_t msgPostBL(post_box_t* pb, const msg_t* msg, uint32_t sz)
{
    if(pb==NULL)return(NULL);
    fl_t n = NULL;
    while(n == NULL)
    {
        n = getNode(&pb->ll.pile);
        if(n == NULL) pthread_yield();
    }
    if(sz<1)sz = sizeof(msg_t);
    memcpy(&n->item, msg, sz);
    putNode(&pb->ll.box,  n, NULL);
    return(n);
}

void msgRelease(const post_box_t* pb, const fl_t msg)
{
    if(pb==NULL)
    {
        prnErr(log_ll,"NULL pb, failed to Release  Msg\n");
        return;
    }
    putNode(&pb->ll.pile,  msg, NULL);
}

void msgRezervedPostNB(post_box_t* pb, const fl_t msg)
{
    if(pb==NULL)
    {
        prnErr(log_ll,"NULL pb, failed to Post Msg\n");
        return;
    }
    putNode(&pb->ll.box,  msg, NULL);
}

fl_t msgRezerveNB(post_box_t* pb)
{
    if(pb==NULL)
    {
        prnErr(log_ll,"NULL pb, failed to Rezerve  Msg\n");
        return;
    }
    return(getNode(&pb->ll.pile));
}


int msgRezerveMultiNB(post_box_t* pb, fl_t* nodes, uint32_t count)
{
    uint32_t cnt =0;

    fl_head_t* hd = &pb->ll.pile;
    pthread_spin_lock(&hd->lock);
	if(hd->head==NULL)
	{
		if(unlikely( hd->count!=0 ) ) /*sanity + debugging check */
		{
			prnErr(log_ll,"File %s, Line:%d Head (%s) Null, count %u in %p\n",__FILE__,__LINE__, pb->pbName,hd->count, hd);
			hd->count = 0;
		}
		pthread_spin_unlock(&hd->lock);
		*nodes = NULL;
		return(0);
	}
	fl_t nd =  hd->head;
    *nodes = hd->head;
    if(count>=hd->count)
    {
        prnErr(log_ll,"LowBuffer(%s:%d/%d)\n", pb->pbName,hd->count, hd->maxCount);
        count = hd->count;
    }
	while(nd)
    {
        if((++cnt == count) || (nd == NULL))break;
        nd = nd->nxt;
    }
    if(nd == NULL)
    {
        prnErr(log_ll,"NULL node(%s)...\n", pb->pbName);
    }
    hd->count -= cnt;
    if(hd->count> hd->maxCount)
    {
        prnErr(log_ll,"LL %s underflow!\n", pb->pbName);
    }
    if(hd->count == 0)
    {
        hd->head = NULL;
        hd->tail = NULL;
    }
    else
    {
        hd->head = nd->nxt;
        nd->nxt = NULL;
    }
    if(hd->count < hd->low_mark)
    {
        hd->low_mark = hd->count;
        prnFinf(log_ll,"LL %s low mark dropped: %d!\n", pb->pbName, hd->low_mark);
    }
    //prnDbg(log_ll,"\t\t\t\t\t PB:%s Req %d, Rez %d Rem %d\n",pb->pbName, count, cnt, hd->count);
	pthread_spin_unlock(&hd->lock);
	return(cnt);
}

void prnLLdetail(fl_t br, char* WHO, char* name)
{
    if(getLogLevel(log_ll)>log_info) return;
    prnLock;
    printf("%s: Branch(%s) info : \n\t", (WHO!=NULL?WHO:"?"),(name!=NULL?name:"?"));
    int cnt=0;
    peer_pack_t *pkt;
    sock_data_msg_t* dt;
    while(br)
    {
        dt = br->item.pl.msg;
        printf("%3u(%u), " , br->item.pl.itemId, dt->pl.art.ArtDmxOut.a_net.raw_addr);
        br = br->nxt;
        cnt++;
        //if((++cnt & 3) == 3)printf("\n\t");
    }
    printf(" Sum %d \n", cnt);
    prnUnlock;
}

void addToBranch(node_branch_t* br, fl_t nd)
{
    if(br->hd == NULL)
    {
        br->hd = nd;
        br->lst = nd;
    }
    else
    {
        br->lst->nxt = nd;
        br->lst = br->lst->nxt;
    }
}

/*
fl_t msgRezerveTimed(post_box_t* pb, long nsec, int* cause)
{
    struct timespec ts;
    if(pb==NULL)pb = defPB;
    while (clock_gettime(CLOCK_REALTIME, &ts) == -1) perror("msgRezerveTimed: clock_gettime");

    ts.tv_nsec+= nsec;
    int rs = sem_timedwait(&pb->pile.sem, &ts);
    if(cause!=NULL) *cause = rs;
    if( rs == EAGAIN)
    {
        return(NULL);
    }
    fl_t n=getNode(&pb->pile);
    return(n);
}

int postGetPileCount(post_box_t* pb)
{
    if(pb==NULL)pb = defPB;
    int cnt, rc;
    rc = sem_getvalue(&pb->pile.sem, &cnt);
    return((rc==0)?cnt:rc);
}*/

/* Append the "from" LinkedList chain to the tail of the "to" LinkedList.
 * If "to" was empty, it will fill it with "from".
 * "from" is always left empty at return.
 */
 /*
void moveList(fl_head_t* from, fl_head_t*  to)
{
	portENTER_CRITICAL(&from->mux);
	ESP_LOGW("movList","from.count = %u, to.count = %u",from->count, to->count);
	if((from->count>0)||(from->head!=NULL))
	{
		portENTER_CRITICAL(&to->mux);
		if(to->tail!=NULL)
		{
			to->tail->nxt = from->head;
		}
		if(to->head==NULL) to->head = from->head;
		to->tail = from->tail;
		to->count+=from->count;
		portEXIT_CRITICAL(&to->mux);
	}
	from->count=0;
	from->tail=NULL;
	from->head=NULL;
	ESP_LOGW("movList","Done: from.count = %u, to.count = %u",from->count, to->count);
	portEXIT_CRITICAL(&from->mux);
}

IRAM_ATTR BaseType_t getOwner(fl_head_t* hd, TickType_t blockT)
{
	return(xSemaphoreTake(hd->owner, blockT));
}

IRAM_ATTR void returnOwner(fl_head_t* hd)
{
	xSemaphoreGive(hd->owner);
}
*/
/*
int InitArtList(uint32_t count, fl_head_t* hd)
{
	fl_t nd;
	int i;
	// always init mutexes, even if initial count of LL is 0
	//hd->mux = cmx;
	pthread_mutex_init(&hd->mux, NULL);
    pthread_mutex_lock(&hd->mux);
    sem_init(&hd->sem, 0, 0);
	hd->tail = NULL;
	hd->tail = NULL;
    hd->count=0;
    hd->collisions = 0;
	if(count>0)
	{
		hd->head = (fl_t)calloc(count, sizeof(freeList_t));
		if(hd->head)
		{
			hd->count = count;
			nd=hd->head;
			for(i=0;i<count-1;i++)
			{
                nd->item.hdr.msgid = art_rq_msg_e;
				nd->item.pl.itemId = i;
				nd->nxt = nd+1;
				nd = nd->nxt;
			}
			nd->item.hdr.msgid = art_rq_msg_e;
			nd->item.pl.itemId = i;
			nd->nxt=NULL;
			hd->tail = nd;
		}
	}
    pthread_mutex_unlock(&hd->mux);
	return(0);
}


static int InitPBList(uint32_t count, fl_head_t* hd)
{
	fl_t nd;
	int i;
	// always init mutexes, even if initial count of LL is 0
	//hd->mux = cmx;
	pthread_mutex_init(&hd->mux, NULL);
    pthread_mutex_lock(&hd->mux);
	hd->tail = NULL;
	hd->tail = NULL;
    hd->count=0;
    hd->collisions = 0;
	if(count>0)
	{
		hd->head = (fl_t)calloc(count, sizeof(freeList_t));
		if(hd->head)
		{
			hd->count = count;
			nd=hd->head;
			for(i=0;i<count-1;i++)
			{
				nd->nxt = nd+1;
				nd->item.hdr.msgtype = def_msg_e;
				nd->item.pl.itemId = i;
				nd = nd->nxt;
			}
			nd->item.hdr.msgtype = def_msg_e;
            nd->item.pl.itemId = i;
			nd->nxt=NULL;
			hd->tail = nd;
		}
	}
    pthread_mutex_unlock(&hd->mux);
	return(0);
}*/
