#ifndef AUDIO_TEST_FF_H
#define AUDIO_TEST_FF_H
#include <stdio.h>
#include <stdint.h>
typedef unsigned UINT;
typedef uint64_t FSIZE_t;
typedef struct {FILE *fp; uint64_t size;} FIL;
typedef struct {int dummy;} FATFS;
#define FA_READ 1
#define FR_OK 0
#define f_size(f) ((f)->size)
#define f_tell(f) ((uint64_t)ftell((f)->fp))
int f_open(FIL *f,const char *path,int flags);
int f_close(FIL *f);
int f_read(FIL *f,void *data,UINT n,UINT *got);
int f_lseek(FIL *f,FSIZE_t offset);
int f_mount(FATFS *fs,const char *path,int mount);
#endif
