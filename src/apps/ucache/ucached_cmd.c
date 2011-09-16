#include "ucached.h"

/*
 * c = create shared memory for ucache
 * d = destroy shared memory for ucache
 * x = exit ucached
 */
int main(int argc, char **argv)
{
    if(argc!=2)
    {
        printf("usage: ./ucache_cmd <command>\n");
        return 0; 
    }

    int rc = 0;

    if(argv[1][0] == 's')
    {
        /* Start ucached if not already started */
        if(open(FIFO1, O_WRONLY) == -1)
        {
            /* Crank up the daemon since it's not running */
            
            puts("SUCCESS: Daemon started");
            rc = system("./ucached");
        }
        else
        {
            puts("FAILURE: Daemon already started");
        }
        return 1;
    }

    char buffer[BUFF_SIZE];

   /* Read and Write File Descriptors */
    int readfd;
    int writefd;

    /* Open FIFOs for use */
    writefd = open(FIFO1, O_WRONLY);
    readfd = open(FIFO2, O_RDONLY);

    /* Send Command to Daemon */
    mywrite(writefd, &argv[1][0], buffer);

    /* Collect Response */
    myread(readfd, buffer);
    puts(buffer);

    /* Close FIFO when done */
    close(readfd);
    close(writefd);

    return 1;
}
