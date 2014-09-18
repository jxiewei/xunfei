
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "../../include/qtts.h"
#include "../../include/msp_cmn.h"
#include "../../include/msp_errors.h"


#define DEBUG
#ifdef DEBUG
FILE *fp = NULL;
#define debug(fmt, args...) fprintf(fp, fmt, ##args)
#else
#define debug(fmt, args...)
#endif
typedef int SR_DWORD;
typedef short int SR_WORD ;


struct wave_pcm_hdr
{
    char            riff[4];
    SR_DWORD        size_8;
    char            wave[4];
    char            fmt[4];
    SR_DWORD        dwFmtSize;

    SR_WORD         format_tag;
    SR_WORD         channels;
    SR_DWORD        samples_per_sec;
    SR_DWORD        avg_bytes_per_sec;
    SR_WORD         block_align;
    SR_WORD         bits_per_sample;

    char            data[4];
    SR_DWORD        data_size;
} ;


struct wave_pcm_hdr default_pcmwavhdr = 
{
    { 'R', 'I', 'F', 'F' },
    0,
    {'W', 'A', 'V', 'E'},
    {'f', 'm', 't', ' '},
    16,
    1,
    1,
    16000,
    32000,
    2,
    16,
    {'d', 'a', 't', 'a'},
    0  
};

enum {
    CMD_TTS = 0,
    CMD_SET_LOGIN_CONFIG = 1,
    CMD_SET_TTS_PARAMS = 2,
};

typedef unsigned char byte;

int read_cmd(byte **buf)
{
    unsigned int len;
    unsigned char tmp[4];

    if ((len = read_exact(tmp,4)) != 4) {
        return(-1);
    }

    len = ntohl(*((unsigned int *)tmp));
    debug("read_cmd got len %u\n", len);
    *buf = malloc(len*sizeof(byte));
    read_exact(*buf,len);
}

int write_head(unsigned int len) {
    unsigned int li = htonl(len);
    write_exact(&li,4);
}

int write_cmd(byte *buf,int len)
{
    write_head(len);
    return write_exact(buf,len);
}

int read_exact(byte *buf,int len)
{
    int i,got=0;
    do{
        if((i=read(0,buf+got,len-got))<=0) {
            if (i < 0)
                debug("read failed: %s\n", strerror(errno));
            return(i);
        }
        got+=i;
    }while(got<len);
    return(len);
}

int write_exact(byte *buf,int len)
{
    int i,wrote=0;
    do{
        if((i=write(1,buf+wrote,len-wrote))<=0)
            return(i);
        wrote+=i;
    }while(wrote<len);
    return(len);
}

char *login_configs = NULL;
int set_login_config(byte *buf, int len)
{ 
    if (!login_configs) free(login_configs);
    login_configs = malloc(sizeof(byte)*(len+1));
    memcpy(login_configs, buf, len);
    login_configs[len] = 0;
    return 0;
}

char *tts_params = NULL;
int set_tts_params(byte *buf, int len)
{
    debug("setting tts pararms\n");
    if (!tts_params) free(tts_params);
    tts_params = malloc(sizeof(byte)*(len+1));
    memcpy(tts_params, buf, len);
    tts_params[len] = 0;
    return 0;
}

int text_to_speech(byte *buf, int len)
{
    struct wave_pcm_hdr pcmwavhdr = default_pcmwavhdr;
    const char* sess_id = NULL;
    unsigned int text_len = 0;
    char* audio_data, *obuf = NULL;
    unsigned int audio_len = 0;
    unsigned int olen = 0;
    int synth_status = 1;
    int ret = 0;
    byte erlret = 0;

    debug("Texting to speech %d bytes, %s\n", len, buf);
    ret = MSPLogin(NULL, NULL, login_configs);
    if ( ret != MSP_SUCCESS )
        return ret;

    sess_id = QTTSSessionBegin(tts_params, &ret);
    if ( ret != MSP_SUCCESS )
        return ret;

    ret = QTTSTextPut(sess_id, buf, len, NULL );
    if ( ret != MSP_SUCCESS ) {
        QTTSSessionEnd(sess_id, "TextPutError");
        return ret;
    }

    while (1) 
    {
        const void *data = QTTSAudioGet(sess_id, &audio_len, &synth_status, &ret);
        if (NULL != data)
        {
            pcmwavhdr.data_size += audio_len;
            obuf = realloc(obuf, pcmwavhdr.data_size);
            memcpy(obuf+olen, data, audio_len);
            olen += audio_len;
        }
        usleep(150000);
        if (synth_status == 2 || ret != 0) 
            break;
    }

    debug("got %d bytes speech\n", olen);

    pcmwavhdr.size_8 += pcmwavhdr.data_size + 36;
    write_head(sizeof(erlret)+sizeof(pcmwavhdr)+olen);
    write_exact(&erlret, sizeof(erlret));
    write_exact((byte*)&pcmwavhdr, sizeof(pcmwavhdr));
    write_exact(obuf, olen);

    QTTSSessionEnd(sess_id, NULL);
    MSPLogout();
    return 0;
}

int main(int argc, char *argv[]) 
{
    byte *buf = NULL;
    int len = 0;
    int fn;
    byte ret = 0;

#ifdef DEBUG
    fp = fopen("/opt/kazoo/xf.log", "w+");
#endif

    while ((len = read_cmd(&buf)) > 0){
        fn = buf[0];
        debug("cmd %d\n", fn);
        if (fn == CMD_TTS) {
            if ((ret = text_to_speech(buf+1, len-1)) != 0) {
               write_cmd(&ret, sizeof(ret)); 
            }
        }
        else if (fn == CMD_SET_LOGIN_CONFIG) {
            ret = set_login_config(buf+1, len-1);
            write_cmd(&ret, sizeof(ret)); 
        }
        else if (fn == CMD_SET_TTS_PARAMS) {
            ret = set_tts_params(buf+1, len-1);
            write_cmd(&ret, sizeof(ret)); 
        }
        else {
            debug("uninown cmd %d\n", fn);
            exit(EXIT_FAILURE);
        }

        free(buf);
    }
    debug("exiting\n");
#ifdef DEBUG
    fclose(fp);
#endif
}
