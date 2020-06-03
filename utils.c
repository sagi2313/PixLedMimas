//create a udp server socket. return ESP_OK:success ESP_FAIL:error
#include "utils.h"
#include "mimas_cfg.h"
#include <sys/sysinfo.h>

void setHandler(void (*handler)(int,siginfo_t *,void *));
void fault_handler(int signo, siginfo_t *info, void *extra);
void print_trace(void);
void print_trace_gdb();

extern app_node_t* anetp;
node_t* Node = NULL;

int cpu_online=0;

log_lvl_e LogLvl[(int)log_src_max];
uint32_t  LogMask = BIT32(log_src_max) -1u;
const char *ll_str[] = {"DBG  ","INFO " ,"fINF","ERROR"};
const char *ls_str[(int)log_src_max] =
{
    "GEN ", "PROD", "CONS", "TRAC", "EVNT", "AHAN", "PIXH", "PWMH", "DMXH", "LL  ", "MIMA", "GeN "
};

task_cfg_t tasks[MAX_TASK_CNT] =
{
    {"PixlProd", 0, 0, PROD_PRIO, PROD_AFFINITY },
    {"PixlCons", 0, 0, CONS_PRIO, CONS_AFFINITY },
    {"PixlPixH", 0, 0, PIXH_PRIO, PIXH_AFFINITY },
    {"PixlPwmH", 0, 0, PWMH_PRIO, PWMH_AFFINITY },
    {"PixlStat", 0, 0, STAT_PRIO, STAT_AFFINITY },
    {"PixlMimW", 0, 0, MIMS_PRIO, MIMS_AFFINITY }
};


void setLogLevel(log_src_e src, log_lvl_e lvl)
{
    LogLvl[(int)src] = lvl;
}

log_lvl_e getLogLevel(log_src_e src)
{
    return(LogLvl[(int)src] );
}

void initLogLevels(log_lvl_e lvl)
{
    for(int i=0;i<log_src_max;i++)
    {
        LogLvl[i] = lvl;
    }
}

int sys_init(void)
{
    int res, i;
    initLogLevels(DEF_LOG_LVL);
    prnFinf(log_any,"BCM lib version = %u\n", bcm2835_version());
    cpu_online = get_nprocs();
    res = bcm2835_init();
    if(res!=1)
    {
        prnErr(log_any,"Error %d init bcm2835\n", res);
        return(-1);
    }
    setHandler(fault_handler);
    for(i=0;i<MAX_TASK_CNT;i++)
    {
        tasks[i].affinity &= (BIT8(cpu_online)-1u);
    }
}

uint64_t next_pow2_64(uint64_t x)
{
 return x < 2 ? x : 1<<(64-__builtin_clzl(x-1));
}
uint32_t next_pow2_32(uint32_t x)
{
    return x < 2 ? x : 1<<(32-__builtin_clz(x-1));
}

static int get_socket_error_code(int socket)
{
    int result;
    uint32_t optlen = sizeof(int);
    if(getsockopt(socket, SOL_SOCKET, SO_ERROR, &result, &optlen) == -1)
    {
		printf("getsockopt failed\n");
		return -1;
    }
    return result;
}

int show_socket_error_reason(int socket)
{
    int err = get_socket_error_code(socket);
    printf("socket error %d %s\n", err, strerror(err));
    return err;
}

inline void mapColor(uint8_t *src, out_def_t *oout, int sUni)
{
    uint_fast32_t i;
    uint8_t* dst = oout->wrPt[sUni];
    oout->fillMap|=BIT8(sUni);
    uint8_t il = anetp->artnode->intLimit;
    uint32_t tint;
    if(il == 0 )
    {
        memset((void*)dst, 0, oout->uniLenLimit[sUni]);
        return;
    }
    switch(oout->colMap)
    {
        case grb_map_e:
        {
            for(i=0;i<oout->uniLenLimit[sUni];)
            {
                tint = src[i + 1];
                tint *= il;
                dst[i] = (uint8_t)(tint / 100u);

                tint = src[i];
                tint *= il;
                dst[i + 1] = (tint/ 100u);

                tint = src[i + 2];
                tint *= il;
                dst[i + 2] = (tint / 100u);
                i+=3;
            }
            break;
        }
        default: // unknown vector layout ignores intensity limit at this point
        {
            memcpy((void*)dst, (void*)src, oout->uniLenLimit[sUni]);
            break;
        }
    }
}

int setSockTimout(int sock, int ms)
{
	struct timeval tv;
	int res;
	tv.tv_sec = ms/1000u;
	tv.tv_usec = 1000l * (ms%1000);
	res = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv));
	return res;
}

/*returns 0 if wired or error, 1 if wirelss*/
int check_wireless(const char* ifname)
{
  int sock = -1;
  struct iwreq pwrq;
  memset(&pwrq, 0, sizeof(pwrq));
  strncpy(pwrq.ifr_name, ifname, IFNAMSIZ);

  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket");
    return 0;
  }

  if (ioctl(sock, SIOCGIWNAME, &pwrq) != -1) {
   /* if (protocol) strncpy(protocol, pwrq.u.name, IFNAMSIZ);*/
    close(sock);
    return 1;
  }

  close(sock);
  return 0;
}

void  get_ifs(void)
{
   struct ifaddrs *addrs,*tmp;

    getifaddrs(&addrs);
    tmp = addrs;

    while (tmp)
    {
        if (tmp->ifa_addr && tmp->ifa_addr->sa_family == AF_PACKET)
        {
            printf("IF %s is %s, %s(flags %X)\n", tmp->ifa_name,((check_wireless(tmp->ifa_name)==1)?"wireless":"wired"), \
                   ((tmp->ifa_flags & IFF_LOOPBACK)?"loop":"no_loop"),tmp->ifa_flags);
            tmp = tmp->ifa_next;
        }
    }
}


/* This uses a non-standard parsing (ie not inet_aton, or inet_pton)
 * because of legacy choice to parse 10.8 as 10.8.0.0 not 10.0.0.8
 */
static int get_addr_ipv4(__u8 *ap, const char *cp)
{
    int i;

    for (i = 0; i < 4; i++) {
        unsigned long n;
        char *endp;

        n = strtoul(cp, &endp, 0);
        if (n > 255)
            return -1;      /* bogus network value */

        if (endp == cp) /* no digits */
            return -1;

        ap[i] = n;

        if (*endp == '\0')
            break;

        if (i == 3 || *endp != '.')
            return -1;      /* extra characters */
        cp = endp + 1;
    }

    return 1;
}

// This function is to open the netlink socket as the name suggests.
int netlink_open(struct rtnl_handle* rth)
{
    int addr_len;
    memset(rth, 0, sizeof(rth));

    // Creating the netlink socket of family NETLINK_ROUTE

    rth->fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (rth->fd < 0)
    {
        perror("cannot open netlink socket");
        return -1;
    }
    memset(&rth->local, 0, sizeof(rth->local));
    rth->local.nl_family = AF_NETLINK;
    rth->local.nl_groups = 0;

    // Binding the netlink socket
    if (bind(rth->fd, (struct sockaddr*)&rth->local, sizeof(rth->local)) < 0)
    {
        perror("cannot bind netlink socket");
        return -1;
    }
    addr_len = sizeof(rth->local);
    if (getsockname(rth->fd, (struct sockaddr*)&rth->local, (socklen_t*) &addr_len) < 0)
    {
        perror("cannot getsockname");
        return -1;
    }
    if (addr_len != sizeof(rth->local))
    {
        fprintf(stderr, "wrong address lenght %d\n", addr_len);
        return -1;
    }
    if (rth->local.nl_family != AF_NETLINK)
    {
        fprintf(stderr, "wrong address family %d\n", rth->local.nl_family);
        return -1;
    }
    rth->seq = time(NULL);
    return 0;
}

// This function does the actual reading and writing to the netlink socket
int rtnl_talk(struct rtnl_handle *rtnl, struct nlmsghdr *n, pid_t peer,
        unsigned groups, struct nlmsghdr *answer)
{
    int status;
    struct nlmsghdr *h;
    struct sockaddr_nl nladdr;
    // Forming the iovector with the netlink packet.
    struct iovec iov = { (void*)n, n->nlmsg_len };
    char buf[8192];
    // Forming the message to be sent.
    struct msghdr msg = { (void*)&nladdr, sizeof(nladdr), &iov, 1, NULL, 0, 0 };
    // Filling up the details of the netlink socket to be contacted in the
    // kernel.
    memset(&nladdr, 0, sizeof(nladdr));
    nladdr.nl_family = AF_NETLINK;
    nladdr.nl_pid = peer;
    nladdr.nl_groups = groups;
    n->nlmsg_seq = ++rtnl->seq;
    if (answer == NULL)
        n->nlmsg_flags |= NLM_F_ACK;
    // Actual sending of the message, status contains success/failure
    status = sendmsg(rtnl->fd, &msg, 0);
    if (status < 0)
    {
        printf("talk status = %d, errno = %d\n", status, errno);
        perror("talk");
        return -1;
    }
    return(0);
}




// This is the utility function for adding the parameters to the packet.
int addattr_l(struct nlmsghdr *n, int maxlen, int type, void *data, int alen)
{
    int len = RTA_LENGTH(alen);
    struct rtattr *rta;

    if (NLMSG_ALIGN(n->nlmsg_len) + len > maxlen)
        return -1;
    rta = (struct rtattr*)(((char*)n) + NLMSG_ALIGN(n->nlmsg_len));
    rta->rta_type = type;
    rta->rta_len = len;
    memcpy(RTA_DATA(rta), data, alen);
    n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + len;
    return 0;
}


int get_addr_1(inet_prefix *addr, const char *name, int family)
{
    memset(addr, 0, sizeof(*addr));

    if (strcmp(name, "default") == 0 ||
            strcmp(name, "all") == 0 ||
            strcmp(name, "any") == 0) {
        if (family == AF_DECnet)
            return -1;
        addr->family = family;
        addr->bytelen = (family == AF_INET6 ? 16 : 4);
        addr->bitlen = -1;
        return 0;
    }

    if (strchr(name, ':')) {
        addr->family = AF_INET6;
        if (family != AF_UNSPEC && family != AF_INET6)
            return -1;
        if (inet_pton(AF_INET6, name, addr->data) <= 0)
            return -1;
        addr->bytelen = 16;
        addr->bitlen = -1;
        return 0;
    }


    addr->family = AF_INET;
    if (family != AF_UNSPEC && family != AF_INET)
        return -1;

    if (get_addr_ipv4((__u8 *)addr->data, name) <= 0)
        return -1;

    addr->bytelen = 4;
    addr->bitlen = -1;
    return 0;
}

int get_prefix(inet_prefix *dst, char *arg, int family)
{
    int err;
    unsigned plen;

    memset(dst, 0, sizeof(*dst));

    if (strcmp(arg, "default") == 0 ||
            strcmp(arg, "any") == 0 ||
            strcmp(arg, "all") == 0) {
        if (family == AF_DECnet)
            return -1;
        dst->family = family;
        dst->bytelen = 0;
        dst->bitlen = 0;
        return 0;
    }

    err = get_addr_1(dst, arg, family);
    if (err == 0) {
        switch(dst->family) {
            case AF_INET6:
                dst->bitlen = 128;
                break;
            case AF_DECnet:
                dst->bitlen = 16;
                break;
            default:
            case AF_INET:
                dst->bitlen = 32;
        }
    }
    return err;
}



int add_IP_Address(char * IP)
{
    struct rtnl_handle RTH;

    struct rtnl_handle * rth = &RTH;
    netlink_open(rth);
    inet_prefix lcl;
    // structure of the netlink packet.
    struct {
        struct nlmsghdr     n;
        struct ifaddrmsg    ifa;
        char            buf[1024];
    } req;

    memset(&req, 0, sizeof(req));
    req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    req.n.nlmsg_type = RTM_NEWADDR;
    req.n.nlmsg_flags = NLM_F_CREATE | NLM_F_EXCL | NLM_F_REQUEST;


//  req.n.nlmsg_type = RTM_DELADDR;
//  req.n.nlmsg_flags = NLM_F_REQUEST;

    req.ifa.ifa_family = AF_INET ;
    req.ifa.ifa_prefixlen = 32 ;
    req.ifa.ifa_index = 1 ; // get the loopback index
    req.ifa.ifa_scope = 0 ;

    get_prefix(&lcl, IP, req.ifa.ifa_family);
    if (req.ifa.ifa_family == AF_UNSPEC)
        req.ifa.ifa_family = lcl.family;
    addattr_l(&req.n, sizeof(req), IFA_LOCAL, &lcl.data, lcl.bytelen);

    if (rtnl_talk(rth, &req.n, 0, 0, NULL) < 0)
        return -2;
}

int sock_bind(int sockfd, const char* ifName, const  in_addr_t* bindIP, uint16_t portno)
{

    int res;
    char tmpStr[64];
    memset(tmpStr, 0, 64);
    int len;
    int optval = 1;
    struct sockaddr_in serveraddr;
    if(ifName!=NULL)
    {
        res = setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, (const void *)&ifName[0] , strlen(ifName));
        if(res)perror("sockOpt set IF (SO_BINDTODEVICE) error:");
        else printf("Set IF to '%s', OK\n",ifName);
    }
    len = 64;
    res = getsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, tmpStr, &len);
    if(res)perror("sockOpt get IF (SO_BINDTODEVICE) error:");
    else printf("Get IF OK -> '%s'\n",tmpStr);

    res= setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int));
    if(res)perror("sockOpt SO_REUSEADDR error:");
    res= setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, (const void *)&optval , sizeof(int));
    if(res)perror("sockOpt SO_BROADCAST error:");
    memset((char *) &serveraddr,0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    //serveraddr.sin_addr.s_addr = inet_addr("192.168.1.6");
    if(bindIP == NULL)
    {
        serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else
    {
        serveraddr.sin_addr.s_addr = (*bindIP);
    }
//serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);
    res = bind(sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) ;
    if (res < 0)  perror("ERROR on binding");
    return res;
}

int socket_init(node_interfaces_detail_t* ifs)
{
    int res;
    int sockfd;

    //get_ifs();
/* socket: create the parent socket */
    sockfd = socket(AF_INET, /*SOCK_NONBLOCK|*/SOCK_DGRAM, 0);

    if (sockfd < 0)
    {
        error("ERROR opening socket");
        return(sockfd);
    }
/* setsockopt: Handy debugging trick that lets us rerun the server immediately after we kill it; otherwise we have to wait about 20 secs.
 * Eliminates "ERROR on binding: Address already in use" error. */

    if(ifs!=NULL)
    {
        res = sock_bind(sockfd, ifs->if_name, NULL, 0);
        if(res)return(-1);
    }
    //volatile int result = altBind(sockfd);
    return(sockfd);
}

int socket_set_blocking(const int sockfd, int on)
{
   int flags = fcntl(sockfd, F_GETFL, 0);
   if (flags == -1)
   {
    perror("Failed to get sock_flags");
    return -1;
   }
   flags = (on!=0) ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
   if(fcntl(sockfd, F_SETFL, flags)!=0)
   {
    perror("Failed to set sock_flags");
    return -2;
   }
   return(0);
}

int altBind(int sockfd)
{
struct sockaddr_ll my_addr;
 struct ifreq s_ifr;
int res;

 strncpy (s_ifr.ifr_name, "eth0", sizeof(s_ifr.ifr_name));

 /* get interface index of eth0 */
 res = ioctl(sockfd, SIOCGIFINDEX, &s_ifr);
 if(res!=0)
 {
    perror("ioctl err: ");
 }
unsigned int idx = if_nametoindex ("eth0");
 /* fill sockaddr_ll struct to prepare binding */
 my_addr.sll_family = AF_INET;//AF_PACKET;
 //my_addr.sll_protocol = htons(ETH_P_ALL);
 my_addr.sll_ifindex =  s_ifr.ifr_ifindex;
 res = bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr_ll));
if(res!=0)perror("AltBind err:");
 /* bind socket to eth0 */
 return (res);
}

void getInterfaces(void)
{
    struct ifaddrs *addrs,*tmp;

    getifaddrs(&addrs);
    tmp = addrs;

    while (tmp)
    {
        if (tmp->ifa_addr && tmp->ifa_addr->sa_family == AF_PACKET)printf("%s\n", tmp->ifa_name);
        tmp = tmp->ifa_next;
    }

    freeifaddrs(addrs);
}

void prnIfFlags(short ifrf)
{
	printf("\t\t FLAGS: %X\n",ifrf);
    printf("\t\t%16s(%04X): %16s\n", "IFF_UP          ",  IFF_UP          ,( ifrf & IFF_UP          )?"yes":"no");
	printf("\t\t%16s(%04X): %16s\n", "IFF_BROADCAST	  ",IFF_BROADCAST	 ,( ifrf & IFF_BROADCAST	 )?"yes":"no");
	printf("\t\t%16s(%04X): %16s\n", "IFF_DEBUG		  ",IFF_DEBUG		 ,( ifrf & IFF_DEBUG		 )?"yes":"no");
	printf("\t\t%16s(%04X): %16s\n", "IFF_LOOPBACK	  ",IFF_LOOPBACK	 ,( ifrf & IFF_LOOPBACK	 )?"yes":"no");
	printf("\t\t%16s(%04X): %16s\n", "IFF_POINTOPOINT ",	  IFF_POINTOPOINT ,( ifrf & IFF_POINTOPOINT )?"yes":"no");
	printf("\t\t%16s(%04X): %16s\n", "IFF_RUNNING	  ",  IFF_RUNNING	 ,( ifrf & IFF_RUNNING	 )?"yes":"no");
	printf("\t\t%16s(%04X): %16s\n", "IFF_NOARP		  ",IFF_NOARP		 ,( ifrf & IFF_NOARP		 )?"yes":"no");
	printf("\t\t%16s(%04X): %16s\n", "IFF_PROMISC	  ",  IFF_PROMISC	 ,( ifrf & IFF_PROMISC	 )?"yes":"no");
	printf("\t\t%16s(%04X): %16s\n", "IFF_NOTRAILERS  ",  IFF_NOTRAILERS  ,( ifrf & IFF_NOTRAILERS  )?"yes":"no");
	printf("\t\t%16s(%04X): %16s\n", "IFF_ALLMULTI	  ",IFF_ALLMULTI	 ,( ifrf & IFF_ALLMULTI	 )?"yes":"no");
	printf("\t\t%16s(%04X): %16s\n", "IFF_MASTER	  ",  IFF_MASTER		 ,( ifrf & IFF_MASTER		 )?"yes":"no");
	printf("\t\t%16s(%04X): %16s\n", "IFF_SLAVE		  ",IFF_SLAVE		 ,( ifrf & IFF_SLAVE		 )?"yes":"no");
	printf("\t\t%16s(%04X): %16s\n", "IFF_MULTICAST	  ",IFF_MULTICAST	 ,( ifrf & IFF_MULTICAST	 )?"yes":"no");
	printf("\t\t%16s(%04X): %16s\n", "IFF_PORTSEL	  ",  IFF_PORTSEL	 ,( ifrf & IFF_PORTSEL	 )?"yes":"no");
	printf("\t\t%16s(%04X): %16s\n", "IFF_AUTOMEDIA	  ",IFF_AUTOMEDIA	 ,( ifrf & IFF_AUTOMEDIA	 )?"yes":"no");
	printf("\t\t%16s(%04X): %16s\n", "IFF_DYNAMIC	  ",  IFF_DYNAMIC	 ,( ifrf & IFF_DYNAMIC	 )?"yes":"no");
	printf("\t\t%16s(%04X): %16s\n", "IFF_LOWER_UP	  ",IFF_LOWER_UP	 ,( ifrf & IFF_LOWER_UP	 )?"yes":"no");
	printf("\t\t%16s(%04X): %16s\n", "IFF_DORMANT	  ",  IFF_DORMANT	 ,( ifrf & IFF_DORMANT	 )?"yes":"no");
	printf("\t\t%16s(%04X): %16s\n", "IFF_ECHO		  ",IFF_ECHO		 ,( ifrf & IFF_ECHO		 )?"yes":"no");

	/*printf("\t\t PRIVATE FLAGS\n");
    printf("\t\tIFF_802_1Q_VLAN    : %s\n",( ifrf & IFF_802_1Q_VLAN    )?"yes":"no");
	printf("\t\tIFF_EBRIDGE        : %s\n",( ifrf & IFF_EBRIDGE         )?"yes":"no");
	printf("\t\tIFF_SLAVE_INACTIVE : %s\n",( ifrf & IFF_SLAVE_INACTIVE  )?"yes":"no");
	printf("\t\tIFF_MASTER_8023AD  : %s\n",( ifrf & IFF_MASTER_8023AD   )?"yes":"no");
	printf("\t\tIFF_MASTER_ALB     : %s\n",( ifrf & IFF_MASTER_ALB     )?"yes":"no");
	printf("\t\tIFF_BONDING        : %s\n",( ifrf & IFF_BONDING        )?"yes":"no");
	printf("\t\tIFF_SLAVE_NEEDARP  : %s\n",( ifrf & IFF_SLAVE_NEEDARP   )?"yes":"no");
	printf("\t\tIFF_ISATAP         : %s\n",( ifrf & IFF_ISATAP         )?"yes":"no");*/

}


void  getIPAddress(struct ifaddrs *res, const char* ifName)
{
    char ipAddress[32]="Unable to get IP Address";
    char* p;
    struct ifaddrs *interfaces = NULL;
    struct ifaddrs *temp_addr = NULL;
    int success = 0;
    // retrieve the current interfaces - returns 0 on success
    success = getifaddrs(&interfaces);
    if (success == 0) {
        // Loop through linked list of interfaces
        temp_addr = interfaces;
        while(temp_addr != NULL) {
          if((temp_addr->ifa_addr->sa_family == AF_INET)&& ((temp_addr->ifa_flags & IFF_LOOPBACK) == 0)) {
                // Check if interface is en0 which is the wifi connection on the iPhone
                if(strcmp(temp_addr->ifa_name, ifName)==0)
                {
                    //ipAddress=inet_ntoa(((struct sockaddr_in*)temp_addr->ifa_addr)->sin_addr);
                    *res =*temp_addr;
                     p = inet_ntoa(((struct sockaddr_in*)temp_addr->ifa_addr)->sin_addr);
                     printf("%s ip : %s\n", temp_addr->ifa_name, p);
                     prnIfFlags(temp_addr->ifa_flags);

                     freeifaddrs(interfaces);
                     return;
               }
            }
            temp_addr = temp_addr->ifa_next;
        }
    }
    // Free memory
    freeifaddrs(interfaces);

}


int getMac(int sock, uint8_t* mac, char* ifName)
{
    struct ifreq ifr;
    struct ifconf ifc;
    struct sockaddr ifraddr;
    char buf[1024];
    int success = 0;

    if (sock == -1) return(-1);

    ifc.ifc_len = sizeof(buf);
    ifc.ifc_buf = buf;
    if (ioctl(sock, SIOCGIFCONF, &ifc) == -1)return(-2);

    struct ifreq* it = ifc.ifc_req;
    const struct ifreq* const end = it + (ifc.ifc_len / sizeof(struct ifreq));

    for (; it != end; ++it)
    {
        strcpy(ifr.ifr_name, it->ifr_name);
        if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0)
        {
            if (! (ifr.ifr_flags & IFF_LOOPBACK))
            { // don't count loopback
                if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0)
                {
                    success = 1;
                    strncpy(ifName, it->ifr_name, 32);
                    memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
                    //return(0);
                    //break;

                    if (ioctl(sock, SIOCGIFDSTADDR, &ifr) == 0)
                    {
                        success = 1;
                        memcpy((void*)&ifraddr, (void*)&ifr.ifr_addr, sizeof(struct sockaddr));
                        return(0);
                        break;
                    }
                }
            }
        }
        else
        {
            return(-3);
        }
    }
    return(-4);
}

inline void mimas_reset(void)
{
    bcm2835_gpio_set(MIMAS_RST);
    bcm2835_delayMicroseconds(10000ull);
    bcm2835_gpio_clr(MIMAS_RST);
    bcm2835_delayMicroseconds(10000ull);
    bcm2835_gpio_set(MIMAS_RST);
}
#include <linux/wireless.h>
int getifs(int sock, node_interfaces_t* ifss)
{
    volatile struct ifreq ifr;
    struct ifconf ifc;
    struct sockaddr ifraddr;
    char buf[1024];
    int success = 0, i;

    struct ifaddrs *addrs,*tmp;
    node_interfaces_detail_t *ifs = &ifss->ifs[0];
    getifaddrs(&addrs);
    tmp = addrs;

    while (tmp)
    {
        if (tmp->ifa_addr && tmp->ifa_addr->sa_family == AF_PACKET)
        {
            ifc.ifc_len = sizeof(buf);
            ifc.ifc_buf = buf;
            strcpy(ifr.ifr_name, tmp->ifa_name);
            if (ioctl(sock, SIOCGIFFLAGS, &ifr) == 0)
            {
                if (! (ifr.ifr_flags & IFF_LOOPBACK))
                { // don't count loopback
                    ifs[success].flags = ifr.ifr_flags;
                    strncpy(ifs[success].if_name, tmp->ifa_name, 32);
                    if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0)
                    {
                        memcpy(ifs[success].mac, ifr.ifr_hwaddr.sa_data, 6);
                        char* m=ifs[success].mac;
                        memset(ifs[success].mac_str, 0, sizeof(ifs[success].mac_str));
                        sprintf(ifs[success].mac_str,"%X:%X:%X:%X:%X:%X", m[0],m[1],m[2], m[3],m[4],m[5]);
                    }
                    if (ioctl(sock, SIOCGIFDSTADDR, &ifr) == 0)
                    {
                        memcpy((void*)&ifs[success].sockaddr, (void*)&ifr.ifr_addr, sizeof(struct sockaddr_in));
                        memset(ifs[success].ip_str, 0, sizeof(ifs[success].ip_str));
                        strcat( ifs[success].ip_str ,inet_ntoa(ifs[success].sockaddr.sin_addr));
                    }
                    //SIOCGIWNAME
                    if (ioctl(sock, SIOCGIWNAME, &ifr) == 0)
                    {
                        volatile int rc;
                        struct iwreq req;
                        char buf[256];
                        strcpy(req.ifr_name,ifs[success].if_name);
                        req.u.essid.pointer = ifs[success].ssid1;
                        req.u.essid.length = sizeof(ifs[success].ssid1);
                        memset(ifs[success].ssid1,0,sizeof(ifs[success].ssid1));
                        ifs[success].isWireless = 1;
                        rc = ioctl(sock, SIOCGIWESSID, &req);
                        if((rc !=0 )||(req.u.essid.length<1))
                        {
                            ifs[success].ssid1[0]='\0';
                            strcat(ifs[success].ssid1, "YES\0");
                        }
                        rc = ioctl(sock, SIOCGIWNWID, &req);
                        rc = ioctl(sock, SIOCGIWPRIV, &req);
                        //SIOCGIWESSID

                        rc = ioctl(sock, SIOCGIWNICKN, &req);

                    }
                    //SIOCGIFINDEX
                    if (ioctl(sock, SIOCGIFINDEX, &ifr) == 0)
                    {
                        ifs[success].ifindex = ifr.ifr_ifindex;
                    }
                    printf("found interface %s\n", ifs[success].if_name);
                    success ++;
                }
            }
        }
        tmp = tmp->ifa_next;
    }
    ifss->curr_if_idx = 0xFF;
    ifss->if_count = success;
    freeifaddrs(addrs);
    int wireless_up=-1;
    if(success>1)
    {
        for(i=0;i<success;i++)
        {
            if(ifss->ifs[i].flags & IFF_UP)
            {
                if(ifss->ifs[i].isWireless == 0)
                {
                    ifss->curr_if_idx = i;
                    break;
                }
            }
            else
            {
                wireless_up = i;
            }
        }
        if((ifss->curr_if_idx == 0xFF)&&(wireless_up>-1))
        {
            ifss->curr_if_idx = wireless_up;
        }
        if(ifss->curr_if_idx == 0xFF)
        {
            return(-1);
        }
    }
    else
    {
        if(success == 0)return(-1);
    }
    return(0);
}

int setIP(char* newIP, int ifIdx)
{
    struct ifreq ifr, ifr_temp;
    int rc;
    if(Node==NULL)Node = anetp->artnode;
    char * name = Node->ifs.ifs[ifIdx].if_name;
    int fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);

    strncpy(ifr.ifr_name, name, IFNAMSIZ);

    ifr.ifr_addr.sa_family = AF_INET;

    memcpy((void*)&ifr_temp, (void*)&ifr, sizeof(struct ifreq));
    rc =  ioctl(fd, SIOCGIFFLAGS, &ifr_temp);
    if(rc)
    {
        perror("Get Old Flags error");
        return(rc);
    }
    prnIfFlags(ifr_temp.ifr_flags);
   /* ifr.ifr_flags &= ~(IFF_UP);
    rc =  ioctl(fd, SIOCSIFFLAGS, &ifr);
    if(rc)
    {
        perror("Set IF Flags error");
        return(rc);
    }
*/
    inet_pton(AF_INET, newIP, ifr.ifr_addr.sa_data + 2);
    rc =  ioctl(fd, SIOCSIFADDR, &ifr);
    if(rc)
    {
        perror("SetIP error");
        return(rc);
    }

    ifr.ifr_flags &= ~(IFF_UP);
    rc =  ioctl(fd, SIOCSIFFLAGS, &ifr);
    if(rc)
    {
        perror("Get Old Flags error");
        return(rc);
    }
     getifs(fd, &Node->ifs);
/*
    inet_pton(AF_INET, "255.255.0.0", ifr.ifr_addr.sa_data + 2);
    ioctl(fd, SIOCSIFNETMASK, &ifr);
*/
    rc = ioctl(fd, SIOCGIFFLAGS, &ifr);
    if(rc)
    {
        perror("SetIP error");
        return(rc);
    }
    strncpy(ifr.ifr_name, name, IFNAMSIZ);
    ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);

    ioctl(fd, SIOCSIFFLAGS, &ifr);
    close(fd);
    return rc;
}
void mimas_all_black(out_def_t* outs)
{
    int i;

    for(i=0;i<MIMAS_STREAM_OUT_CNT;i++)
    {
        memset(&outs[i].mpack, 0, sizeof(mimaspack_t));
        if( MIMAS_STREAM_BM & (BIT32(i)) )
        {
            mimas_store_packet(i,&outs[i].mpack, ( outs[i].mappedLen));
        }
    }
    i = mimas_refresh_start_stream(MIMAS_STREAM_BM,0);
    if(i) prnErr(log_any,"mimas_all_black mimas error %d\n",i);
}

inline mimas_state_t mimas_get_state(void)
{
    mimas_state_t res;
    res.clk_rdy = bcm2835_gpio_lev(MIMAS_CLK_RDY);
    res.sys_rdy = bcm2835_gpio_lev(MIMAS_SYS_RDY);
    res.idle = bcm2835_gpio_lev(MIMAS_IDLE);
    return(res);
}

inline void mimas_prn_state(mimas_state_t *st)
{
    mimas_state_t res;
    if(st == NULL ) res= mimas_get_state();
    else res = *st;
    printf("MIMAS status: clk_rdy %u, sys_rdy = %u, irq = %u\n",  res.clk_rdy, res.sys_rdy, res.idle);
}

int initMimas(void)
{
    int retry = 10;
    uint8_t clk_rdy, sys_ready, irq;
    bcm2835_gpio_fsel(MIMAS_RST, BCM2835_GPIO_FSEL_OUTP);
    bcm2835_gpio_fsel(MIMAS_SYS_RDY, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_fsel(MIMAS_CLK_RDY, BCM2835_GPIO_FSEL_INPT);
    bcm2835_gpio_fsel(MIMAS_IDLE, BCM2835_GPIO_FSEL_INPT);
   // MIMAS_RESET

    bcm2835_gpio_set(MIMAS_RST);
    //bcm2835_delayMicroseconds(10000ull);
    usleep(10000ul);
    bcm2835_gpio_clr(MIMAS_RST);
    //bcm2835_delayMicroseconds(25000ull);
    usleep(30000ul);
    bcm2835_gpio_set(MIMAS_RST);
    usleep(2000ul);

    do
    {
        clk_rdy = bcm2835_gpio_lev(MIMAS_CLK_RDY);
        sys_ready = bcm2835_gpio_lev(MIMAS_SYS_RDY);
        irq  =  bcm2835_gpio_lev(MIMAS_IDLE);
        uint32_t pads=bcm2835_gpio_pad(BCM2835_PADS_GPIO_0_27);
        printf("clk_rdy %u, sys_rdy = %u, irq = %u, pads = %X\n", clk_rdy, sys_ready, irq, pads);
/*
        pads = (BCM2835_PAD_PASSWRD | BCM2835_PAD_DRIVE_16mA) ;
        bcm2835_gpio_set_pad(BCM2835_PADS_GPIO_0_27, pads);
        printf("setting pads to %X\t", pads);
        pads=bcm2835_gpio_pad(BCM2835_PADS_GPIO_0_27);
        printf("new pads val  %X\n", pads);
        */
        if((clk_rdy==1) && (sys_ready == 1) && (irq == 0))return(0);
        bcm2835_delayMicroseconds(1000000ull);

    }while(retry--);
    return(-1);
}

static void* mimasWorker(void* d)
{

}

int initMimasIntf(void* d)
{
    int res = initSPI();
    if(res!=0)return(res);
    do
    {
        res = initMimas();
        if(res!=0) usleep(100000); // 100milisec
    }while(res!=0);

    return(0);
}

int socketStart(node_t* n, uint16_t portno)
{
    int rc;
    node_interfaces_t* ifs = &n->ifs;
    n->sockfd = socket_init(NULL);
    if(n->sockfd<0)return(-1);
    if(0 == getifs(n->sockfd, ifs))
    {
        rc = sock_bind(n->sockfd, ifs->ifs[ifs->curr_if_idx].if_name, NULL/*&ifs->ifs[ifs->curr_if_idx].sockaddr.sin_addr.s_addr*/, portno);
        if(rc != 0)
        {
            perror("bind failed");
        }
        else
        {
            printf("Sock %d at port %u bound to %s\n", n->sockfd, portno, ifs->ifs[ifs->curr_if_idx].if_name);
        }
    }
    return(0);
}

void NodeInit(app_node_t* an, uint8_t maxUniCount, addressing_t start_uni_addr)
{
    if(an == NULL)
    {
        printf("Failed to init Node, null obj!\n");
        return;
    }
    node_t* n = an->artnode;
    Node = n;
    n->current_if_idx = n->ifs.curr_if_idx;
    node_interfaces_detail_t *cIfDet = &n->ifs.ifs[n->ifs.curr_if_idx];
    //n->art_start_uni =start_uni_addr;
    //n->universe_count = maxUniCount;
    snprintf(n->longName,63,"%s pixel Controler by Sagi. Node romName %s-%u-%X%X\0\0",NODE_NAME_DEF, NODE_NAME_DEF,NODE_VERSION, cIfDet->mac[4], cIfDet->mac[5]);
    snprintf(n->nodeName,16,"%s-%u-%X%X\0",NODE_NAME_DEF,NODE_VERSION, cIfDet->mac[4], cIfDet->mac[5]);
    strcpy(n->userName, n->nodeName);
}

int tmr_create(uint32_t *timerid )
{
    struct sigevent sigev;
    memset(&sigev,0,sizeof(struct sigevent));
    sigev.sigev_notify = SIGEV_NONE;
    int rc = timer_create(CLOCK_MONOTONIC, &sigev, (timer_t*)timerid);
    if(rc)
    {
        perror("Create Timer error");
    }
    return(rc);

}
//pthread_sigqueue


//#define ALL_EVENTS
//#define EVENT_DIFFS
void* one_sec(void* dat)
{
    task_cfg_t *tcfg = (task_cfg_t*)dat;
    post_box_t*     pb = (post_box_t*)tcfg->iniData;
    threadConfig(tcfg, log_evnt);
    rq_head_t*      evrq_head = &pb->rq;
    trace_msg_t*    evrq = evrq_head->evq;
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
    uint64_t    tries,blocks,hits;
    while(1)
    {
        /*getSpinStats(&tries,&blocks,&hits);
        prnFinf(log_trace,"Hits:%llu, Tries:%llu, Blocks:%llu\n", hits,tries,blocks);*/
        clock_gettime(CLOCK_REALTIME, &sleep_for[0]);
        res = get_taces(evrq_head, &idxs[0], 64);
        if(res>0)
        {
            print+=res;
            i=0;
            len =0;
            for(i=0;i<res;i++)
            {
                trs[i] = evrq[idxs[i]];
            }

            free_traces(evrq_head, i);
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

inline long nsec_diff(struct timespec *now, struct timespec *be4)
{
    struct timespec diff;
    diff.tv_nsec = now->tv_nsec - be4->tv_nsec;
    diff.tv_sec = now->tv_sec - be4->tv_sec;
    if(diff.tv_nsec < 0l)
    {
        diff.tv_sec--;
        diff.tv_nsec  = 1000000000l + diff.tv_nsec;
    }
    return( ((long)(diff.tv_sec)*1000000000l) + diff.tv_nsec );
}

void querySys(void)
{
    volatile long res;
    res = sysconf(_SC_PHYS_PAGES); prnFinf(log_any,"_SC_PHYS_PAGES : %d\n", res);
    res = sysconf(_SC_AVPHYS_PAGES); prnFinf(log_any,"_SC_AVPHYS_PAGES : %d\n", res);
    res = sysconf(_SC_NPROCESSORS_CONF); prnFinf(log_any,"_SC_NPROCESSORS_CONF : %d\n", res);
    res = sysconf(_SC_NPROCESSORS_ONLN); prnFinf(log_any,"_SC_NPROCESSORS_ONLN : %d\n", res);
    res = sysconf(_SC_PAGESIZE); prnFinf(log_any,"_SC_PAGESIZE : %d\n", res);

    res = (long)get_nprocs();prnFinf(log_any,"get_nprocs : %d\n", res);
    res = (long)get_nprocs_conf();prnFinf(log_any,"get_nprocs_conf : %d\n", res);
}

void threadConfig(task_cfg_t* tcfg, log_src_e lsrc)
{
    int ret;
    cpu_set_t cpuset;

    ret = pthread_setname_np(tcfg->thread, tcfg->name);
    if(ret)
    {
        prnErr(lsrc,"%s: thread rename failed, ret %d\n",tcfg->name, ret );
    }

    ret = pthread_getaffinity_np(tcfg->thread , sizeof(cpu_set_t), &cpuset);
    if (ret != 0)
    {
        prnErr(lsrc,"Failed to get affinity for %s, ret %d\n", tcfg->name, ret);
    }
    if( cpuset.__bits[0] != tcfg->affinity )
    {
        cpuset.__bits[0] = tcfg->affinity;
        //sched_setaffinity()
        ret = pthread_setaffinity_np(tcfg->thread, sizeof(cpu_set_t), &cpuset);

        if(ret != 0)
        {
            prnErr(lsrc,"Failed to set affinity for %s, ret %d\n", tcfg->name, ret);
        }
        ret = pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

        if (ret != 0)
        {
            prnErr(lsrc,"Failed to get affinity for %s, ret %d\n", tcfg->name, ret);
        }
        else
        {
            if(tcfg->affinity == cpuset.__bits[0])
            {
                prnFinf(lsrc,"%s: Affinity is set to 0x%0lx\n", tcfg->name, cpuset.__bits[0]);
            }
            else
            {
                prnErr(lsrc,"%s: Affinity missmatch: requested: 0x%X, has 0x%X\n", tcfg->name, tcfg->affinity ,cpuset.__bits[0]);
            }
        }
    }
   /* struct sched_param sp;
    ret = sched_getparam(tcfg->tid, &sp);
    prnFinf(log_any,"Min nice:%d, Max nice:%d\n", sched_get_priority_min(SCHED_OTHER), sched_get_priority_max(SCHED_OTHER));
    ret = pthread_setschedprio( tcfg->thread, tcfg->nice );*/
    ret = setpriority(PRIO_PROCESS, tcfg->tid, tcfg->nice);
    prnFinf(lsrc,"%s set_nice to (%i) returns %d, errno %d\n",tcfg->name,tcfg->nice, ret, errno);

}


void print_trace(void)
{
  void *array[10];
  size_t size;
  char **strings;
  size_t i;

  size = backtrace(array, 10);
  strings = backtrace_symbols(array, size);

  printf ("Obtained %zd stack frames, executing thread %d.\n", size, gettid);

  for (i = 0; i < size; i++)
     printf ("%s\n", strings[i]);

  free (strings);
}

void print_trace_gdb()
{
    char pid_buf[256] ={0};
    sprintf(pid_buf, "%d", getpid());
    char name_buf[4096];
    name_buf[readlink("/proc/self/exe", name_buf, 4095)]=0;

    int child_pid = fork();
    if (!child_pid) {
        dup2(2,1); // redirect output to stderr
       // printf("\n(1)stack trace for %s pid=%s\n",name_buf, pid_buf);
        //execlp("gdb", "gdb", "--batch", "-n", "-ex", "thread", "-ex", "bt fu", name_buf, pid_buf, NULL);

        //printf("\n(2)stack trace for %s pid=%s\n",name_buf, pid_buf);
       // execlp("gdb", "gdb", "--batch", "-n",  "-ex", "bt fu", name_buf, pid_buf, NULL);

        printf("\n(3)stack trace for %s pid=%s\n",name_buf, pid_buf);
        execlp("gdb", "gdb", "--batch","-ex", "bt fu", name_buf, pid_buf, NULL);
        //if gdb failed to start
        abort();
    } else {
        waitpid(child_pid,NULL,0);
        printf("Done backtreace=======================\n");
    }
 }
void fault_handler(int signo, siginfo_t *info, void *extra)
{

	printf(" =========== Signal %d received =========== \n", signo);
	printf("siginfo address=%x\n",info->si_addr);

	ucontext_t *p=(ucontext_t *)extra;
	int val = p->uc_mcontext.arm_pc;
	printf("address = %x\n\nBACKTRACE:\n",val);
	printf("\n=====================================================\n");
	print_trace();
	printf("\n=====================================================\n");
	print_trace_gdb();
	printf("\n=====================================================\n");
	abort();
}

void setHandler(void (*handler)(int,siginfo_t *,void *))
{
    printf("Setting up SIG_HAN\n");
	struct sigaction action;
	memset(&action, 0, sizeof(struct sigaction));
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
	//SIGALRM
	if (sigaction(SIGALRM, &action, NULL) == -1) {
		perror("sigbus: sigaction");
		_exit(1);
	}
    printf("Done setting up SIG_HAN\n");
}
