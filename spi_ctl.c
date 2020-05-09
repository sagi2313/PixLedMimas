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
#include <byteswap.h>
static int fd = -1;
int bits=8;
//uint8_t mimas[8][DATABYTES + 4];
uint8_t mimas_pwm[20];
struct spi_ioc_transfer tr[13];
struct spi_ioc_transfer tr_pwm;
static uint8_t start_stream_header[4];


static void pabort(const char *s)
{
	perror(s);
	//abort();
}

static const char *device = "/dev/spidev0.0";
static uint8_t mode;
//static uint8_t bits = 8;
static uint32_t speed = 80000000;
static uint16_t delay;

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

int mimas_send_packet(int chan, uint8_t* data, int len)
{
return(-5); // obsolete, not supported
    if(fd == -1)return(-1);
    if(chan>7)return(-3);
    int ret;
    data[0] = STREAM_PKT_SEND;
    data[1] = 1<<chan;
    data[2] = len/256;
    data[3] = len%256;
    tr[chan].tx_buf = (unsigned long)(void*)data;
    tr[chan].len =4u + len;
//printf("mimas Send pack at ch %u, len = %u\n",chan, len);
    ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr[chan]);
    if (ret < 1){
    pabort("can't send spi message");
    printf("tr[chan].le = %u\n", tr[chan].len );
    return (-2);
    }
    /*
    data[0] = 0x10;
    data[1] = 1<<chan;
    data[2] = len/256;
    data[3] = len%256;
    bcm2835_spi_writenb(&data[0],len+4);
    */
    return(0);
}

/*prepare and save data for messages to be sent to mimas in local buffer 'tr */
int mimas_store_packet(int chan, uint8_t* data, int len)
{

    if(fd == -1)return(-1);
    if(chan > (MIMAS_STREAM_OUT_CNT-1))return(-3);
    int ret;
    uint16_t ch_bm =  1 << chan;
    data[0] = STREAM_PKT_SEND;

    data[1]= (ch_bm & 0xFF);
    //data[2]= ((ch_bm >> 8) <<4) | (len >> 8);
    data[2]= ((ch_bm >> 4) & 0xF0) | (len >> 8);
    data[3]= len & 0xFF;

    tr[chan].tx_buf = (unsigned long)(void*)data;
    tr[chan].len =4u + len;
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

int mimas_store_pwm_val(uint8_t grp, uint8_t chan, uint16_t* val, uint8_t cnt)
{
    if(chan>7)return(-3);
    if(grp>3)return(-13);
    if((cnt ==0)||(cnt>8)) return(-4);
    if(fd == -1)return(-1);
    int i, ret;
    //mimas_cmd_hdr_t* hdr = (mimas_cmd_hdr_t*)(&mimas_pwm[0]);
    //hdr->pwm.opCode = PWM_VAL_ST;
    //hdr->pwm.chan = chan;
    //hdr->pwm.group = grp;
    //hdr->pwm.cnt = cnt;
    mimas_pwm[0] = PWM_VAL_ST;
    mimas_pwm[1] = chan;
    mimas_pwm[2] = grp;
    mimas_pwm[3] = cnt;
    if((chan+cnt)>8)
    {
        cnt = 8 - chan;
    }
    uint16_t* p = &mimas_pwm[4];
    for(i=0;i<cnt;i++)
    {
        *p = *val;
        p++;
        val++;
    }
    tr_pwm.len =4u + (2 * i);

    ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr_pwm);
    if (ret < 1)
    {
        pabort("can't send spi message");
        return (-5);
    }

    return(0);
}

/*
pwm API "mimas_store_pwm_period":
Sets pwm period for a group of pwms
    grp is the group select, and it works as a bitmap. only bits 0 & 1 matter. set bit0 to true for access in first pwm group
        and bit1 for access to second pwm group
    val is a 16bit value to be assigned to groups pwm period
*/
int mimas_store_pwm_period(uint8_t grp, uint16_t val)
{
    if(fd == -1)return(-1);
    if(grp>3)return(-13);
    mimas_pwm[0] = PWM_PER_ST;
    mimas_pwm[1] = 0;
    mimas_pwm[2] = grp;
    mimas_pwm[3] = 1;
    *(uint16_t*)&mimas_pwm[4] = val;
    tr_pwm.len = 6u;
    int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr_pwm);
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
        and bit1 for access to second pwm group
    val is a 8bit value to be assigned to groups pwm frequency divider
*/
int mimas_store_pwm_div(uint8_t grp, uint8_t val)
{
    if(fd == -1)return(-1);
    if(grp>3)return(-13);
    mimas_pwm[0] = PWM_DIV_ST;
    mimas_pwm[1] = 0;
    mimas_pwm[2] = grp;
    mimas_pwm[3] = 1;
    mimas_pwm[4] = val;
    tr_pwm.len = 5u;
    int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr_pwm);
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
        and bit1 for access to second pwm group
    chan is the channel select, and it works as channel id. Valid is 0 through 7.
    enabled is a pointer to array of 8bit values to be assigned to channels starting from "chan" up to chan+cnt.
    in each byte bit0 controls enable flag and bit1 controls invert. Bit0  = 1 -> enable, Bit0 = 1 -> invert
    cnt tells how many channels to set starting from chan. this means that chan + cnt must be up to 8
*/
int mimas_store_pwm_chCntrol(uint8_t grp, uint8_t chan, uint8_t* enabled, uint8_t cnt)
{
    if(chan>7)return(-3);
    if(grp>3)return(-13);
    if((cnt ==0)||(cnt>8)) return(-4);
    if(fd == -1)return(-1);
    int i, ret, j;
    mimas_pwm[0] = PWM_CH_CTRL_ST;
    mimas_pwm[1] = chan;
    mimas_pwm[2] = grp;
    mimas_pwm[3] = cnt;
    uint8_t* p = &mimas_pwm[4];
    j = 0;
    for(i=chan;i<cnt;i++)
    {
        *p = (*enabled) & 3;
        p++;
        enabled++;
        j++;
        if(i>7)break;
    }
    tr_pwm.len =4u + j;
    ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr_pwm);
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
        and bit1 for access to second pwm group
    enabled controls enable flag for selected groups
*/int mimas_store_pwm_gCntrol(uint8_t grp, uint8_t enabled)
{
    if(fd == -1)return(-1);
    if(grp > 3)return(-13);
    mimas_pwm[0] = PWM_G_CTRL_ST;
    mimas_pwm[1] = 0;
    mimas_pwm[2] = grp;
    mimas_pwm[3] = 1;
    mimas_pwm[4] = enabled & 1;
    tr_pwm.len = 5u;
    int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr_pwm);
    if (ret < 1)
    {
        pabort("can't send spi message");
        return (-5);
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
    ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr[8]);
    if (ret < 1){
    pabort("can't send spi message");
    return (-2);
    }

    return(0);
}

int mimas_refresh_start_stream(uint16_t start_bm, uint16_t proto_bm)
{
    if(fd == -1)return(-1);
    int i, ret;
    //struct timespec ts[4];
    //clock_gettime(CLOCK_REALTIME, &ts[0]);
    if(start_bm == 0 ) return(-3);
    start_bm &= MIMAS_STREAM_BM;
    proto_bm &= MIMAS_STREAM_BM;

    uint16_t temp_bm=0;
    uint8_t *ch;

    for(i=0;i<MIMAS_STREAM_OUT_CNT;i++)
    {
        if((start_bm & BIT32(i))==0)continue;
        if(tr[i].len>0)
        {
            ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr[i]);
            if (ret < 1)
            {
                printf("error %d sending pack(%u), len = %u , errno %d\n", ret, i, tr[i].len, errno );
                errno =0;
            }
            else{
            //ch = (uint8_t*)(tr[i].tx_buf);
            //temp_bm|=ch[1];
            //temp_bm|=(ch[2] & 0xF0) << 8;
            temp_bm|=(uint16_t)BIT32(i);
            }
        }
        else{
            printf("WARNING: tr[%d] len = %d\n",i,tr[i].len);
        }
    }

    //clock_gettime(CLOCK_REALTIME, &ts[1]);
    if(temp_bm!=start_bm)
    {
        printf("WARNING: spi : data_bm %x != start_bm %x\n", temp_bm, start_bm);
        temp_bm&=start_bm;
        if(temp_bm == 0 )
        {
            printf("ERROR: spi: nothing to send, aborting\n");
            return(-2);
        }
    }
    start_stream_header[1]= temp_bm  & 0xFF; /// use the  value that probably wont crash mimas
    start_stream_header[2]= ((temp_bm >> 4) & 0xF0);
    start_stream_header[2]|= (proto_bm>> 8);
    start_stream_header[3] = proto_bm & 0xFF;
   // printf("MIMAS start %X\n", start_bm);
    ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr[12]);
    if (ret < 1)
    {
        pabort("can't send spi message");
        return (-3);
    }
    for(i=0;i<MIMAS_STREAM_OUT_CNT;i++)
    {
        if(tr[i].len>0)tr[i].len = 0u;
    }
    //clock_gettime(CLOCK_REALTIME, &ts[2]);
    //printf("spi times: %ld | %ld \n", ts[1].tv_nsec - ts[0].tv_nsec, ts[2].tv_nsec - ts[0].tv_nsec);
    return(0);
}
/*
int spi_main(int argc, char *argv[])
{
	int ret = 0;
	int fd;
	int i=0, j;

	//parse_opts(argc, argv);
	memset(tr,0,sizeof(tr));
    tr[8].tx_buf = (unsigned long)(void*)&mimas[0][0];
	tr[8].rx_buf = (unsigned long)(void*)NULL;
	tr[8].len = 4;
	tr[8].delay_usecs = 20;
	tr[8].speed_hz = speed;
	tr[8].bits_per_word = 8;
	mimas[8][0] = 0x80;
	mimas[8][1] = 0xFF;
	mimas[8][2] = 0;
	mimas[8][3] = 0;
	for(i=0;i<8;i++)
	{
	  tr[i].tx_buf = (unsigned long)(void*)&mimas[i][0];
	  tr[i].rx_buf = (unsigned long)(void*)NULL;
	  tr[i].len =DATABYTES + 4;
	  tr[i].delay_usecs = delay;
	  tr[i].speed_hz = speed;
	  tr[i].bits_per_word = bits;
	  mimas[i][0]=0x10;
	  mimas[i][1]=(1<<i);
	  mimas[i][2]=(uint8_t)(DATABYTES/256);
	  mimas[i][3]=(uint8_t)(DATABYTES%256);
	}
	for(j=0;j<8;j++)
	{
		for(i=4;i<(4u + DATABYTES);i++)
		{
			mimas[j][i]=i%101;
		}
	}


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
	printf("sending %u bytes(%x, %x)\n",DATABYTES + 4, mimas[2], mimas[3]);
	transfer(fd);

	close(fd);

	return ret;
}
*/
int initSPI(void)
{
delay = 1;
uint8_t cs_change = 0;
int i, ret;
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

    tr[12].tx_buf = (unsigned long)(void*)&start_stream_header[0];
	tr[12].rx_buf = (unsigned long)(void*)NULL;
	tr[12].len = 4;
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
