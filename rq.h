#ifndef RQ_DEF
#define RQ_DEF
#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdint-gcc.h>
#include "type_defs.h"
//#define LL_BUFFER
#define ERROR_IDX  ((uint32_t)(-1))
//#define MIN( A , B ) ( ((A>B)?B:A) )

#ifdef      LL_BUFFER
#define     RQ_DEPTH    (8u * (GLOBAL_OUTPUTS_MAX * UNI_PER_OUT))
#else
#define     RQ_DEPTH    (1024u)
#endif

typedef enum{
    no_msg_e      = -1,
    def_msg_e     = 0,
    sys_msg_e     = 1,
    art_rq_msg_e  = 2
}msgtypes_e;

typedef struct
{
    union
    {
        int             msgid;
        msgtypes_e      msgtype;
    };
    uint32_t    msgLen;
}msg_header_t;

typedef struct
{
        uint32_t    itemId;
        peer_pack_t  *msg;
}msg_payload_t;


typedef struct
{
    msg_header_t        hdr;
	msg_payload_t       pl;
}msg_t;

typedef struct  __fList__t* fl_t;
typedef struct __fList__t
{
    msg_t               item;
	void*               pb;
	fl_t		        nxt;

}freeList_t;

typedef struct
{
#ifdef LL_BUFFER
	fl_t			head;
	fl_t			tail;
    pthread_mutex_t 	mux;
	sem_t               sem;
    uint_fast32_t       collisions;
#else
#define             IDXMASK ( RQ_DEPTH  - 1u )
#define             EV_Q_MASK (EV_Q_DEPTH - 1u)
union{
	peer_pack_t     *rq;
	trace_msg_t     *evq;
	};
	uint_fast32_t		head __attribute__ ((aligned(64)));
	uint32_t        _p1[128 - sizeof(uint_fast32_t)];
	uint_fast32_t		tail __attribute__ ((aligned(64)));
	uint32_t        _p2[128 - sizeof(uint_fast32_t)];

	uint_fast32_t		headStep __attribute__ ((aligned(64)));
	uint32_t        _p1b[128 - sizeof(uint_fast32_t)];
	uint_fast32_t		tailStep __attribute__ ((aligned(64)));
	uint32_t        _p2b[128 - sizeof(uint_fast32_t)];

#endif
	uint_fast32_t 	count __attribute__ ((aligned(64)));
	uint32_t        _p3[128 - sizeof(uint_fast32_t)];
	pthread_spinlock_t  lock;
}fl_head_t;

typedef struct
{
#ifdef LL_BUFFER
    fl_head_t   pile;
    fl_head_t   box;
#else
    fl_head_t   rq_head;
#endif
    char        pbName[32];
}post_box_t;


post_box_t* createPB(uint32_t count, const char* name, void* rqs);
void  setDefPB(const post_box_t* pb);
void msgWritten(fl_head_t* hd);
void msgWrittenMulti(fl_head_t* hd, int cnt);
void msgRead(fl_head_t* hd);
#ifndef LL_BUFFER
#define INC_HEAD( H ) \
    H.head = ((++H.head) & IDXMASK)
#define INC_TAIL( T ) \
    T.tail = ((++T.tail) & IDXMASK)
int rezerveMsgs(int count, fl_head_t* hd);
peer_pack_t* rezerveMsg( fl_head_t* hd);
peer_pack_t* getMsg(fl_head_t* hd);
int rezerveMsgMulti(fl_head_t* hd,peer_pack_t** p, int cnt);
int get_pb_info(fl_head_t* from, fl_head_t* to);
int post_msg(fl_head_t* hd, void* data, int len);
int get_taces(fl_head_t* hd, int* indexes, int max_count );
void free_traces(fl_head_t* hd, int count);
#else
fl_t getNode(fl_head_t* hd);
int getNodes(fl_head_t* hd, fl_t* nodes); // pull all pendiong node out at once
uint32_t putNode(fl_head_t* hd, fl_t nd);
uint32_t getNodeCount(fl_head_t* hd);
int InitArtList(uint32_t count, fl_head_t* hd);

fl_t msgPostNB(post_box_t* pb, const msg_t* msg);
fl_t msgPostBL(post_box_t* pb, const msg_t* msg);
void msgRelease(const post_box_t* pb, const fl_t msg);
fl_t msgRxNB(post_box_t* pb);
fl_t msgRxBL(post_box_t* pb);
fl_t msgRxBLTimed(post_box_t* pb, struct timespec *ts, int* cause);
int postGetPileCount(post_box_t* pb);
fl_t msgRezerveNB(post_box_t* pb);
void msgRezervedPostNB(post_box_t* pb, const fl_t msg);
fl_t msgRezerveTimed(post_box_t* pb, long nsec, int* cause);
#define getFree(L , N, I) \
	do{ N = getNode((L)); \
	if(N == NULL){I = ERROR_IDX;} \
	else {I = N->item.pl.itemId;}}while(0);

#endif
#endif
