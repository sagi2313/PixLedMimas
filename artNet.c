/*
 * ArtNet.c
 *
 *  Created on: 28 Mar 2018
 *      Author: Sagi64
 */
#include "type_defs.h"
#include "artNet.h"
#include "utils.h"
//#include "protocolCommon.h"
static node_t *Node=NULL;
static gen_addres_u *sAdr, *eAdr;

void art_set_node(const node_t* N)
{
    Node = N;
    if(N!=NULL)
    {
        sAdr = &N->sm.startNet;
        eAdr = &N->sm.endNet;
    }
}

art_resp_e ArtNetDecode(const art_net_pack_t* p)
{
	art_net_opcodes_e	OpCod;

	if((memcmp(ARTNET_ID, p->head.id,7)==0))
	{
        if(Node==NULL)
        {
            return(no_init_e);
        }

		OpCod = (art_net_opcodes_e)(p->head.OpCode.OpCode);
		switch(OpCod)
		{
			case OpDmx:
			{
				art_dmx_data_t *d;
				d = &(p->ArtDmxOut);
				if(d->a_net.net!=sAdr->anet.net)
				{
					printf("artnet: Net %u out of bounds\n", d->a_net.net);
					return(addressing_net_err_e);
				}
				uint8_t univ = (d->a_net.SubUni.subuni_full);
				if((univ<sAdr->anet.SubUni.subuni_full)||(univ>eAdr->anet.SubUni.subuni_full))
				{
					//printf("artnet: SubNet %u out of bounds\n", univ);
					return(addressing_uni_err_e);
				}
				return(art_data_e);
			}
			case OpPoll:
			{
				//make_artnet_resp();
				return(art_poll_e);
				break;
			}
			case OpSync:
			{
				//printf("aSync\n");
				return(art_syn_pack_e);
				break;
			}
			default:
			{
				printf("artnet: Unknown OpCod %u (0x%X)\n",(uint32_t)OpCod,(uint32_t)OpCod);
				return(art_error_e);
			}
		}
	}
	printf("artnet: ArtID err 0x08lX\n", *(uint64_t*)&p->head.id[0]);
	return(ignore_error_e);
}

static uint8_t art[sizeof(art_net_pack_t)];

void make_artnet_resp(peer_pack_t* pp)
{
	int i,k,j,cnt;
	uint8_t max_uni;
	art_net_pack_t *p=(art_net_pack_t*)&art[0];
	art_net_poll_rep_t *pr = &p->PollRep;
	//peerp->sockt = Node->ArtSock;
	memset(art,0,sizeof(art));
	p->head.OpCode.OpCode = OpPollReply;
	strcat(p->head.id,"Art-Net\0");

	memcpy(&pr->myIp, &Node->ifs.ifs[Node->ifs.curr_if_idx].sockaddr.sin_addr , sizeof(ip4_addr_t));
	memcpy(&pr->my_mac, &Node->ifs.ifs[Node->ifs.curr_if_idx].mac , 8);
	memcpy(&pr->bindIP, &pr->myIp, sizeof(ip4_addr_t));

    strcpy(&pr->ShortName[0] ,Node->nodeName);
	strcpy(&pr->LongName[0], Node->longName);
	strcpy(&pr->NodeReport[0],"#0001 [0000]");

	pr->port = ARTNET_PORT;
	pr->status1.bitf.port_programming = 1;
	pr->status2.bitf.addressing15bit = 1;
	pr->EstaManCode.Ushort = ESTA_CODE;

	pr->myVersInfo = NODE_VERSION;
	pr->netSwitch = sAdr->anet.net;
	pr->subSwitch = sAdr->anet.SubUni.subnet;
	pr->bindIdx = 1;
	cnt = 0;
	k=0;
	max_uni = Node->universe_count;
	while(k<max_uni)
	{
		for(i=0;i<4;i++)
		{
			*(uint8_t*)(&pr->goodIn[i])=0;
			pr->goodIn[i].inp_disabled=1;
			*(uint8_t*)(&pr->goodOut[i])=0;
			pr->goodOut[i].data_txed=1;
		}
		memset(&pr->port_types[0],0,sizeof(prt_type_s));

		j=((max_uni - k)>4?4:(max_uni - k));
		pr->NumPorts.Uchr[1] = j;
		for(i=0;i<j; k++)
		{
			pr->port_types[i].output=1;
			pr->port_types[i].prtType = prtDMX512;
			pr->swOut[i]= ((sAdr->anet.SubUni.universe + k)& 0xF);// swout++;

			printf("Fill (j: %d) k: %02d subSw: %d swout: %02d",j,k,pr->subSwitch,pr->swOut[i] );
			if(pr->swOut[i] == 15)
			{
				pr->NumPorts.Uchr[1] = i+1;
				k++;
				break;
			}
			i++;
		}

		j = sendto(Node->sockfd,art,sizeof(art_net_pack_t),0,(const struct sockaddr*)&pp->sender, sizeof(struct sockaddr_in));
		cnt++;
		pr->bindIdx++;
		if(j == sizeof(art_net_pack_t))
		{
			printf("Artnet Poll Resp(%u) ok\n\r", cnt);
		}
		else
		{
			printf("Artnet Poll Resp to %s:error(%u,%d)\n\r", inet_ntoa(pp->sender.sin_addr),cnt, j);
			show_socket_error_reason(Node->sockfd);
		}
		if(pr->swOut[i] == 15)				pr->subSwitch ++;
		//pr->subSwitch = Node->app_cfg.art_start_uni.SubUni.subnet + (k/16);
	}
}

