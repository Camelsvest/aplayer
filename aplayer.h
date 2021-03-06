#ifndef _APLAYER_H_
#define _APLAYER_H_

#include <stdio.h>
#include <pthread.h>
#include <alsa/asoundlib.h>

#include <list>
using namespace std;

#include "wav_file.h"

class APlayer
{
public:
    APlayer(bool nonblock = false);
    virtual ~APlayer();

    int play(const char *filename);
    void stop();
    bool isRunning();

    static void* readingThreadFunc(void *data);
    static void* playingThreadFunc(void *data);
    void * readingTask(void *data);
    void * playingTask();

protected:
    typedef struct {
        APlayer *self;
        void    *data;
    } thread_param_t;
    
private:
    const char * getFileNameExt(const char *filename);
    bool   isWavFile(const char *filename);

    snd_pcm_format_t getPCMFormat(WavFile *file);

    int    initHW();
    void   uninitHW();
    int    setParams(WavFile *file);

    /*
     * count - frame count actually
     */
    ssize_t pcmWrite(char *data, size_t count);
    void    xrun(void);
    void    suspend(void);

    bool isPlaying;
    bool isReading;
    FILE *fp;
    pthread_t readingThID;
    pthread_t playingThID;
   
    pthread_mutex_t *lock;
    pthread_cond_t  *cond;
   
    int openMode;    
    snd_pcm_t *handle;
    snd_output_t *log;
    snd_pcm_uframes_t chunkSize;    /* unit is frame */
    size_t chunkBytes;    

    /* for playing */
    snd_pcm_format_t format;
    uint16_t bitsPerFrame;
    uint16_t channels;
    uint16_t bytesPerSample;

    typedef struct {
        char *buffer;
        uint32_t bufSize;
    } buf_data_t;
    list<buf_data_t *> bufList;
};
#endif
