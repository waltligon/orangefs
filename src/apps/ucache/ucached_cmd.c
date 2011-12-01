
#include <usrint.h>
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
        printf("usage: ucache_cmd <command char>\n");
        return 0; 
    }

    int rc = 0;
    void *rp;

    if(argv[1][0] == 's')
    {
        char ps_buff[256];
        FILE *pipe = popen("ps -e | grep -w ucached", "r");
        rp = fgets(ps_buff, 256, pipe);
        if(rp == NULL)
        {
            rc = remove(FIFO1);
            rc = remove(FIFO2);
            /* Crank up the daemon since it's not running */
            rc = system("ucached");
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

    if(writefd == -1)
    {
        perror("cmd main opening writefd"); 
        return -1;       
    }

    /* Send Command to Daemon */
    strcpy(buffer, &argv[1][0]);
    rc = write(writefd, buffer, BUFF_SIZE);
    if(rc == -1)
    {
        perror("writing cmd");
    }

    /* TODO: what about large responses. chunk data? */
    readfd = open(FIFO2, O_RDONLY); 

    /* Collect Response */
    int count = read(readfd, buffer, BUFF_SIZE);
    while(count > 0 || ((count == -1) && (errno == EINTR)))
    {
        puts(buffer);
        memset(buffer, 0, BUFF_SIZE);
        count = read(readfd, buffer, BUFF_SIZE);
    }
    
    /* Close FIFO when done */
    close(readfd);
    close(writefd);

    return 1;
}
