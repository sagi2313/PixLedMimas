#ifndef RECORDER_H_INCLUDED
#define RECORDER_H_INCLUDED
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

typedef union
{
    uint16_t        gAdr;
    art_net_net_t   artAdr;
}g_adr_u;
typedef struct
{
    g_adr_u     adr;
    uint16_t    chunk_len;
    uint8_t     chunkData[512];
}whole_art_packs_rec_t;

typedef struct
{
    char                    fname[32];
    uint32_t                maxLen;
    uint32_t                len;
    void*                   shmP;
    uint32_t                crec_num;
    whole_art_packs_rec_t*  crec;
}recFile_t;

int createRecFile(/*bool fullRec,*/ recFile_t* rf);
//int record(whole_art_packs_rec_t *rec, recFile_t* rf);
int record(whole_art_packs_rec_t *rec, recFile_t* rf, uint8_t* dat);
#endif // RECORDER_H_INCLUDED
