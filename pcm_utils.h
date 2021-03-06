#ifndef _PCM_UTILS_H_
#define _PCM_UTILS_H_

#include <alsa/asoundlib.h>

#ifdef __cplusplus
extern "C" {
#endif

void show_available_sample_formats(snd_pcm_t *handle, snd_pcm_hw_params_t* params);

int dump_memory(const unsigned char *buf, unsigned int size);


#ifdef __cplusplus
}
#endif

#endif
