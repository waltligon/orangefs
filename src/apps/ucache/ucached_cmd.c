#include <stdio.h>
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
    if(argc < 2 || argc > 3)
    {
        printf("usage: ucache_cmd <command char>\n");
        return 0; 
    }

    int rc = 0;
    //void *rp;

    char this_cmd = argv[1][0];
    if(this_cmd == 's')
    {
        /*
        char ps_buff[256];
        FILE *pipe = popen("ps -e | grep -w ucached", "r");
        rp = fgets(ps_buff, 256, pipe);
        if(rp == NULL)
        {
            rc = remove(FIFO1);
            rc = remove(FIFO2);
            // Crank up the daemon since it's not running 
            rc = system("ucached");
            puts("SUCCESS: Daemon started");
        }
        */
        /* Look in /tmp for ucached.started indicating the ucached has been started */
        FILE *ucached_started = fopen(UCACHED_STARTED, "r");
        if(!ucached_started)
        {
            rc = remove(FIFO1);
            rc = remove(FIFO2);
            // Crank up the daemon since it's not running 
            rc = system("ucached");
            ucached_started = fopen(UCACHED_STARTED, "w");
            if(ucached_started)
                fclose(ucached_started);
            puts("SUCCESS: Daemon started");
        }
        else
        {  
            puts("FAILURE: Daemon already started");
            //puts(ps_buff);
        }
        return 1;
    }

    char buffer[BUFF_SIZE];
    memset(buffer, 0, BUFF_SIZE);

   /* Read and Write File Descriptors */
    int readfd;
    int writefd;

    /* Open FIFOs for use */
    writefd = open(FIFO1, O_WRONLY);

    if(writefd == -1)
    {
        perror("ucached_cmd couldn't open writefd"); 
        return -1;       
    }

    /* Send Command to Daemon */
    buffer[0] = this_cmd;
    if(argc == 3)
    {   
        strcat(buffer, " ");
        strcat(buffer, argv[2]);
    }
    rc = write(writefd, buffer, BUFF_SIZE);
    if(rc == -1)
    {
        perror("Error occured during write to ucached");
    }

    memset(buffer, 0, BUFF_SIZE);
    readfd = open(FIFO2, O_RDONLY); 

    /* Collect Response */
    int count = read(readfd, buffer, BUFF_SIZE);
    while(count > 0 || ((count == -1) && (errno == EINTR)))
    {
        //if(count)
        //    printf("read: %d\n", count);
        //buffer[count] = 0;
        fputs(buffer, stdout);
        if(strlen(buffer) < BUFF_SIZE)
        {
            //printf("strlen = %d\n", strlen(buffer));
            break;
        }
        memset(buffer, 0, BUFF_SIZE);
        count = read(readfd, buffer, BUFF_SIZE);
    }
    printf("\n");
    /* Close FIFO when done */
    close(readfd);
    close(writefd);

    if(this_cmd == 'i')
    {
        memset(buffer, 0, BUFF_SIZE);
        FILE *info = fopen(UCACHED_INFO_FILE, "r");
        /*
        while(!info)
        {
            info = fopen(UCACHED_INFO_FILE, "r");
        }*/
        if(!info)
        {
            perror("UCACHED_INFO_FILE");
        }
        while(fread(buffer, sizeof(char), BUFF_SIZE - 1, info) > 0)
        {
            buffer[strlen(buffer)] = 0;
            printf("%s", buffer);
            memset(buffer, 0, BUFF_SIZE);
        }
        fclose(info);
    }
    return 1;
}

