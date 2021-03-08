#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "aplayer.h"
#include "wav_file.h"
#include "pcm_utils.h"

#define DEFAULT_FORMAT		SND_PCM_FORMAT_U8
#define DEFAULT_SPEED 		8000

#define DEFAULT_INTERLEAVED 1
#define MAX_RING_BUF_LENGTH 300000 /* ring buffer length in us, microseconds */

#define DEFAULT_CHUNK_COUNT 3       
#define SLEEP_TIME          20*1000000 /*nanoseconds*/

#define DEBUG
#ifdef DEBUG
#define DBG(fmt, ...)           \
do {                            \
    struct timeval now;         \
    struct tm tmNow;            \
    gettimeofday(&now, NULL);   \
    localtime_r(&now.tv_sec, &tmNow); \
    fprintf(stderr, "%02d:%02d:%02d.%03ld 0x%08X %s:%d: " fmt, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec, now.tv_usec/1000, \
            (uint32_t)pthread_self(), __FILE__, __LINE__, ##__VA_ARGS__); \
} while (0)
#else
#define DBG(...)
#endif

APlayer::APlayer(bool nonblock)
    : isPlaying(false)
    , isReading(false)
    , fp(NULL)
    , readingThID(0)
    , playingThID(0)
    , lock(NULL)
    , cond(NULL)
    , handle(NULL)
    , log(NULL)
{
    openMode = 0;
    if (nonblock)
        openMode |= SND_PCM_NONBLOCK;

}

APlayer::~APlayer()
{
    if (lock)
    {
        pthread_mutex_destroy(lock);
        free(lock);
    }

    if (cond)
    {
        pthread_cond_destroy(cond);
        free(cond);
    }
}

int APlayer::play(const char * filename, const char *device)
{
    WavFile *wav;
    int ret = -1;
    
    if (isRunning())
        stop();

    if (isWavFile(filename))
    {
        wav = new WavFile();
        ret = wav->open(filename);
        if (ret < 0)
        {
            DBG("Failed to open %s\n", filename);
            return -1;
        }
        
        if (initHW(device) < 0)
            return -1;

        setParams(wav);
    }

    if (lock == NULL)
    {
        lock = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
        pthread_mutex_init(lock, NULL);
    }
    
    if (cond == NULL)
    {
        cond = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
        pthread_cond_init(cond, NULL);
    }   

    if (ret == 0)
    {
        thread_param_t *param;
        param = (thread_param_t *)malloc(sizeof(thread_param_t));
        param->self = this;
        param->data = wav;
        ret = pthread_create(&readingThID, NULL, readingThreadFunc, (void *)param);
    }

    if (ret == 0)
    {
        ret = pthread_create(&playingThID, NULL, playingThreadFunc, (void *)this);
    }
    
    return ret;
}

void APlayer::stop()
{
    void *retval;

    if (readingThID != 0 && playingThID != 0 && lock != NULL && cond != NULL)
    {
        pthread_mutex_lock(lock);
        isReading = false;
        isPlaying = false;

        pthread_cond_signal(cond);
        pthread_mutex_unlock(lock);

        pthread_join(readingThID, &retval);
        readingThID = 0;
        pthread_join(playingThID, &retval);
        playingThID = 0;

        uninitHW();
    }

}

bool APlayer::isRunning()
{
    return (readingThID != 0 || playingThID != 0);
}

void* APlayer::readingThreadFunc(void *args)
{
    thread_param_t *param;
    void *data;

    param = (thread_param_t *)args;

    APlayer *player = param->self;
    data = param->data;

    free(args);
    
    return player->readingTask(data);
}

void* APlayer::playingThreadFunc(void *data)
{
    APlayer *player = static_cast<APlayer *>(data);

    return player->playingTask();
}

void* APlayer::readingTask(void *data)
{   
    char *buffer;
    buf_data_t *bufData;
    int bytes, requestBytes, bufSize, totalBytes;
    WavFile *wav;
    isReading = true;

    DBG("ReadingTask started.\r\n");

    wav = static_cast<WavFile *>(data);
    if (wav)
    {
        totalBytes = wav->length();
        assert(totalBytes > 0);
        
        pthread_mutex_lock(lock);
        do 
        {
            pthread_cond_wait(cond, lock);

            assert(chunkBytes > 0);
            bufSize = DEFAULT_CHUNK_COUNT * chunkBytes;
            buffer = (char *)malloc(bufSize);
            if (totalBytes > bufSize)
                requestBytes = bufSize;
            else
                requestBytes = totalBytes;
            bytes = wav->readData(buffer, requestBytes);
        
            if (bytes > 0)
            {
                bufData = (buf_data_t *)malloc(sizeof(buf_data_t));
                bufData->buffer = buffer;
                bufData->bufSize = bytes;

                bufList.push_back(bufData);

                totalBytes -= bytes;

                if (bytes < requestBytes)
                    isReading = false; /* finished */
            }
            else
            {
                DBG("read error, break\r\n");
                break; /* error */
            }
                
        }
        while(isReading && totalBytes > 0);
        pthread_mutex_unlock(lock);

        if (wav)
        {
            wav->close();
            delete wav;        
        }
    }

    isReading = false;
    
    DBG("ReadingTask stoped.\r\n");

    return NULL;
}

void* APlayer::playingTask()
{    
    buf_data_t *bufData;
    uint32_t count, bytes, size = 0, retry = 0;

    DBG("PlayingTask started.\r\n");

    pthread_mutex_lock(lock);
    
    isPlaying = true;

    while (isPlaying)
    {
        if (!bufList.empty())
        {
            bufData = bufList.front();
            bufList.pop_front();
            pthread_cond_signal(cond);
            DBG("Ask to read more...\r\n");
            pthread_mutex_unlock(lock);
        }
        else if (isReading)
        {
            pthread_mutex_unlock(lock);
            pthread_cond_signal(cond);
            DBG("sleep 10 millseconds\r\n");
            usleep(10*1000);    // 10 ms
            pthread_mutex_lock(lock);
            continue;
        }            
        else if (size > 0)
        {
            // we had played something, now reading thread had quited
            break;  /* reading thread had exited */
        }
        else 
        {
            // isReading == false; wait reading thread startup
            usleep(10*1000);
            retry++;
            if (retry <= 3)
                continue;
            else
                break;
        }


        bytes = 0;
        assert(bufData);
        while ( bufData->bufSize > bytes && isPlaying)
        {
            if ((bufData->bufSize - bytes) >= chunkBytes)
                count = chunkBytes * 8 / bitsPerFrame;
            else
                count = (bufData->bufSize - bytes) * 8 / bitsPerFrame;
            
            size = pcmWrite(bufData->buffer + bytes, count);

            bytes += size * bitsPerFrame / 8;
        }

        free(bufData->buffer);
        free(bufData);

        pthread_mutex_lock(lock);
    }    

    while (!bufList.empty())
    {
        bufData = bufList.front();
        bufList.pop_front();    

        free(bufData->buffer);
        free(bufData);        
    }

    isPlaying = false;
    
    pthread_mutex_unlock(lock);

	snd_pcm_nonblock(handle, 0);
	snd_pcm_drain(handle);

	// behavious copies from aplay, ALSA example
	if (openMode & SND_PCM_NONBLOCK)
    	snd_pcm_nonblock(handle, 1);

    DBG("PlayingTask stoped.\r\n");

    return NULL;
}

const char* APlayer::getFileNameExt(const char *filename)
{
    const char *p;

    p = strrchr(filename, '.');
    if (p)
    {
        ++p;
    }

    return p;
}

bool APlayer::isWavFile(const char *filename)
{
    size_t length, i;
    char *ext, str[4];

    ext = (char *)getFileNameExt(filename);
    
    length = strlen(ext);
    if (length > sizeof(str))
        length = sizeof(str);

    for (i = 0; i < length; i++)
        str[i] = tolower(ext[i]);
    
    return (0 == strcmp("wav", str));
}

snd_pcm_format_t APlayer::getPCMFormat(WavFile *file)
{
    snd_pcm_format_t format = SND_PCM_FORMAT_UNKNOWN;

    assert(file != NULL);
    switch(file->bits())
    {
    case 8:
		format = SND_PCM_FORMAT_U8;
		break;
	case 16:
		if (file->isBigEndian())
			format = SND_PCM_FORMAT_S16_BE;
		else
			format = SND_PCM_FORMAT_S16_LE;
		break;
    case 24:
		switch (TO_CPU_SHORT(file->bytes(), file->isBigEndian()) / file->channels()) 
        {
		case 3:
			if (file->isBigEndian())
				format = SND_PCM_FORMAT_S24_3BE;
			else
				format = SND_PCM_FORMAT_S24_3LE;
			break;
		case 4:
			if (file->isBigEndian())
				format = SND_PCM_FORMAT_S24_BE;
			else
				format = SND_PCM_FORMAT_S24_LE;
			break;
		default:
			break;
		}
		break;    
    case 32:
        if (file->format() == WAV_FMT_PCM)
        {
			if (file->isBigEndian())
				format = SND_PCM_FORMAT_S32_BE;
			else
				format = SND_PCM_FORMAT_S32_LE;
		} 
        else if (file->format() == WAV_FMT_IEEE_FLOAT)
        {
			if (file->isBigEndian())
				format = SND_PCM_FORMAT_FLOAT_BE;
			else
				format = SND_PCM_FORMAT_FLOAT_LE;
		}
		break;
    default:
        break;        
    }

    return format;
}

int APlayer::initHW(const char *device)
{
    int err;
    snd_pcm_info_t *info;

    snd_pcm_info_alloca(&info);
	err = snd_output_stdio_attach(&log, stdout, 0);
	assert(err >= 0);

    if (device && strlen(device) > 0)
    {
        err = snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK, openMode);
        if (err < 0)
        {
            DBG("audio open error: %s\n", snd_strerror(err));
            return -1;
        }
    }
    else
    {
        DBG("Invalid parameters.\n");
        return -1;
    }
    
    err = snd_pcm_info(handle, info);
    if (err < 0)
    {
        DBG("info error: %s\n", snd_strerror(err));
        return -1;
    }

    if (openMode & SND_PCM_NONBLOCK)
    {
        err = snd_pcm_nonblock(handle, 1);
		if (err < 0) {
			DBG("nonblock setting error: %s", snd_strerror(err));
			return -1;
		}        
    }

    return 0;
}

void APlayer::uninitHW()
{
	snd_pcm_close(handle);
	handle = NULL;

	snd_output_close(log);
	snd_config_update_free_global();	
}

int APlayer::setParams(WavFile *file)
{
    int err;
    struct {
        snd_pcm_format_t format;
	    unsigned int channels;
	    unsigned int rate;
    } hwparams;
	snd_pcm_hw_params_t *params;
	snd_pcm_sw_params_t *swparams;
	uint32_t rate, bufferTime, periodTime;
	snd_pcm_uframes_t bufferSize, startThreshold, stopThreshold;
	
    assert(file != NULL);
    hwparams.channels = file->channels();
    hwparams.format = getPCMFormat(file);
    hwparams.rate = file->rate();

    format = hwparams.format;
    channels = hwparams.channels;
    bitsPerFrame = file->bits() * channels;
    bytesPerSample = file->bytes();

	snd_pcm_hw_params_alloca(&params);
	snd_pcm_sw_params_alloca(&swparams);
	
	err = snd_pcm_hw_params_any(handle, params);
	if (err < 0)
	{
		DBG("Broken configuration for this PCM: no configurations available");
		return -1;
	}

    if (DEFAULT_INTERLEAVED)
		err = snd_pcm_hw_params_set_access(handle, params,
                SND_PCM_ACCESS_RW_INTERLEAVED);
	else
		err = snd_pcm_hw_params_set_access(handle, params,
				SND_PCM_ACCESS_RW_NONINTERLEAVED);

	if (err < 0)
	{
		DBG("Access type not available");
		return -1;
	}

	err = snd_pcm_hw_params_set_format(handle, params, format);
	if (err < 0)
	{
		DBG("Sample format non available\r\n");
		show_available_sample_formats(handle, params);
		return -1;
	}

	err = snd_pcm_hw_params_set_channels(handle, params, channels);
	if (err < 0)
	{
		DBG("Channels count non available");
		return -1;
	}	

	rate = hwparams.rate;
	err = snd_pcm_hw_params_set_rate_near(handle, params, &hwparams.rate, 0);
	assert(err >= 0);
	if ((float)rate * 1.05 < hwparams.rate || (float)rate * 0.95 > hwparams.rate)
	{
		char plugex[64];
		const char *pcmname = snd_pcm_name(handle);
		DBG("Warning: rate is not accurate (requested = %iHz, got = %iHz)\n",
		                rate, hwparams.rate);
		if (! pcmname || strchr(snd_pcm_name(handle), ':'))
			*plugex = 0;
		else
			snprintf(plugex, sizeof(plugex), "-Dplug:%s", snd_pcm_name(handle));
		DBG("         please, try the plug plugin %s\n", plugex);
	}

	rate = hwparams.rate;

	err = snd_pcm_hw_params_get_buffer_time_max(params, &bufferTime, 0); // us
	assert(err >= 0);
	if (bufferTime > MAX_RING_BUF_LENGTH)
		bufferTime = MAX_RING_BUF_LENGTH;

	periodTime = bufferTime / 4;
	assert(periodTime > 0);
	err = snd_pcm_hw_params_set_period_time_near(handle, params,
						     &periodTime, 0);
	assert(err >= 0);

	assert(bufferTime > 0);
	err = snd_pcm_hw_params_set_buffer_time_near(handle, params,
						     &bufferTime, 0);

	assert(err >= 0);	

	err = snd_pcm_hw_params(handle, params);
	if (err < 0)
	{
		DBG("Unable to install hw params:");
		snd_pcm_hw_params_dump(params, log);
		return -1;
	}

	snd_pcm_hw_params_get_period_size(params, &chunkSize, 0);
	snd_pcm_hw_params_get_buffer_size(params, &bufferSize);
	if (chunkSize == bufferSize)
	{
		DBG("Can't use period equal to buffer size (%lu == %lu)\r\n",
		        chunkSize, bufferSize);
		return -1;
	}
	chunkBytes = chunkSize * file->bits() * file->channels() / 8;

	err = snd_pcm_sw_params_current(handle, swparams);
	if (err < 0)
	{
		DBG("Unable to get current sw params.");
		return -1;
	}

	err = snd_pcm_sw_params_set_avail_min(handle, swparams, chunkSize);

	/* round up to closest transfer boundary */
	startThreshold = bufferSize;
    err = snd_pcm_sw_params_set_start_threshold(handle, swparams, startThreshold);
	assert(err >= 0);
	stopThreshold = bufferSize;
	err = snd_pcm_sw_params_set_stop_threshold(handle, swparams, stopThreshold);
	assert(err >= 0);

	if (snd_pcm_sw_params(handle, swparams) < 0)
	{
		DBG("unable to install sw params:");
		snd_pcm_sw_params_dump(swparams, log);
		return -1;
	}

    snd_pcm_dump(handle, log);

    return 0;
}

ssize_t APlayer::pcmWrite(char *data, size_t count)
{
	ssize_t r;
	ssize_t result = 0;

	if (count < chunkSize)
    {
		snd_pcm_format_set_silence(format, data + count * bitsPerFrame / 8, (chunkSize - count) * channels);
		count = chunkSize;
	}

	while (count > 0)
    {
		r = snd_pcm_writei(handle, data, count);
		if (r == -EAGAIN || (r >= 0 && (size_t)r < count))
        {
			snd_pcm_wait(handle, 100);
		}
        else if (r == -EPIPE)
        {
			xrun();
		}
        else if (r == -ESTRPIPE)
        {
			suspend();
		}
        else if (r < 0)
        {
			DBG("write error: %s", snd_strerror(r));
			return -1;
		}
		if (r > 0)
        {
			result += r;
			count -= r;
			data += r * bitsPerFrame / 8;
		}
	}
	return result;
}

void APlayer::xrun(void)
{
	snd_pcm_status_t *status;
	int res;
	
	snd_pcm_status_alloca(&status);
	if ((res = snd_pcm_status(handle, status))<0)
    {
		DBG("status error: %s\r\n", snd_strerror(res));
		return;
	}

	if (snd_pcm_status_get_state(status) == SND_PCM_STATE_XRUN)
    {
		if ((res = snd_pcm_prepare(handle))<0)
        {
			DBG("xrun: prepare error: %s\r\n", snd_strerror(res));
			return;
		}
		return;		/* ok, data should be accepted again */
	}
    
	DBG("read/write error, state = %s\r\n", snd_pcm_state_name(snd_pcm_status_get_state(status)));
	return;
}

void APlayer::suspend(void)
{
	int res;

	while ((res = snd_pcm_resume(handle)) == -EAGAIN)
		sleep(1);	/* wait until suspend flag is released */

	if (res < 0)
    {
		if ((res = snd_pcm_prepare(handle)) < 0)
        {
			DBG("suspend: prepare error: %s\r\n", snd_strerror(res));
		}
	}
}
