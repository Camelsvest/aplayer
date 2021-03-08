#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pcm_utils.h"

#define MAX_LINE_LENGTH 256
#define MAX_COLUMNS 32


void show_available_sample_formats(snd_pcm_t *handle, snd_pcm_hw_params_t* params)
{
	int format;

	fprintf(stdout, "Available formats:\n");
	for (format = 0; format <= SND_PCM_FORMAT_LAST; format++)
	{
		if (snd_pcm_hw_params_test_format(handle, params, (snd_pcm_format_t)format) == 0)
			fprintf(stdout, "- %s\n", snd_pcm_format_name((snd_pcm_format_t)format));
	}
}

int dump_memory(const unsigned char *buf, unsigned int size)
{
    int             bytes, i;
    unsigned char   ch, *pos;
    char            line[MAX_LINE_LENGTH];

    printf("Dump address:%p, %u bytes\r\n", buf, size);
    
    i = 0;
    pos = (unsigned char *)buf;

    // print the address we are pulling from
    bytes = snprintf(line, sizeof(line), "%08X | ", (int)buf);
    assert(bytes > 0 && bytes < sizeof(line));
    
    while (size-- > 0)
    {
        // print each char
        bytes += snprintf(line + bytes, sizeof(line) - bytes, "%02X ", *buf++);

        if (!(++i % MAX_COLUMNS) || (size == 0 && i % MAX_COLUMNS))
        {
    		// if we come to the end of a line...
       
    		// if this is the last line, print some fillers.
    		if (size == 0)
    		{
    			while (i++ % MAX_COLUMNS)
    			{ 
    				bytes += snprintf(line + bytes, sizeof(line) - bytes, "__ ");
    			}
    		}

            bytes += snprintf(line + bytes, sizeof(line) - bytes, "| ");
    		while (pos < buf) // print the character version
    		{  
    			ch = *pos++;
                bytes += snprintf(line + bytes, sizeof(line) - bytes, "%c", (ch < 33 || ch > 126) ? 0x2E : ch);
    		}

    		// If we are not on the last line, prefix the next line with the address.
    		if (size > 0)
    		{
                puts(line);
                
                bytes = snprintf(line, sizeof(line), "%08X | ", (int)buf);
    		}                        
        }                
    }

    puts(line);
    
    return bytes;
}

