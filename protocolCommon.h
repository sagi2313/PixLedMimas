#ifndef PROTOCOLCOMMON_H_INCLUDED
#define PROTOCOLCOMMON_H_INCLUDED


/*

extern const char* rstcauses[];//={"Init","FrErr","ArtOther","NoSema","Socket Timeout"};
extern const char* smstates[];//={"out_of_sync_e","wait_sync","wait_seq","working_e","full_frame_e","process_out","free_e","frame_err"};

typedef enum //art_resp_e
{
	art_data_e=0,
	art_syn_pack_e,
	art_syncing_e,
	no_more_e,
	art_poll_e,
	art_sync_error_e,
	art_rx_buff_over_e,
	art_error_e	,
	net_error_e,
	addressing_net_err_e,
	addressing_uni_err_e,
	no_init_e,
	ignore_error_e
}art_resp_e;

typedef enum //sm_state_e
{
	out_of_sync_e,
	wait_sync_e,
	wait_min_uni_e,
	//wait_seq_e,
	working_e,
	full_frame_e,
	process_out_e,
	free_e,
	frame_err_e
}sm_state_e;

typedef enum //sm_reset_causes_e
{
	eResetInit=0,
	eResetFrErr,
	eResetArtOther,
	eResetNoSema,
	eResetSockTimeout,
	eResetMaxCause
}sm_reset_causes_e;

typedef enum //protoSelect_e
{
    protoUndef,
    protoArtNet,
    protosACN
}protoSelect_e;


#include "type_defs.h"

typedef union
{
	art_net_pack_t      art;
	e131_pack_t         sacn;

}any_prot_pack_t;

typedef union
{
    art_net_net_t anet;
    addressing_t  addr;
}gen_addres_u;

typedef struct
{
    struct sockaddr_in  sender;
    any_prot_pack_t     pl; //payload
}peer_pack_t;
#define PEER_PACK_T


node_t* createNode(protoSelect_e prot);
void SmReset(node_t* n, sm_reset_causes_e cause);
*/
#endif // PROTOCOLCOMMON_H_INCLUDED
