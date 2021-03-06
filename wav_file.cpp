#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include <alsa/asoundlib.h>
#include "wav_file.h"

typedef struct {
    uint32_t magic;        /* 'RIFF' */
    uint32_t length;       /* filelen */
    uint32_t type;     /* 'WAVE' */
} wav_hdr_t;

typedef struct {
	uint16_t format;		/* see WAV_FMT_* */
	uint16_t channels;
	uint32_t sample_fq;	/* frequence of sample */
	uint32_t byte_p_sec;
	uint16_t byte_p_spl;	/* samplesize; 1 or 2 bytes */
	uint16_t bit_p_spl;	/* 8, 12 or 16 bit */
} wav_fmt_body_t;

typedef struct {
	wav_fmt_body_t format;
	uint16_t ext_size;
	uint16_t bit_p_spl;
	uint32_t channel_mask;
	uint16_t guid_format;	/* WAV_FMT_* */
	uint8_t  guid_tag[14];	/* WAV_GUID_TAG */
} wav_fmt_ext_body;

typedef struct {
	uint32_t type;		/* 'data' */
	uint32_t length;		/* samplecount */
} wav_chnk_hdr_t;


WavFile::WavFile()
    : fp(NULL)
    , bigEndian(false)
    , fmtSize(0)
    , fmtID(0)
    , numChannels(0)
    , sampleRate(0)
    , bytesPerSec(0)
    , blockAlign(0)
    , bitsPerSample(0)
    , bytesPerSample(0)
    , numData(0)
{
}

WavFile::~WavFile()
{
    close();
}

int WavFile::open(const char *filename)
{
    wav_hdr_t hdr;
    wav_chnk_hdr_t chnk_hdr;
    wav_fmt_body_t fmt_body;
    wav_fmt_ext_body fmt_ext_body;

    uint32_t chnk_type;
    int bytes, length;

    if (strlen(filename) <= 0)
        return -1;
        
    fp = fopen(filename, "rb");
    if (fp == NULL)
        return -1;

    // read hdr
    bytes = safeRead(&hdr, sizeof(hdr));
    if (bytes < sizeof(hdr))
        return -1;

	if (hdr.magic == WAV_RIFF)
		bigEndian = false;
	else if (hdr.magic == WAV_RIFX)
		bigEndian = true;
    else
        return -1;

    if (hdr.type != WAV_WAVE)
        return -1;

    fprintf(stdout, "WAV Length %u.\n", (TO_CPU_INT(hdr.length, bigEndian) + 8));

    //  read chunk hdr
    while (true)
    {
        bytes = safeRead(&chnk_hdr, sizeof(chnk_hdr));
        if (bytes < sizeof(chnk_hdr))
            return -1;

        chnk_type = chnk_hdr.type;
        fmtSize = TO_CPU_INT(chnk_hdr.length, bigEndian);
        if (chnk_type == WAV_FMT)
            break;
    }

    fmtSize += fmtSize % 2;
    if (fmtSize < sizeof(wav_fmt_body_t))
    {
        fprintf(stderr, "unknown length of 'fmt ' chunk (read %u, should be %u at least)",
		      fmtSize, (uint32_t)sizeof(wav_fmt_body_t));
        return -1;
    }

    bytes = safeRead(&fmt_body, sizeof(fmt_body));
    if (bytes < sizeof(fmt_body))
        return -1;

    fmtID = TO_CPU_SHORT(fmt_body.format, bigEndian);
    if (fmtID == WAV_FMT_EXTENSIBLE)
    {
        fmt_ext_body.format = fmt_body;
        bytes = safeRead(&fmt_ext_body.ext_size, sizeof(fmt_ext_body) - sizeof(wav_fmt_body_t));
        if (bytes < sizeof(fmt_ext_body) - sizeof(wav_fmt_body_t))
            return -1;

        fmtID = TO_CPU_SHORT(fmt_ext_body.guid_format, bigEndian);
    }

    if (fmtID != WAV_FMT_PCM && fmtID != WAV_FMT_IEEE_FLOAT)
    {
        fprintf(stderr, "can't play WAVE-file format 0x%04x which is not PCM or FLOAT encoded", fmtID);
        return -1;
    }

    numChannels = TO_CPU_SHORT(fmt_body.channels, bigEndian);
    if (numChannels < 1)
    {
        fprintf(stderr, "can't play WAVE-files with %u tracks", numChannels);
        return -1;
    }

    sampleRate = TO_CPU_SHORT(fmt_body.sample_fq, bigEndian);
    bytesPerSec = TO_CPU_SHORT(fmt_body.byte_p_sec, bigEndian);
    bitsPerSample = TO_CPU_SHORT(fmt_body.bit_p_spl, bigEndian);
    bytesPerSample = bitsPerSample / 8;
    blockAlign = bytesPerSample * numChannels;

    while (true)
    {
        bytes = safeRead(&chnk_hdr, sizeof(chnk_hdr));
        length = TO_CPU_INT(chnk_hdr.length, bigEndian);
        if (chnk_hdr.type == WAV_DATA)
        {
            numData = length;
            break;
        }
        else if (length > 0)
            fseek(fp, length, SEEK_CUR);
    }

    return 0;
}

int WavFile::safeRead(void *buffer, size_t bytes)
{
    size_t reads, offset = 0, total = bytes;

    assert(fp != NULL);
    while (total > 0)
    {
        reads = fread((uint8_t *)buffer + offset, 1, total, fp);

        offset += reads;
        total -= reads;
        
        if (feof(fp))
        {
            fprintf(stderr, "End of file now.\n");
            break;
        }
        else if (ferror(fp))
        {
            fprintf(stderr, "ferror(fp) = %d", ferror(fp));
            break;        
        }
    }

    return offset;
}

int WavFile::readData(char *buf, int bufSize)
{
    if (bufSize % blockAlign)
        bufSize = (bufSize / blockAlign) * blockAlign;

    return safeRead(buf, bufSize);
}

void WavFile::dumpInfo()
{
    fprintf(stdout, "Format:\t %u\r\n", fmtID);
    fprintf(stdout, "Bits:\t %u\r\n", bitsPerSample);
    fprintf(stdout, "Rate:\t %u Hz\r\n", sampleRate);
    if (numChannels == 1)
        fprintf(stdout, "Mono\r\n");
    else if (numChannels == 2)
        fprintf(stdout, "Stereo\r\n");

    fprintf(stdout, "%u seconds\r\n", numData/bytesPerSec);
}

void WavFile::close()
{
    if (fp)
    {
        fclose(fp);
        fp = NULL;
    }
}
