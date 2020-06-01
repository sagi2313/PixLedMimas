/*
 * ArtNet.h
 *
 *  Created on: 28 Mar 2018
 *      Author: Sagi64
 */

#ifndef MAIN_ARTNET_H_
#define MAIN_ARTNET_H_



//#include <net/if.h>
//#include <net/ethernet.h>
//#include <netinet/in.h>
#include <arpa/inet.h>
/*
 * ArtNet.h
 *
 * Created: 7/2/2016 14:06:54
 *  Author: Sagi
 */
#ifdef PACK_STRUCT_STRUCT
#undef PACK_STRUCT_STRUCT
#define PACK_STRUCT_STRUCT
#endif

#define ARTNET_PORT (uint16_t)0x1936
#define ARTNET_ID	(const char*) "Art-Net\0"
#define ARTNET_ID_RAW (*(uint64_t*)("Art-Net\0"))


#define prtDMX512	(uint8_t)0
#define	prtMIDI		(uint8_t)1
#define	prtAvab		(uint8_t)2
#define	prtCMX		(uint8_t)3
#define	prtADB		(uint8_t)4
#define	prtArtNet	(uint8_t)5
#define prtInput	(uint8_t)64
#define prtOutput	(uint8_t)128
#pragma pack(1)

typedef union __attribute__((packed))
{
	char   		Schr[2];
	uint8_t 	Uchr[2];
	uint16_t 	Ushort;
	int16_t		Sshort;
}union16_t ;
typedef enum  __attribute__((packed))
{
	OpPoll		=	0x2000,
	OpPollReply	=	0x2100,
	OpDiagData	=	0x2300,
	OpCommand	=	0x2400,
	OpDmx		=	0x5000,
	OpNzs		=	0x5100,	//This is an ArtNzs data packet. It contains non-zero start code (except RDM) DMX512 information for a single Universe.
	OpSync		=	0x5200,
	OpAddress	=	0x6000,
	OpInput		=	0x7000,
	OpTodReq	=	0x8000,	//RDM TableOfDevices
	OpTodData	=	0x8100,
	OpTodCtrl	=	0x8200,
	OpRdm		=	0x8300,
	OpRdmSub	=	0x8400,
	OpVidSetup	=	0xA010,
	OpVidPalette=	0xA020,
	OpVidData	=	0xA040,
	OpOther
}art_net_opcodes_e;

typedef union __attribute__((packed))
{
	uint16_t/*art_net_opcodes_e*/ OpCode;
	struct __attribute__((packed))
	{
		uint8_t HiCode;
		uint8_t LoCode;
	}b __attribute__((packed));
}art_net_opcodes_u;

typedef struct  __attribute__((packed))
{
	uint8_t	dc1:1;	//don't care
	uint8_t	send_poll_reply:1;
	uint8_t	diag_enabled:1;
	uint8_t	diag_type:1;
	uint8_t	vlc_enable:1;
}TalkToMe_s __attribute__((packed));

typedef union
{
	uint8_t		takl_to_me;
	TalkToMe_s	bitf;
}TalkToMe_u;

typedef union
{
	uint8_t	stat;
	struct
	{
		uint8_t	ubea:1;
		uint8_t	rdm_cap:1;
		uint8_t	dualBoot:1;
		uint8_t	dc:1;
		uint8_t	port_programming:2;
		uint8_t	indicator_state:2;
	}bitf __attribute__((packed));
}node_status1_u;

typedef union
{
	uint8_t	stat;
	struct
	{
		uint8_t	web_cfg:1;
		uint8_t	ip_auto:1;
		uint8_t	dhcp:1;
		uint8_t	addressing15bit:1;
	}bitf __attribute__((packed));
}node_status2_u __attribute__((packed));

typedef struct
{
		uint8_t	  prtType:6;
		uint8_t	  input:1;
		uint8_t	  output:1;
}prt_type_s __attribute__((packed));

typedef struct
{
	uint8_t unused:2;
	uint8_t rx_error:1;
	uint8_t inp_disabled:1;
	uint8_t dmx512_txt:1;
	uint8_t dmx512_sips:1;
	uint8_t dmx512_tst:1;
	uint8_t data_rxed:1;
}good_in_s __attribute__((packed));

typedef struct
{
	uint8_t Art_OrsACN:1;
	uint8_t mergeLTP:1;
	uint8_t dmx_short:1;
	uint8_t mergeArtNet:1;
	uint8_t dmx512_txt:1;
	uint8_t dmx512_sips:1;
	uint8_t dmx512_tst:1;
	uint8_t data_txed:1;
}good_out_s __attribute__((packed));

typedef union {
addressing_t raw_addr;
struct
{
	union
	{
		struct
		{
			uint8_t  universe:4;
			uint8_t  subnet:4;
		}__attribute__((packed));
		uint8_t subuni_full;
	}SubUni __attribute__((packed));
	uint8_t	net:7;
	};
}art_net_net_t __attribute__((packed));


typedef struct
{
	char				id[8];
	art_net_opcodes_u	OpCode;
}art_net_head_t;

typedef in_addr_t ip4_addr_t;


typedef struct
{
    art_net_head_t	head;
    union16_t		version;
    union16_t		aux;
}art_net_sync_t;

typedef struct
{
	art_net_head_t	head;
	ip4_addr_t		myIp;
	uint16_t		port;
	uint16_t		myVersInfo;
	uint8_t			netSwitch;
	uint8_t			subSwitch;
	union16_t		Oem;
	uint8_t			ubea;
	node_status1_u	status1;
	union16_t		EstaManCode;
	char			ShortName[18];
	char			LongName[64];
	char			NodeReport[64];
	union16_t		NumPorts;
	prt_type_s		port_types[4];
	good_in_s		goodIn[4];
	good_out_s		goodOut[4];
	uint8_t			swIn[4];
	uint8_t			swOut[4];
	uint8_t			swVideo;
	uint8_t			swMacro;
	uint8_t			swRemote;
	uint8_t			spare[3];
	uint8_t			style;
	uint8_t			my_mac[6];
	ip4_addr_t		bindIP;
	uint8_t			bindIdx;
	node_status2_u	status2;
	uint8_t			filler[26];
}art_net_poll_rep_t __attribute__((packed));


typedef struct
{
	art_net_head_t	head;
	union16_t		version;
	TalkToMe_u		talk_to_me;
	uint8_t			diag_priority;
}art_net_poll_t;

typedef struct
{
	art_net_head_t 	head;
	union16_t		version;
	uint8_t			seq;
	uint8_t			phys;
	art_net_net_t	a_net;
	union16_t		len;
	uint8_t			dmx[512];
}art_dmx_data_t;

 typedef union
{
	art_net_head_t 		head;
	art_net_poll_t		PollReq;
	art_net_poll_rep_t	PollRep;
	art_dmx_data_t		ArtDmxOut;
}art_net_pack_t;

#define ARTNET_PKTSIZE (sizeof(art_net_pack_t))


#pragma pack()
#include "type_defs.h"

void art_set_node(const node_t* N);
#ifdef PEER_PACK_T
void make_artnet_resp(peer_pack_t* pp);

#endif
art_resp_e ArtNetDecode(const art_net_pack_t* p);

#define ARTN_TAG ARTNET_ID
#define ESTA_CODE ((uint16_t)(0)) /* ((uint16_t)*(uint16_t*)"sA")*/
#endif /* MAIN_ARTNET_H_ */
