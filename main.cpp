#include <stdio.h>
#include <list>
using namespace std;
#include "wav_file.h"
#include "aplayer.h"

static void *play_thread(void *data)
{
    char *filename;
    APlayer *player;

    filename = (char *)data;

    if (strlen(filename) > 0)
    {
        player = new APlayer(false);
        if (player->play(filename) < 0)
        {
            printf("Failed to open file %s\n", filename);
            return NULL;
        }

        while (true)
        {
            usleep(50000); // 50 ms

            if (!player->isRunning())
                break;
        }

        delete player;        
    }

    return NULL;
}

int main(int argc, char *argv[])
{
    int index;
    pthread_t thID;

    char ch;
    
    if (argc < 2)
    {
        printf("usage: %s [filename] \t- open WAV file\n", argv[0]);
        return -1;
    }

    for (index = 0; index < (argc-1); index++)
    {
        pthread_create(&thID, NULL, play_thread, argv[index+1]);
        pthread_detach(thID);
    }

    do 
    {
        printf("press Q key to quit ...");
        ch = getchar();
    } while (ch != 'q' && ch != 'Q');
    
    return 0;
}
