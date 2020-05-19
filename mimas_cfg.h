#ifndef MIMAS_CFG_H_INCLUDED
#define MIMAS_CFG_H_INCLUDED

#define MIMAS_DEV_CNT               (1)

/* PIN connections */
#define MIMAS_RST       18 /*GPIO18 */
#define MIMAS_CLK_RDY   17 /*GPIO17 */
#define MIMAS_SYS_RDY   27 /*GPIO27 */
#define MIMAS_IDLE      22 /*GPIO22 */

/* Streamer definitions */
#define MIMAS_STREAM_OUT_CNT        (12)
#define MIMAS_STREAM_BM             ((uint32_t)(BIT32(MIMAS_STREAM_OUT_CNT) -1u) )

/* PWM definitions */
#define MIMAS_PWM_GROUP_CNT         (2)
#define MIMAS_PWM_OUT_PER_GRP_CNT   (8)
#define MIMAS_PWM_OUT_CNT           (MIMAS_PWM_OUT_PER_GRP_CNT * MIMAS_PWM_GROUP_CNT)

#define PWM_GRP_A       1
#define PWM_GRP_B       2
#define PWM_GRP_ALL     (PWM_GRP_A | PWM_GRP_B)
typedef struct
{
    uint8_t clk_rdy:1;
    uint8_t sys_rdy:1;
    uint8_t idle:1;
    uint8_t dc:5;
}mimas_state_t;
#endif // MIMAS_CFG_H_INCLUDED
