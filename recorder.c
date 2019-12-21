#include "type_defs.h"

#include "recorder.h"


void* pvtmMmapAlloc (char * mmapFileName, size_t size, char create)
{
  void * retv = NULL;
  if (create)
  {
    mode_t origMask = umask(0);
    int mmapFd = open(mmapFileName, O_CREAT|O_RDWR, 00666);
    umask(origMask);
    if (mmapFd < 0)
    {
      perror("open mmapFd failed");
      return NULL;
    }
    if ((ftruncate(mmapFd, size) == 0))
    {
      int result = lseek(mmapFd, size - 1, SEEK_SET);
      if (result == -1)
      {
        perror("lseek mmapFd failed");
        close(mmapFd);
        return NULL;
      }

      /* Something needs to be written at the end of the file to
       * have the file actually have the new size.
       * Just writing an empty string at the current file position will do.
       * Note:
       *  - The current position in the file is at the end of the stretched
       *    file due to the call to lseek().
              *  - The current position in the file is at the end of the stretched
       *    file due to the call to lseek().
       *  - An empty string is actually a single '\0' character, so a zero-byte
       *    will be written at the last byte of the file.
       */
      result = write(mmapFd, "", 1);
      if (result != 1)
      {
        perror("write mmapFd failed");
        close(mmapFd);
        return NULL;
      }
      retv  =  mmap(NULL, size,
                  PROT_READ | PROT_WRITE, MAP_SHARED, mmapFd, 0);

      if (retv == MAP_FAILED || retv == NULL)
      {
        perror("mmap");
        close(mmapFd);
        return NULL;
      }
    }
  }
  else
  {
    int mmapFd = open(mmapFileName, O_RDWR, 00666);
    if (mmapFd < 0)
    {
      return NULL;
    }
    int result = lseek(mmapFd, 0, SEEK_END);
    if (result == -1)
    {
      perror("lseek mmapFd failed");
      close(mmapFd);
      return NULL;
    }
    if (result == 0)
    {
      perror("The file has 0 bytes");
      close(mmapFd);
      return NULL;
    }
    retv  =  mmap(NULL, size,
                PROT_READ | PROT_WRITE, MAP_SHARED, mmapFd, 0);

    if (retv == MAP_FAILED || retv == NULL)
    {
      perror("mmap");
      close(mmapFd);
      return NULL;
    }

    close(mmapFd);

  }
  return retv;
}


//void* createRecFile(/*bool fullRec,*/ const char* fname, size_t flen)
int createRecFile(/*bool fullRec,*/ recFile_t* rf)
{
    void* pt =pvtmMmapAlloc( rf->fname, rf->maxLen, 1);
    rf->crec = (whole_art_packs_rec_t*)pt;
    rf->shmP = pt;
    rf->len = 0;
    rf->crec_num = 0;
    if(pt!=NULL)
    {
        memset(pt,0,rf->maxLen);
        return(0);
    }
    return (-1);
}
int record(whole_art_packs_rec_t *rec, recFile_t* rf, uint8_t* dat)
{
    if(rf == NULL)return(-1);
    if(rf->shmP == NULL) return(-2);
    if(rf->len + sizeof(whole_art_packs_rec_t) >= rf->maxLen)return (-3);
   // memcpy((void*)&rf->crec[rf->crec_num], rec, sizeof(whole_art_packs_rec_t));
    rf->crec[rf->crec_num].adr = rec->adr;
    memcpy(&rf->crec[rf->crec_num].chunkData[0], dat, 512);
    rf->len+=sizeof(whole_art_packs_rec_t);
    return(rf->crec_num++);
}
