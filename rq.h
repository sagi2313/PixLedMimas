#ifndef RQ_DEF
#define RQ_DEF
#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdint-gcc.h>
#include "type_defs.h"
#include "sys_defs.h"
//#define LL_BUFFER
#define ERROR_IDX  ((uint32_t)(-1))
//#define MIN( A , B ) ( ((A>B)?B:A) )

extern pthread_spinlock_t  prnlock;
typedef enum
{
    pb_ll_e,
    pb_rq_e
}pb_type_e;

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
        uint32_t        itemId;
        sock_data_msg_t  *msg;
        int32_t         vDevId;
}msg_payload_t;


typedef struct
{
    //msg_header_t        hdr;
	msg_payload_t       pl;
}msg_t;

typedef struct  __fList__t* fl_t;


typedef struct
{
#define             IDXMASK ( RQ_DEPTH  - 1u )
#define             EV_Q_MASK (EV_Q_DEPTH - 1u)
	uint_fast32_t		head __attribute__ ((aligned(64)));
	uint32_t        _p1[128 - sizeof(uint_fast32_t)];
	uint_fast32_t		tail __attribute__ ((aligned(64)));
	uint32_t        _p2[128 - sizeof(uint_fast32_t)];

	uint_fast32_t		headStep __attribute__ ((aligned(64)));
	uint32_t        _p1b[128 - sizeof(uint_fast32_t)];
	uint_fast32_t		tailStep __attribute__ ((aligned(64)));
	uint32_t        _p2b[128 - sizeof(uint_fast32_t)];

	uint_fast32_t 	count __attribute__ ((aligned(64)));
	uint32_t        _p3[128 - sizeof(uint_fast32_t)];
    uint_fast32_t 	low_mark __attribute__ ((aligned(64)));
	uint32_t         _p4[128 - sizeof(uint_fast32_t)];
    uint32_t        Q_MSK;
    uint32_t        slot_sz;
    union
    {
        uint8_t         *genQ;
        peer_pack_t     *rq;
        trace_msg_t     *evq;
    };
	pthread_spinlock_t  lock;
}rq_head_t;

typedef struct
{
    uint_fast32_t 	    count __attribute__ ((aligned(64)));
	uint32_t            _p1[128 - sizeof(uint_fast32_t)];
    uint_fast32_t 	    low_mark __attribute__ ((aligned(64)));
	uint32_t            _p2[128 - sizeof(uint_fast32_t)];
	uint32_t            maxCount;
	fl_t			    head;
	fl_t			    tail;
    pthread_spinlock_t  lock;
}fl_head_t;

typedef struct
{
    fl_head_t   pile;
    fl_head_t   box;
}ll_pb_t;

typedef struct
{
    pb_type_e pbTyp;
    union
    {
        ll_pb_t     ll;
        rq_head_t   rq;
    };
    char        pbName[32];
}post_box_t;

typedef struct __fList__t
{
    msg_t               item;
	post_box_t*         pb;
	fl_t		        nxt;
}freeList_t;

typedef struct
{
    fl_t hd;
    fl_t lst;
}node_branch_t;

void addToBranch(node_branch_t* br, fl_t nd);

//post_box_t* createPB(uint32_t count, const char* name, void* rqs, uint32_t q_size);
post_box_t* createPB_RQ(uint32_t count, const char* name, uint8_t* rqs, uint32_t q_size);
post_box_t* createPB_LL(uint32_t count, const char* name, uint8_t* pool, uint32_t item_size);
void msgWritten(rq_head_t* hd);
void msgWrittenMulti(rq_head_t* hd, int cnt);
void msgRead(rq_head_t* hd);
int getCopyMsg(rq_head_t* hd, peer_pack_t* pack);

#define INC_HEAD( H ) \
    H.head = ((++H.head) & IDXMASK)
#define INC_TAIL( T ) \
    T.tail = ((++T.tail) & IDXMASK)
int rezerveMsgs(int count, rq_head_t* hd);
peer_pack_t* rezerveMsg( rq_head_t* hd);
peer_pack_t* getMsg(rq_head_t* hd);
int rezerveMsgMulti(rq_head_t* hd,peer_pack_t** p, int cnt);
int get_pb_info(rq_head_t* from, rq_head_t* to);
int post_msg(rq_head_t* hd, void* data, int len);
int get_taces(rq_head_t* hd, int* indexes, int max_count );
void free_traces(rq_head_t* hd, int count);
void prn_pb_info(post_box_t* pb);
// LL API
fl_t getNode(fl_head_t* hd);
int getNodes(fl_head_t* hd, fl_t* nodes); // pull all pendiong node out at once
uint32_t putNode(post_box_t* pb, fl_t nd, fl_t *nnd);
uint32_t putNodes(post_box_t* pb, fl_t nd);
uint32_t getNodeCount(fl_head_t* hd);

fl_t msgPostNB(post_box_t* pb, const msg_t* msg, uint32_t sz);
fl_t msgPostBL(post_box_t* pb, const msg_t* msg, uint32_t sz);
void msgRelease(const post_box_t* pb, const fl_t msg);
fl_t msgRxNB(post_box_t* pb);
fl_t msgRxBL(post_box_t* pb);
fl_t msgRxBLTimed(post_box_t* pb, struct timespec *ts, int* cause);
int postGetPileCount(post_box_t* pb);
fl_t msgRezerveNB(post_box_t* pb);
void msgRezervedPostNB(post_box_t* pb, const fl_t msg);
fl_t msgRezerveTimed(post_box_t* pb, long nsec, int* cause);
int msgRezerveMultiNB(post_box_t* pb, fl_t* nodes, uint32_t count);
void prnLLdetail(fl_t br, char* WHO, char* name);
#define getFree(L , N, I) \
	do{ N = getNode((L)); \
	if(N == NULL){I = ERROR_IDX;} \
	else {I = N->item.pl.itemId;}}while(0);

#endif
