#include "rq.h"
#include <errno.h>

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)
static post_box_t* defPB = NULL;

post_box_t* createPB(uint32_t count, const char* name, void* rqs, uint32_t q_size)
{
    post_box_t* pb = (post_box_t*)calloc(1, sizeof(post_box_t));
    if(pb == NULL)return(NULL);
    if(name!=NULL)strncat(pb->pbName, name, 31);
    memset((void*)(&pb->rq_head), 0, sizeof(fl_head_t));
    #ifdef LL_BUFFER
    InitPBList(count, &pb->pile);
    InitPBList(0, &pb->box);
    sem_init(&pb->box.sem, 0, 0);
    sem_init(&pb->pile.sem, 0, count);

    #else

    pb->rq_head.count = 0;
    pb->rq_head.genQ = rqs;
    pb->rq_head.head = (pb->rq_head.head - 1)& (count-1u);
    pb->rq_head.Q_MSK = (count-1u);
    pb->rq_head.slot_sz = q_size / count;
    pthread_spin_init( &pb->rq_head.lock,  PTHREAD_PROCESS_PRIVATE );
    #endif
    return(pb);
}

void  setDefPB(const post_box_t* pb)
{
    defPB =pb;
}

#ifndef LL_BUFFER

int rezerveMsgs(int count, fl_head_t* hd)
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
peer_pack_t* rezerveMsg( fl_head_t* hd)
{
    register uint32_t count = __atomic_load_n(&hd->count,  __ATOMIC_RELAXED);
    if(count > hd->Q_MSK)return(NULL);
    return (  &hd->genQ[ hd->slot_sz * ((hd->head +1) &  hd->Q_MSK) ] );
}

/*  get a pointer to next unread msg */
peer_pack_t* getMsg(fl_head_t* hd)
{
    register uint32_t count = __atomic_load_n(&hd->count,  __ATOMIC_RELAXED);
    if(count <1)return(NULL);

    return (&hd->genQ[ ( hd->slot_sz * hd->tail )]);
}

/* mark a msg are "read" and move tail index , decrease count */
void msgRead(fl_head_t* hd)
{
    pthread_spin_lock(&hd->lock);
    hd->count = __atomic_sub_fetch(&hd->count, 1, __ATOMIC_RELAXED);
    pthread_spin_unlock(&hd->lock);
    hd->tail = ((++hd->tail) & hd->Q_MSK);
    hd->tailStep++;
}

/* mark a msg are "read" and move tail index , decrease count , copy msg into buffer "pack"*/
int getCopyMsg(fl_head_t* hd, peer_pack_t* pack)
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
void msgWritten(fl_head_t* hd)
{
    pthread_spin_lock(&hd->lock);
    hd->count = __atomic_add_fetch(&hd->count, 1, __ATOMIC_RELAXED);
    pthread_spin_unlock(&hd->lock);
    hd->head = ((++hd->head) & hd->Q_MSK);
    hd->headStep++;
}

/* mark cnt slots as written, meaning slot processing is done, msgs will be available to receiver */
void msgWrittenMulti(fl_head_t* hd, int cnt)
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
int rezerveMsgMulti(fl_head_t* hd,peer_pack_t** p, int cnt)
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

int get_pb_info(fl_head_t* from, fl_head_t* to)
{
    pthread_spin_lock(&from->lock);
    *to = *from;
    pthread_spin_unlock(&from->lock);
}
int post_msg(fl_head_t* hd, void* data, int len)
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

int get_taces(fl_head_t* hd, int* indexes, int max_count )
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
void free_traces(fl_head_t* hd, int count)
{
    pthread_spin_lock(&hd->lock);
    hd->count -=count;
    hd->tail += count;
    hd->tail &= hd->Q_MSK;
    pthread_spin_unlock(&hd->lock);
}
#else






int InitArtList(uint32_t count, fl_head_t* hd)
{
	fl_t nd;
	int i;
	/* always init mutexes, even if initial count of LL is 0 */
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
	/* always init mutexes, even if initial count of LL is 0 */
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
}

/* Removes the head item from the LinkedList, and returns it to caller.
 * if it is the last item in list then head and tail are set to NULL.
 */
fl_t getNode(fl_head_t* hd)
{
    while( pthread_mutex_trylock(&hd->mux)!=0)
    {

      __atomic_add_fetch(&hd->collisions, 1, __ATOMIC_RELAXED);
    }
	//pthread_mutex_lock(&hd->mux);
	if(hd->head==NULL)
	{
		if(unlikely( hd->count!=0 ) ) /*sanity + debugging check */
		{
			printf("Head Null, count %u in %p\n",hd->count, hd);
			hd->count = 0;
		}
		pthread_mutex_unlock(&hd->mux);
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
		printf("Head NOT Null, count was 0, but actual %u in %p\n", hd->count, hd);
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
	pthread_mutex_unlock(&hd->mux);
	return(nd);
}



int getNodes(fl_head_t* hd, fl_t* nodes)
{
    int count =0;
    while( pthread_mutex_trylock(&hd->mux)!=0)
    {
      __atomic_add_fetch(&hd->collisions, 1, __ATOMIC_RELAXED);
    }
	//pthread_mutex_lock(&hd->mux);
	if(hd->head==NULL)
	{
		if(unlikely( hd->count!=0 ) ) /*sanity + debugging check */
		{
			printf("Head Null, count %u in %p\n",hd->count, hd);
			hd->count = 0;
		}
		pthread_mutex_unlock(&hd->mux);
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
        printf("Head NOT Null, count was %d, but actual %u in %p\n",count, hd->count, hd);
    }
	nd=hd->head;
    hd->count = 0;
    hd->tail = NULL;
    hd->head = NULL;
	*nodes = nd;
	pthread_mutex_unlock(&hd->mux);
	return(count);
}


/* Appends a node item to the tail of the "hd" Linked List. */
uint32_t putNode(fl_head_t* hd, fl_t nd)
{
	//__sync_fetch_and_add(&hd->count, 1ul);
	//
	uint32_t cnt;
	if( unlikely(hd == NULL)) return 0;
	pthread_mutex_lock(&hd->mux);
	if(likely(nd!=NULL))
	{
		if(hd->tail!=NULL)
		{
			hd->tail->nxt = nd;
		}
		hd->tail = nd;
		hd->tail->nxt = NULL;
		if(hd->head==NULL)hd->head = nd;
		hd->count++;
	}
	cnt = hd->count;
	pthread_mutex_unlock(&hd->mux);
	return(cnt);
	//(void)__sync_val_compare_and_swap(&hd->head,(uint32_t)NULL,nd);
}

inline uint32_t getNodeCount(fl_head_t* hd)
{

	uint32_t cnt;

	pthread_mutex_lock(&hd->mux);
	cnt =  hd->count;
	pthread_mutex_unlock(&hd->mux);
	return(cnt);
	//return( (uint32_t)(__atomic_load_n(&hd->count,__ATOMIC_ACQ_REL)));
}


fl_t msgRxNB(post_box_t* pb)
{
    if(pb==NULL)pb = defPB;
    if( sem_trywait(&pb->box.sem) == EAGAIN) return NULL;
    return(getNode(&pb->box));
}

fl_t msgRxBL(post_box_t* pb)
{
    if(pb==NULL)pb = defPB;
    while(sem_wait(&pb->box.sem)<0);

    return(getNode(&pb->box));
}

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

fl_t msgPostNB(post_box_t* pb, const msg_t* msg)
{
    if(pb==NULL)pb = defPB;
    if( sem_trywait(&pb->pile.sem) == EAGAIN)return(NULL);
    fl_t n=getNode(&pb->pile);
    memcpy(&n->item, msg, sizeof(msg_t));
    putNode(&pb->box,  n);
    sem_post(&pb->box.sem);
    return(n);
}

fl_t msgPostBL(post_box_t* pb, const msg_t* msg)
{
    if(pb==NULL)pb = defPB;
    while( sem_wait(&pb->pile.sem) <0);
    fl_t n=getNode(&pb->pile);
    memcpy(&n->item, msg, sizeof(msg_t));
    putNode(&pb->box,  n);
    sem_post(&pb->box.sem);
    return(n);
}

void msgRelease(const post_box_t* pb, const fl_t msg)
{
    if(pb==NULL)pb = defPB;
    putNode(&pb->pile,  msg);
    sem_post(&pb->pile.sem);
}

void msgRezervedPostNB(post_box_t* pb, const fl_t msg)
{
    if(pb==NULL)pb = defPB;
    putNode(&pb->box,  msg);
    sem_post(&pb->box.sem);
}

fl_t msgRezerveNB(post_box_t* pb)
{
    if(pb==NULL)pb = defPB;
    if( sem_trywait(&pb->pile.sem) == EAGAIN)
    {
        return(NULL);
    }
    fl_t n=getNode(&pb->pile);
    return(n);
}

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
}
#endif
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
