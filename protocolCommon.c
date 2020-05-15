#include "type_defs.h"
#include "protocolCommon.h"


const char* rstcauses[eResetMaxCause]={"Init","FrErr","ArtOther","NoSema","Socket Timeout"};
const char* smstates[8]={"out_of_sync_e","wait_sync","wait_seq","working_e","full_frame_e","process_out","free_e","frame_err"};


void SmReset(node_t* n, sm_reset_causes_e cause)
{
	sm_t* sm = &n->sm;
	memset((void*)sm, 0, sizeof(sm_t));
	sm->state = out_of_sync_e;
    sm->startNet = n->art_start_uni;

    int maxUni =(n->universe_count >(MIMAS_STREAM_OUT_CNT * UNI_PER_OUT))?(MIMAS_STREAM_OUT_CNT * UNI_PER_OUT):n->universe_count ;

    maxUni+= (n->art_start_uni  - 1);
    sm->endNet = maxUni;
    sm->universes_count = n->universe_count;
    sm->min_uni = 0xFFFF;
    printf("SM: State Machine reset(%s)\n",  rstcauses[cause]);
}


node_t* createNode(protoSelect_e p)
{
    node_t* n=(node_t*)malloc(sizeof(node_t));
    if(n!=NULL)
    {
        memset((void*)n, 0, sizeof(node_t));
        n->prot = p;
    }
    return(n);
}
