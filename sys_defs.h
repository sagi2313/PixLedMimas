#ifndef SYS_DEFS_H_INCLUDED
#define SYS_DEFS_H_INCLUDED

#define     MAX_UNIS            64
#define     GLOBAL_OUTPUTS_MAX  24
#define     UNI_PER_OUT         5

#define NODE_VERSION ((uint16_t)0x0001)

#define NODE_NAME_DEF   ((const char*)"PixLed\0")

#define MAX_PIX_PER_UNI     (170)
#define DEF_PIX_PER_UNI     (150)
#define DEF_CHAN_PER_PIX    (3)


#define MILIS   (1000000l)
#define CONS_TOV (500l * MILIS)

#define MMLEN_MMAX  80


#ifdef      LL_BUFFER
#define     RQ_DEPTH    (8u * (MIMAS_STREAM_OUT_CNT * UNI_PER_OUT))
#else
#define     RQ_DEPTH    (512u)
#endif

#define EV_Q_DEPTH      8192
#define PWM_Q_DEPTH     128
#define PIX_Q_DEPTH     1024

#define MAX_TASK_CNT       6

#ifndef MAX
#define MAX(a,b)  (a<b?b:a)
#endif
#ifndef MIN
#define MIN(a,b)  (a>b?b:a)
#endif

#endif // SYS_DEFS_H_INCLUDED
