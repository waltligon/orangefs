#include "ucached.h"

/*
 * s = start ucached
 * c = create shared memory for ucache
 * d = destroy shared memory for ucache
 * x = exit ucached
 */
int main(int argc, char **argv)
{
    if(argc!=2)
    {
        printf("usage: ./ucache_cmd <command char>\n");
        return 0; 
    }

    int rc = 0;

    if(argv[1][0] == 's')
    {
        char ps_buff[256];
        FILE *pipe = popen("ps -e | grep -w ucached", "r");
        rc = (int)fgets(ps_buff, 256, pipe);
        if(rc == 0)
        {
            remove(FIFO1);
            remove(FIFO2);
            /* Crank up the daemon since it's not running */
            rc = system("./ucached");
            puts("SUCCESS: Daemon started");
        }
        else
        {  
            puts("FAILURE: Daemon already started");
            puts(ps_buff);
        }
        return 1;
    }

    char buffer[BUFF_SIZE];

   /* Read and Write File Descriptors */
    int readfd;
    int writefd;

    /* Open FIFOs for use */
    writefd = open(FIFO1, O_WRONLY);
    /* Non-blocking since response could excede 4096 chars */
    readfd = open(FIFO2, O_RDONLY | O_NONBLOCK); 

    /* Send Command to Daemon */
    mywrite(writefd, &argv[1][0], buffer);

    /* Collect Response */
    int count = 0;
    /*  */
    while((count = myread(readfd, buffer)) == -1)
    {
    }
    /* Now output the response until no more chars can be read */
    puts(buffer);
    memset(buffer, 0, BUFF_SIZE);

    while((count = myread(readfd, buffer)) > 0)
    {
        puts(buffer);
        memset(buffer, 0, BUFF_SIZE);
    }

    /* Close FIFO when done */
    close(readfd);
    close(writefd);

    return 1;
}
