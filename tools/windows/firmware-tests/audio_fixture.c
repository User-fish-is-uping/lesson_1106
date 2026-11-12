#include "audio_player.h"
#include "hal.h"
#include "ff.h"
#include <string.h>
static const char *input;
static unsigned starts, active, done, no_memory;
void fpioa_set_function(int p,int f) {(void)p;(void)f;}
void sysctl_pll_set_freq(int p,unsigned long hz) {(void)p;(void)hz;}
void i2s_init(int d,int m,int mask) {(void)d;(void)m;(void)mask;}
void i2s_tx_channel_config(int d,int c,int r,int cy,int t,int m)
{(void)d;(void)c;(void)r;(void)cy;(void)t;(void)m;}
void i2s_set_sample_rate(int d,unsigned r) {(void)d; if(r!=16000) exit(40);}
void i2s_send_data_dma(int d,const void *b,size_t n,int c)
{
    if(d!=2 || c!=3 || !b || !n || n>96000 || active) exit(41);
    const uint32_t *words=b;
    for(size_t i=0;i<n;i+=2) if(words[i]!=words[i+1]) exit(42);
    active=1; starts++;
}
int dmac_is_done(int c) {(void)c;return done;}
void dmac_channel_disable(int c) {(void)c;active=0;}
void *iomem_malloc(size_t n) {return no_memory ? NULL : malloc(n);}
void iomem_free(void *p) {free(p);}
int f_open(FIL *f,const char *path,int flags)
{
    (void)path;(void)flags; f->fp=fopen(input,"rb"); if(!f->fp)return 1;
    fseek(f->fp,0,SEEK_END); f->size=ftell(f->fp); rewind(f->fp); return 0;
}
int f_close(FIL *f) {return fclose(f->fp);}
int f_read(FIL *f,void *p,UINT n,UINT *got) {*got=(UINT)fread(p,1,n,f->fp);return 0;}
int f_lseek(FIL *f,FSIZE_t o) {return fseek(f->fp,(long)o,SEEK_SET);}
int f_mount(FATFS *f,const char *p,int m) {(void)f;(void)p;(void)m;return 0;}
int main(int argc,char **argv)
{
    if(argc!=3)return 1;
    input=argv[1]; no_memory=!strcmp(argv[2],"nomem");
    audio_player_init();
    if(strcmp(argv[2],"valid")) {
        if(audio_player_start(GREETER_ENTER,0)!=AUDIO_FAILED || starts) return 2;
        return strcmp(audio_player_status(),"MISSING/BAD WAV") ? 3 : 0;
    }
    if(audio_player_start(GREETER_ENTER,0)!=AUDIO_STARTED)return 4;
    if(audio_player_start(GREETER_EXIT,10)!=AUDIO_BUSY)return 5;
    done=1; if(audio_player_service(100))return 6;
    done=0; if(audio_player_start(GREETER_EXIT,101)!=AUDIO_STARTED)return 7;
    if(!audio_player_service(5000) || active)return 8;
    if(audio_player_service(5001))return 9;
    if(audio_player_start(GREETER_ENTER,5002)!=AUDIO_FAILED)return 10;
    return starts==2 ? 0 : 11;
}
