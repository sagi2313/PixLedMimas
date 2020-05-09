#define PWM_VAL_ST      0x41
#define PWM_CH_CTRL_ST  0x40
#define PWM_PER_ST      0x21
#define PWM_DIV_ST      0x22
#define PWM_G_CTRL_ST   0x20
#define STREAM_START    0x80
#define STREAM_PKT_SEND 0x08
#define PWM_GRP_A       1
#define PWM_GRP_B       2
#define PWM_GRP_ALL     (PWM_GRP_A | PWM_GRP_B)
#pragma pack(1)
typedef struct
{
    uint32_t opCode:8;
    //union{
    uint32_t  chan:12;
   // uint32_t  start:12;
   // };
   // union{
    uint32_t len:12;
   // uint32_t proto:12;
   // };
}mimas_cmd_stream_hdr_t;

typedef struct
{
    uint32_t opCode:8;
    uint32_t chan:8;
    uint32_t group:8;
    uint32_t cnt:8;
}mimas_cmd_pwm_hdr_t;

typedef union{
mimas_cmd_stream_hdr_t  strm;
mimas_cmd_pwm_hdr_t     pwm;
}mimas_cmd_hdr_t;
