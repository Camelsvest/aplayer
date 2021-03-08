#ifndef _WAV_FILE_H_
#define _WAV_FILE_H_

#include <stdint.h>
#include <stdio.h>
#include <endian.h>
#include <byteswap.h>

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define COMPOSE_ID(a,b,c,d)	((a) | ((b)<<8) | ((c)<<16) | ((d)<<24))
#define LE_SHORT(v)		(v)
#define LE_INT(v)		(v)
#define BE_SHORT(v)		bswap_16(v)
#define BE_INT(v)		bswap_32(v)
#elif __BYTE_ORDER == __BIG_ENDIAN
#define COMPOSE_ID(a,b,c,d)	((d) | ((c)<<8) | ((b)<<16) | ((a)<<24))
#define LE_SHORT(v)		bswap_16(v)
#define LE_INT(v)		bswap_32(v)
#define BE_SHORT(v)		(v)
#define BE_INT(v)		(v)
#else
#error "Wrong endian"
#endif

/* Note: the following macros evaluate the parameter v twice */
#define TO_CPU_SHORT(v, be) \
	((be) ? BE_SHORT(v) : LE_SHORT(v))
#define TO_CPU_INT(v, be) \
	((be) ? BE_INT(v) : LE_INT(v))

#define COMPOSE(a, b, c, d)		((a) | ((b)<<8) | ((c)<<16) | ((d)<<24))
#define WAV_RIFF			COMPOSE('R', 'I', 'F', 'F')
#define WAV_RIFX		    COMPOSE('R', 'I', 'F', 'X')
#define WAV_WAVE			COMPOSE('W', 'A', 'V', 'E')
#define WAV_FMT				COMPOSE('f', 'm', 't', ' ')
#define WAV_DATA			COMPOSE('d', 'a', 't', 'a')
#define WAV_FORMAT_PCM			1	/* PCM WAVE file encoding */

/* WAVE fmt block constants from Microsoft mmreg.h header */
#define WAV_FMT_PCM             0x0001
#define WAV_FMT_IEEE_FLOAT      0x0003
#define WAV_FMT_DOLBY_AC3_SPDIF 0x0092
#define WAV_FMT_EXTENSIBLE      0xfffe


class WavFile
{
public:
    WavFile();
    virtual ~WavFile();

    int open(const char *filename);
	int readData(char *buf, int bufSize);
	void close();

	int format() { return fmtID; }
	int channels() { return numChannels; }
	int rate() { return sampleRate; }
	int bits() { return bitsPerSample; }
	int bytes() { return bytesPerSample; }
	bool isBigEndian() { return bigEndian; }
	int length() { return numData; }

    void dumpInfo();

private:
    size_t safeRead(void *buffer, size_t bytes);
    
    FILE *fp;
	
	bool bigEndian;

	uint32_t fmtSize;
	uint16_t fmtID;
	uint16_t numChannels;
	uint32_t sampleRate;
	uint32_t bytesPerSec;
	uint16_t blockAlign;//(bytes per sample)*(channels)
	uint16_t bitsPerSample;
	uint16_t bytesPerSample;
	uint32_t numData;
};

#endif
