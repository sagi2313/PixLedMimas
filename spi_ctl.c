/*
 * SPI testing utility (using spidev driver)
 *
 * Copyright (c) 2007  MontaVista Software, Inc.
 * Copyright (c) 2007  Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * Cross-compile with cross-gcc -I/path/to/cross-kernel/include
 */

#include <stdint-gcc.h>

#include <time.h>
//#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>
#include <string.h>
#include "rq.h"
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#include <bcm2835.h>
#include <errno.h>
#define DATABYTES (3 * 750)
#include "mimas_cmd_defs.h"
#include "mimas_cfg.h"
#include "utils.h"
#include <byteswap.h>
#include <pthread.h>
#include <stdatomic.h>
static int fd = -1;
int bits=8;

static const char *device = "/dev/spidev0.0";
static uint8_t mode;
static uint32_t speed = 80000000u;
static uint16_t delay;
//uint8_t mimas[8][DATABYTES + 4];
uint8_t mimas_pwm[20];
struct spi_ioc_transfer tr[13];
struct spi_ioc_transfer tr_pwm;
static uint8_t start_stream_header[MIMAS_HDR_LEN];
uint_fast64_t spin_trycolission=0ul;
uint_fast64_t spin_blocks=0ul;
uint_fast64_t spin_hit=0ul;
uint_fast64_t unlock_sleep;
static	pthread_spinlock_t  spilock;

//#define MIMAS_TIME_STAT

void getSpinStats(uint64_t* tryCol, uint64_t* blocked, uint64_t* hits)
{
    *tryCol =spin_trycolission;
    *blocked = spin_blocks;
    *hits = spin_hit;

}

inline static int spi_lock(void)
{
    int_fast32_t rc, c;
#ifdef MIMAS_TIME_STAT
    struct timespec ts[2];
    clock_gettime(CLOCK_REALTIME, &ts[0]);
#endif
    c=0;
    spin_hit++;
    do{
        if(++c>9999)break;
        rc = pthread_spin_trylock(&spilock);
    }while(rc);
    if(c>1)spin_trycolission+=c;

    if(rc)
    {
        spin_blocks++;
       // printf("spin %llu, block %llu, hits %llu\n",spin_trycolission, spin_blocks, spin_hit);
        rc = pthread_spin_lock(&spilock);
        if(rc)
        {
#ifdef MIMAS_TIME_STAT
            clock_gettime(CLOCK_REALTIME, &ts[1]);
            prnErr(log_mim, "Spin failed after %ld nSec\n", nsec_diff(&ts[1], &ts[0]));
#else
            prnErr(log_mim, "Spin failed \n");
#endif
            return(-1);
        }
    }
    char tsbuf[80];
    time_t now;
    struct tm tsepo;
    mimas_state_t mSt;
    mSt = mimas_get_state();
    c = 0;
    while( mSt.idle == 1 )
    {
        do
        {
            usleep((useconds_t)1);
            if((c  &  (BIT32(128u) -1u)) == (BIT32(128u) -1u) )mimas_prn_state(&mSt); // printf every 128 checks
            mSt = mimas_get_state();
            //mimas_prn_state(&mSt);
        }while((mSt.idle==1) && (++c<250000));
        if( mSt.idle == 1 )
        {
            MIMAS_RESET
            time(&now);
            tsepo = *localtime(&now);
            strftime(tsbuf, sizeof(tsbuf), "%T", &tsepo);
            prnErr(log_mim, "WARNING(%s): mimas reset in line:%d at %s\n", __FUNCTION__, __LINE__, tsbuf);
            usleep((useconds_t)1500);
            c=0;
        }
    }

    int loops = 0;
    volatile uint_fast64_t delayns;
    while( mSt.sys_rdy == 0 )
    {
        delayns = 0llu;
        if(++loops > 5000) // wait upto 25 mSec (normally busy is about 350 uSec on a mimas_store_packet command)
        {
            prnErr(log_mim,"Mimas stuck badly!\n");
            mimas_prn_state(&mSt);
            time(&now);
            tsepo = *localtime(&now);
            strftime(tsbuf, sizeof(tsbuf), "%T", &tsepo);
            MIMAS_RESET
            prnErr(log_mim,"WARNING(%s): mimas reset in line:%d at %s, state after reset:\n", __FUNCTION__,__LINE__, tsbuf);
            usleep((useconds_t)1500);
            mSt =mimas_get_state();
            mimas_prn_state(&mSt);
            rc = pthread_spin_unlock(&spilock);
            if(rc)
            {
                perror("failed to unlock spilock");
            }
#ifdef MIMAS_TIME_STAT
            clock_gettime(CLOCK_REALTIME, &ts[1]);
            prnErr(log_mim, "Spin failed2 after %ld nSec\n", nsec_diff(&ts[1], &ts[0]));
#else
            prnErr(log_mim, "Spin failed stage2\n");
#endif
            return(-100);
        }
        //usleep((useconds_t )1);
        while(++delayns < 10000llu);
        mSt =mimas_get_state();
     }
#ifdef MIMAS_TIME_STAT
     clock_gettime(CLOCK_REALTIME, &ts[1]);
     prnFinf(log_mim, "Spin taken by %d after %ld nSec, loops = %d, c = %d\n",gettid, nsec_diff(&ts[1], &ts[0]), loops, c);
#endif
     return 0;
}

inline static void spi_unlock(void)
{
    int rc = pthread_spin_unlock(&spilock);

    if(rc)
    {
        perror("failed to leave spilock");
        return;
    }
#ifdef MIMAS_TIME_STAT
    prnFinf(log_mim, "Spin returned by %d\n",gettid);
#endif
}

static void pabort(const char *s)
{
	perror(s);
	//abort();
}


/*
pwm API "mimas_store_pwm_period":
    Sets pwm period for a group of pwms
    grp is the group select, and it works as a bitmap. only bits 0 & 1 matter. set bit0 to true for access in first pwm group
    and bit1 for access to second pwm group val is a 16bit value to be assigned to groups pwm period
*/
int mimas_store_pwm_period(uint8_t grp, uint16_t val)
{
    if(fd == -1)return(-1);
    if( (grp&3) == 0)return(-13);
    mimas_pwm[0] = PWM_PER_ST;
    mimas_pwm[1] = 0;
    mimas_pwm[2] = grp & 3;
    mimas_pwm[3] = 0;
    mimas_pwm[4] = 0;
    mimas_pwm[5] = 0;
    *(uint16_t*)&mimas_pwm[6] = val;
    tr_pwm.len = MIMAS_HDR_LEN + 2u;
    if(spi_lock()!=0) return(-1000);
    int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr_pwm);
    /*unlock_sleep = ((uint64_t)tr_pwm.len/8llu);
    if(unlock_sleep==0llu)unlock_sleep=1llu;
    bcm2835_delayMicroseconds(unlock_sleep);*/
    spi_unlock();
    if (ret < 1)
    {
        pabort("can't send spi message");
        return (-5);
    }
    return(0);
}

/*
pwm API "mimas_store_pwm_div":
Sets pwm frequency divider for a group of pwms
    grp is the group select, and it works as a bitmap. only bits 0 & 1 matter. set bit0 to true for access in first pwm group
    and bit1 for access to second pwm group val is a 8bit value to be assigned to groups pwm frequency divider
*/
int mimas_store_pwm_div(uint8_t grp, uint8_t val)
{
    if(fd == -1)return(-1);
    if(grp>3)return(-13);
    mimas_pwm[0] = PWM_DIV_ST;
    mimas_pwm[1] = 0;
    mimas_pwm[2] = grp & 3;
    mimas_pwm[3] = 0;
    mimas_pwm[4] = 0;
    mimas_pwm[5] = 0;
    mimas_pwm[6] = val;
    tr_pwm.len = MIMAS_HDR_LEN + 1u;
    if(spi_lock()!=0) return(-1000);
    int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr_pwm);

    bcm2835_delayMicroseconds(1llu);
    spi_unlock();
    if (ret < 1)
    {
        pabort("can't send spi message");
        return (-5);
    }
    return(0);
}


/*
pwm API "mimas_store_pwm_gCntrol":
Sets pwm group enable
    grp is the group select, and it works as a bitmap. only bits 0 & 1 matter. set bit0 to true for access in first pwm group
    and bit1 for access to second pwm group. enabled controls enable flag for selected groups iin same bimap fashion
*/
int mimas_store_pwm_gCntrol(uint8_t grp, uint8_t enabled)
{
    if(fd == -1)return(-1);
    if(grp > 3)return(-13);
    mimas_pwm[0] = PWM_G_CTRL_ST;
    mimas_pwm[1] = 0;
    mimas_pwm[2] = grp & 3;
    mimas_pwm[3] = 0;
    mimas_pwm[4] = 0;
    mimas_pwm[5] = 0;
    mimas_pwm[6] = enabled & 1;
    tr_pwm.len = MIMAS_HDR_LEN + 1u;
    if(spi_lock()!=0) return(-1000);
    int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr_pwm);

   /* bcm2835_delayMicroseconds(1llu);*/
    spi_unlock();
    if (ret < 1)
    {
        pabort("can't send spi message");
        return (-5);
    }
    return(0);
}

/*
pwm API "mimas_store_pwm_val":
Sets pwm values
    grp is the group select, and it works as a bitmap. only bits 0 & 1 matter. set bit0 to true for access in first pwm group
        and bit1 for access to second pwm group
    chan is the channel select, and it works as channel id. Valid is 0 through 7.
    val is a pointer to array of 16bit values to be assigned to channels starting from "chan" up to chan+cnt
    cnt tells how many channels to set starting from chan. this means that chan + cnt must be up to 8
*/

int mimas_store_pwm_val( uint16_t chanBM, uint16_t* val)
{
    //if(chan>7)return(-3);
    //if(grp>3)return(-13);
    //if(cnt ==0) return(-4);
    if(fd == -1)return(-1);
    int i, ret;

    mimas_pwm[0] = PWM_VAL_ST;
    *(uint16_t*)&mimas_pwm[1] = chanBM;
    mimas_pwm[3] = 32;
    uint16_t* p = &mimas_pwm[6];
    for(i=0;i<16;i++)
    {
        *p = *val;
        p++;
        val++;
    }
    tr_pwm.len = MIMAS_HDR_LEN + 32u;
    if(spi_lock()!=0) return(-1000);
    ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr_pwm);
    /*
    unlock_sleep = ((uint64_t)tr_pwm.len/8llu);
    if(unlock_sleep==0llu)unlock_sleep=1llu;
    bcm2835_delayMicroseconds(unlock_sleep);*/
    spi_unlock();
    if (ret < 1)
    {
        pabort("can't send spi message");
        return (-5);
    }
    return(0);
}
/*
pwm API "mimas_store_pwm_chCntrol":
Sets pwm enable and invert
    grp is the group select, and it works as a bitmap. only bits 0 & 1 matter. set bit0 to true for access in first pwm group
    and bit1 for access to second pwm group chan is the channel select, and it works as channel id. Valid is 0 through 7.
    enabled is a pointer to array of 8bit values to be assigned to channels starting from "chan" up to chan+cnt.
    in each byte bit0 controls enable flag and bit1 controls invert. Bit0  = 1 -> enable, Bit1 = 1 -> invert
    cnt tells how many channels to set starting from chan. this means that chan + cnt must be up to 8
*/
int mimas_store_pwm_chCntrol(uint16_t chanBm, uint8_t* enabled)
{
    //if(chan>7)return(-3);
    //if(grp>3)return(-13);
    //if((cnt ==0)||(cnt>8)) return(-4);
    if(fd == -1)return(-1);
    int i, ret, j;
    mimas_pwm[0] = PWM_CH_CTRL_ST;
    mimas_pwm[1] = chanBm & 0xFF;
    mimas_pwm[2] = (chanBm>>8)&0xFF;
    mimas_pwm[3] = 16;
    mimas_pwm[4] = 0;
    mimas_pwm[5] = 0;
    uint8_t* p = &mimas_pwm[6];
    for(i=0;i<16;i++)
    {
        *p = *enabled;
        p++;
        enabled++;
    }
    tr_pwm.len = MIMAS_HDR_LEN + 16u;
    if(spi_lock()!=0) return(-1000);
    ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr_pwm);
    /*unlock_sleep = ((uint64_t)tr_pwm.len/8llu);
    if(unlock_sleep==0llu)unlock_sleep=1llu;
    bcm2835_delayMicroseconds(unlock_sleep);*/
    spi_unlock();
    if (ret < 1)
    {
        pabort("can't send spi message");
        return (-5);
    }
    return(0);
}

/*prepare and save data for messages to be sent to mimas in local buffer 'tr
    chan is numeric ID of channel starting at 0.
*/
int mimas_store_packet(int chan, uint8_t* data, int len, uint8_t cfg)
{

    if(fd == -1)return(-1);
    if(chan > (MIMAS_STREAM_OUT_CNT-1))return(-3);
    int ret;
    uint16_t ch_bm =  1 << chan;
    data[0] = STREAM_PKT_SEND;
    data[1] = (ch_bm & 0xFF);
    data[2] = ((ch_bm>>8) & 0x0F);
    data[3] = (len &0xFF);
    data[4] = (len>>8) & 0xFF;
    data[5] = cfg;
    tr[chan].tx_buf = (unsigned long)(void*)data;
    tr[chan].len = MIMAS_HDR_LEN + len;
    return(0);
}


int mimas_store_many_packets( uint16_t chan, uint8_t* data, int len, uint8_t cfg)
{

    if(fd == -1)return(-1);

    int ret, i;
    chan&= BIT16(MIMAS_STREAM_OUT_CNT) -1u;
    if(chan == 0)return(-3);
    data[0] = STREAM_PKT_SEND;
    data[1] = (chan & 0xFF);
    data[2] = ((chan>>8) & 0x0F);
    data[3] = (len &0xFF);
    data[4] = (len>>8) & 0xFF;
    data[5] = cfg;
    for(i=0;i<MIMAS_STREAM_OUT_CNT; i++)
    {
        if(BIT16(i) & chan){
        tr[i].tx_buf = (unsigned long)(void*)data;
        tr[i].len = MIMAS_HDR_LEN + len;
        }
    }

    return(0);
}


int mimas_start_stream(uint16_t start_bm, uint16_t proto_bm)
{
    if(start_bm == 0 ) return(-3);
    if(start_bm  > MIMAS_STREAM_BM) return(-4);
    if(fd == -1)return(-1);

    int ret;
    start_stream_header[1]= (start_bm & 0xFF);
    start_stream_header[2]= ((start_bm >> 4) & 0xF0) | (proto_bm >> 8);
    start_stream_header[3]= proto_bm & 0xFF;
    //printf("MIMAS start %X\n", start_bm);
    if(spi_lock()!=0) return(-1000);
    ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr[8]);

    /*bcm2835_delayMicroseconds(1llu);*/
    spi_unlock();
    if (ret < 1){
    pabort("can't send spi message");
    return (-2);
    }

    return(0);
}

int mimas_refresh_start_stream(uint16_t start_bm, uint32_t proto_bm)
{
    if(fd == -1)return(-1);
#ifdef MIMAS_TIME_STAT
    struct timespec ts[2];
    clock_gettime(CLOCK_REALTIME, &ts[0]);
#endif
    int i, ret;

    if(start_bm == 0 ) return(-3);
    start_bm &= MIMAS_STREAM_BM;
    proto_bm &= MIMAS_PROTO_BM;
    mimas_state_t mst;
    uint16_t temp_bm=0;
    uint8_t *ch;
    if(spi_lock()!=0) return(-1000);

    for(i=0;i<MIMAS_STREAM_OUT_CNT;i++)
    {
        mst = mimas_get_state();
        if( (mst.clk_rdy==0) || (mst.idle == 1) ||(mst.sys_rdy == 0) )
        {
        do{
            MIMAS_RESET;
            usleep(10000ul);
            mst = mimas_get_state();
            }while( (mst.clk_rdy==0) || (mst.idle == 1) ||(mst.sys_rdy == 0) );

        }
        if((start_bm & BIT32(i))==0)continue;

        if(tr[i].len>0)
        {
            ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr[i]);
            if (ret < 1)
            {
                prnErr(log_mim,"error %d sending pack(%u), len = %u , errno %d\n", ret, i, tr[i].len, errno );
                errno =0;
            }
            else
            {
                temp_bm|=(uint16_t)BIT32(i);
            }
        }
        else
        {
            prnErr(log_mim,"WARNING: tr[%d] len = %d\n",i,tr[i].len);
        }
    }

    if(temp_bm!=start_bm)
    {
        prnErr(log_mim,"WARNING: spi : data_bm %x != start_bm %x\n", temp_bm, start_bm);
        temp_bm&=start_bm;
        if(temp_bm == 0 )
        {
            prnErr(log_mim,"ERROR: spi: nothing to send, aborting\n");
            spi_unlock();
            return(-2);
        }
    }
    start_stream_header[1]= temp_bm  & 0xFF; // use the  value that probably wont crash mimas
    start_stream_header[2]= ((temp_bm >> 8) & 0x0F);
    start_stream_header[3]= (proto_bm & 0xFF);
    start_stream_header[4] = (proto_bm>>8) & 0xFF;
    start_stream_header[5] = (proto_bm>>16) & 0xFF;
    ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr[12]);
    spi_unlock();
    if (ret < 1)
    {
        prnErr(log_mim,"can't send spi message");
        return (-3);
    }
    for(i=0;i<MIMAS_STREAM_OUT_CNT;i++)
    {
        if(tr[i].len>0)tr[i].len = 0u;
    }
#ifdef MIMAS_TIME_STAT
    clock_gettime(CLOCK_REALTIME, &ts[1]);
    prnFinf(log_mim,"mimas refresh took %ld\n", nsec_diff(&ts[1], &ts[0]));
#endif
   return(0);
}

int initSPI(void)
{
    uint8_t cs_change = 0;
    int i, ret;

    ret = pthread_spin_init(&spilock, PTHREAD_PROCESS_PRIVATE);
    if(ret)
    {
        perror("failed to init spilock");
        return -1;
    }
    ret = pthread_spin_lock(&spilock);
    if(ret)
    {
        perror("failed to aquire spilock during init");
        return -1;
    }
    delay = 0;
    fd = -1;
	fd = open(device, O_RDWR);
	if (fd < 0)
		pabort("can't open device!!");

	ret = ioctl(fd, SPI_IOC_WR_MODE, &mode);
	if (ret == -1)
		pabort("can't set spi mode");

	ret = ioctl(fd, SPI_IOC_RD_MODE, &mode);
	if (ret == -1)
		pabort("can't get spi mode");

	ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
	if (ret == -1)
		pabort("can't set bits per word");

	ret = ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits);
	if (ret == -1)
		pabort("can't get bits per word");

	ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
	if (ret == -1)
		pabort("can't set max speed hz");
	printf("Max WR speed %u\n",speed);
	ret = ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed);
	if (ret == -1)
		pabort("can't get max speed hz");
	printf("Max RD speed %u\n",speed);

	printf("spi mode: %d\n", mode);
	printf("bits per word: %d\n", bits);
	printf("max speed: %d Hz (%d KHz)\n", speed, speed/1000);
	//printf("sending %u bytes(%x, %x)\n",DATABYTES + 4, mimas[2], mimas[3]);

	memset(tr,0,sizeof(struct spi_ioc_transfer));

		//*(uint32_t*)&start_stream_header[0] = STREAM_START;
	start_stream_header[0] = STREAM_START;
	start_stream_header[1] = 0;
	start_stream_header[2] = 0;
	start_stream_header[3] = 0;
	start_stream_header[4] = 0;
	start_stream_header[5] = 0;

    tr[12].tx_buf = (unsigned long)(void*)&start_stream_header[0];
	tr[12].rx_buf = (unsigned long)(void*)NULL;
	tr[12].len = MIMAS_HDR_LEN;
	tr[12].delay_usecs = delay;
	tr[12].speed_hz = speed;
	tr[12].bits_per_word = 8;
	tr[12].cs_change = cs_change;

	for(i=0;i<12;i++)
	{
	  tr[i].tx_buf = (unsigned long)(void*)NULL;
	  tr[i].rx_buf = (unsigned long)(void*)NULL;
	  tr[i].len = 0;
	  tr[i].delay_usecs = delay;
	  tr[i].speed_hz = speed;
	  tr[i].bits_per_word = 8;
	  tr[i].cs_change = cs_change;
	}
    memset(&tr_pwm,0,sizeof(tr_pwm));
    memset(mimas_pwm,0,sizeof(mimas_pwm));
    tr_pwm.tx_buf = (unsigned long)(void*)&mimas_pwm[0];
    tr_pwm.rx_buf = (unsigned long)(void*)NULL;
    tr_pwm.len = 0;
    tr_pwm.delay_usecs = delay;
    tr_pwm.speed_hz = speed;
    tr_pwm.bits_per_word = 8;
    tr_pwm.cs_change = cs_change;
    pthread_spin_unlock(&spilock);
    printf("Spi init for %d stream devices, mask is %X\n", MIMAS_STREAM_OUT_CNT, MIMAS_STREAM_BM);


return(0);
}



/*
int initSPI(void)
{
int rc;
    rc = bcm2835_spi_begin();
    if(rc == 0)
    {
        perror("bcm2835_spi_begin failed!\n");
        return(-1);
    }
    bcm2835_spi_setClockDivider(BCM2835_SPI_CLOCK_DIVIDER_8); // 50MHz clk
    bcm2835_spi_setDataMode(BCM2835_SPI_MODE0);
    bcm2835_spi_chipSelect(BCM2835_SPI_CS1);
    bcm2835_spi_setChipSelectPolarity(BCM2835_SPI_CS1, 0);
    start_stream_header[0] = 0x20;
	start_stream_header[1] = 0;
	start_stream_header[2] = 0;
	start_stream_header[3] = 0;
    printf("SPI0 init @50MBps ok\n");
    return(0);


}
*/
/*
static void parse_opts(int argc, char *argv[])
{
	while (1) {
		static const struct option lopts[] = {
			{ "device",  1, 0, 'D' },
			{ "speed",   1, 0, 's' },
			{ "delay",   1, 0, 'd' },
			{ "bpw",     1, 0, 'b' },
			{ "loop",    0, 0, 'l' },
			{ "cpha",    0, 0, 'H' },
			{ "cpol",    0, 0, 'O' },
			{ "lsb",     0, 0, 'L' },
			{ "cs-high", 0, 0, 'C' },
			{ "3wire",   0, 0, '3' },
			{ "no-cs",   0, 0, 'N' },
			{ "ready",   0, 0, 'R' },
			{ NULL, 0, 0, 0 },
		};
		int c;

		c = getopt_long(argc, argv, "D:s:d:b:lHOLC3NR", lopts, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'D':
			device = optarg;
			break;
		case 's':
			speed = atoi(optarg);
			break;
		case 'd':
			delay = atoi(optarg);
			break;
		case 'b':
			bits = atoi(optarg);
			break;
		case 'l':
			mode |= SPI_LOOP;
			break;
		case 'H':
			mode |= SPI_CPHA;
			break;
		case 'O':
			mode |= SPI_CPOL;
			break;
		case 'L':
			mode |= SPI_LSB_FIRST;
			break;
		case 'C':
			mode |= SPI_CS_HIGH;
			break;
		case '3':
			mode |= SPI_3WIRE;
			break;
		case 'N':
			mode |= SPI_NO_CS;
			break;
		case 'R':
			mode |= SPI_READY;
			break;
		default:
			print_usage(argv[0]);
			break;
		}
	}
}*/
/*
int transfer( int msg)
{
if(fd == -1)return(-1);
	int ret;
    ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr[msg]);
    if (ret < 1){
    pabort("can't send spi message");
    return (-2);
    }
return(0);
}

static void print_usage(const char *prog)
{
	printf("Usage: %s [-DsbdlHOLC3]\n", prog);
	puts("  -D --device   device to use (default /dev/spidev1.1)\n"
	     "  -s --speed    max speed (Hz)\n"
	     "  -d --delay    delay (usec)\n"
	     "  -b --bpw      bits per word \n"
	     "  -l --loop     loopback\n"
	     "  -H --cpha     clock phase\n"
	     "  -O --cpol     clock polarity\n"
	     "  -L --lsb      least significant bit first\n"
	     "  -C --cs-high  chip select active high\n"
	     "  -3 --3wire    SI/SO signals shared\n");
	exit(1);
}
*/
