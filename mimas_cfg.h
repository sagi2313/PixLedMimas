#ifndef MIMAS_CFG_H_INCLUDED
#define MIMAS_CFG_H_INCLUDED

#define MIMAS_RST       18 /*GPIO18 */
#define MIMAS_CLK_RDY   17 /*GPIO17 */
#define MIMAS_SYS_RDY   27 /*GPIO27 */
#define MIMAS_IDLE      22 /*GPIO22 */
#define MIMAS_STREAM_OUT_CNT    12
#define MIMAS_PWM_OUT_CNT       12
#define MIMAS_STREAM_BM         ( BIT32(MIMAS_STREAM_OUT_CNT) -1u )
#define MIMAS_DEV_CNT   1
#endif // MIMAS_CFG_H_INCLUDED
