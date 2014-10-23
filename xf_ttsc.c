
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "include/qtts.h"
#include "include/msp_cmn.h"
#include "include/msp_errors.h"


#define DEBUG
#ifdef DEBUG
FILE *fp = NULL;
#define debug(fmt, args...) \
{ \
    fprintf(fp, "[%s:%d]" fmt "\n", __FILE__, __LINE__, ##args); \
    fflush(fp); \
}
#else
#define debug(fmt, args...)
#endif

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
    debug("read_cmd got len %u", len);
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
        if((i=read(3,buf+got,len-got))<=0) {
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
        if((i=write(4, buf+wrote,len-wrote))<=0)
            return(i);
        wrote+=i;
    }while(wrote<len);
    fflush(stdout);
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
    debug("setting tts pararms");
    if (!tts_params) free(tts_params);
    tts_params = malloc(sizeof(byte)*(len+1));
    memcpy(tts_params, buf, len);
    tts_params[len] = 0;
    return 0;
}

int text_to_speech(byte *buf, int len)
{
    const char* sess_id = NULL;
    char *obuf = NULL;
    unsigned int audio_len = 0;
    unsigned int olen = 0;
    int synth_status = 1;
    int ret = 0;
    byte erlret = 0;

    debug("Texting to speech %d bytes, %s", len, buf);
    ret = MSPLogin(NULL, NULL, login_configs);
    if ( ret != MSP_SUCCESS ) {
        debug("MSPLogin failed: %d", ret);
        return ret;
    }

    sess_id = QTTSSessionBegin(tts_params, &ret);
    if ( ret != MSP_SUCCESS ) {
        debug("QTTSSessionBegin failed: %d", ret);
        return ret;
    }

    ret = QTTSTextPut(sess_id, buf, len, NULL );
    if ( ret != MSP_SUCCESS ) {
        debug("QTTSTextPut failed: %d", ret);
        QTTSSessionEnd(sess_id, "TextPutError");
        return ret;
    }

    while (1) 
    {
        const void *data = QTTSAudioGet(sess_id, &audio_len, &synth_status, &ret);
        if (NULL != data)
        {
            obuf = realloc(obuf, olen+audio_len);
            memcpy(obuf+olen, data, audio_len);
            olen += audio_len;
        }
        usleep(15000);
        if (synth_status == 2 || ret != 0) 
            break;
    }

    debug("got %d bytes speech", olen);

    write_head(sizeof(erlret)+olen);
    write_exact(&erlret, sizeof(erlret));
    write_exact(obuf, olen);
    free(obuf);

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
    fp = fopen("/opt/kazoo/xf.log", "a+");
#endif

    while ((len = read_cmd(&buf)) > 0){
        fn = buf[0];
        debug("cmd %d", fn);
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
            debug("unknown cmd %d", fn);
            exit(EXIT_FAILURE);
        }

        free(buf);
    }
    debug("exiting");
#ifdef DEBUG
    fclose(fp);
#endif
}
