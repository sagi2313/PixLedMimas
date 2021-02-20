#ifndef MIMAS_CFG_H_INCLUDED
#define MIMAS_CFG_H_INCLUDED

#define MIMAS_DEV_CNT               (1)

/* PIN connections */
#define MIMAS_RST       18 /*GPIO18 */
#define MIMAS_CLK_RDY   17 /*GPIO17 */
#define MIMAS_SYS_ERR   27 /*GPIO27 */ /* Hi on mimas error, low when ready */
#define MIMAS_SYS_BUSY      22 /*GPIO22 */ /*TOGGLES High when busy, low when ready for cmd */


/* Streamer definitions */
#define MIMAS_STREAM_OUT_CNT        (12)
#define MIMAS_STREAM_BM             ((uint32_t)(BIT32(MIMAS_STREAM_OUT_CNT) -1u) )
#define MIMAS_PROTO_BM              ((uint32_t)(BIT32( 2u * MIMAS_STREAM_OUT_CNT) -1u) )
/* PWM definitions */
#define MIMAS_PWM_GROUP_CNT         (2)
#define MIMAS_PWM_OUT_PER_GRP_CNT   (8)
#define MIMAS_PWM_OUT_CNT           (MIMAS_PWM_OUT_PER_GRP_CNT * MIMAS_PWM_GROUP_CNT)
#define MIMAS_HDR_LEN               (6u)

#define PWM_GRP_A       1
#define PWM_GRP_B       2
#define PWM_GRP_ALL     (PWM_GRP_A | PWM_GRP_B)

#define STRM_WS          0x0
#define STRM_SPI         0x1
#define STRM_DMX         0x2
#define STRM_CLR         0x3

#define SET_PROTO(R, P, I)  do{ R&=~(3<< (2*I) ); R|=(P<< (2*I));}while(0);
typedef union
{
    uint8_t raw_state:3;
    uint8_t raw_dc:5;
    struct    {
    uint8_t clk_rdy:1;
    uint8_t sys_err:1;
    uint8_t sys_idle:1;
    uint8_t dc:5;
    };
}mimas_state_t;
#define MIMAS_RDY (5)
#endif // MIMAS_CFG_H_INCLUDED
