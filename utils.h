#ifndef UTILS_H_INCLUDED
#define UTILS_H_INCLUDED
#include "type_defs.h"
#include "mimas_cfg.h"
#include "protocolCommon.h"
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#define PROD_PRIO_NICE (-12)
#define CONS_PRIO_NICE (-18)
#define PIXHAN_PRIO     (-15)

#define MIMAS_RESET do{\
    bcm2835_gpio_set(MIMAS_RST); \
    bcm2835_delayMicroseconds(10000ull);\
    bcm2835_gpio_clr(MIMAS_RST); \
    bcm2835_delayMicroseconds(10000ull); \
    bcm2835_gpio_set(MIMAS_RST);}while(0);


typedef enum
{
    log_dbg,
    log_info,
    log_finf,
    log_err,

}log_lvl_e;

typedef enum
{
    log_any,
    log_prod,
    log_con,
    log_trace,
    log_evnt,
    log_any_han,
    log_pix,
    log_pwm,
    log_dmx,
    log_ll,
    log_src_max
}log_src_e;


extern pthread_spinlock_t  prnlock;
extern uint32_t  LogMask;
extern const char *ll_str[];
extern const char *ls_str[];
extern log_lvl_e LogLvl[];

#define prnLock pthread_spin_lock(&prnlock)
#define prnUnlock pthread_spin_unlock(&prnlock)
#define prn(LL, LS,  ...)  \
do{ \
        if(  ((LL>=LogLvl[(int)LS] ) || (LL==log_finf))  && ( BIT32(LS) & LogMask) ) { \
        /*printf("called %s %d\n",__FILE__,__LINE__);*/ \
        prnLock; \
        printf("%s(%4s): ",ll_str[(int)LL],ls_str[(int)LS]); \
        printf( __VA_ARGS__ ); \
        prnUnlock; \
        } \
        } while(0);

#define  prnDbg( LS,  ...) prn(log_dbg, LS,  __VA_ARGS__)
#define  prnInf( LS,  ...) prn(log_info, LS,  __VA_ARGS__)
#define  prnFinf( LS,  ...) prn(log_finf, LS,  __VA_ARGS__)
#define  prnErr( LS,  ...) prn(log_err, LS,  __VA_ARGS__)

#define DEF_LOG_LVL log_err
#define PWM_SLEEP_TM 100000u
long nsec_diff(struct timespec *now, struct timespec *be4);

void initLogLevels(log_lvl_e lvl);
log_lvl_e getLogLevel(log_src_e src);
void setLogLevel(log_src_e src, log_lvl_e lvl);

void
print_trace (void);
void print_trace_gdb();
struct rtnl_handle
{
    int fd;
    struct sockaddr_nl local;
    struct sockaddr_nl peer;
    __u32 seq;
    __u32 dump;
};

typedef struct
{
    __u8 family;
    __u8 bytelen;
    __s16 bitlen;
    __u32 flags;
    __u32 data[8];
} inet_prefix;


int show_socket_error_reason(int socket);
int initSPI(void);
int mimas_start_stream(uint16_t start_bm, uint16_t proto_bm);
int mimas_store_packet(int chan, uint8_t* data, int len);
int mimas_send_packet(int chan, uint8_t* data, int len);
int mimas_refresh_start_stream(uint16_t start_bm, uint16_t proto_bm);
int mimas_store_pwm_val(uint8_t grp, uint8_t chan, uint16_t* val, uint8_t cnt);
int mimas_store_pwm_period(uint8_t grp, uint16_t val);
int mimas_store_pwm_chCntrol(uint8_t grp, uint8_t chan, uint8_t* enabled, uint8_t cnt);
int mimas_store_pwm_gCntrol(uint8_t grp, uint8_t enabled);
int mimas_store_pwm_div(uint8_t grp, uint8_t val);

void mapColor(uint8_t *src, out_def_t *oout, int sUni);
int setSockTimout(int sock, int ms);
int check_wireless(const char* ifname);
void get_ifs(void);
int socket_init(node_interfaces_detail_t*);
int sock_bind(int sockfd, const char* ifName, const  in_addr_t* bindIP, uint16_t portno);
int altBind(int sockfd);
void getInterfaces(void);
void mimas_all_black(out_def_t*);
mimas_state_t mimas_get_state(void);
void mimas_prn_state(mimas_state_t*);
void mimas_reset(void);
void getIPAddress(struct ifaddrs *res, const char* ifName);
int getMac(int sock, uint8_t* mac, char* ifName);
int getifs(int sock, node_interfaces_t* ifss);
int add_IP_Address(char * IP);
int webServStart(void);
void InitOuts(void);
int initMimas(void);
void NodeInit(app_node_t* n, uint8_t maxUniCount, addressing_t start_uni_addr);
int socketStart(node_t* n, uint16_t portno);
int setIP(char* newIP, int ifIdx);
int socket_set_blocking(const int sockfd, int on);
int tmr_create(uint32_t *timerid );
void* one_sec(void* d);
#endif // UTILS_H_INCLUDED
