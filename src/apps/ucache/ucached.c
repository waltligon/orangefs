#include "ucached.h"
#include "shmem_util.h"

/* FIFO  */
static int readfd = 0;  /* Command File Descriptor */
static int writefd = 0; /* Response File Descriptor */
static char buffer[BUFF_SIZE]; /* For FIFO reads and writes */

/* Log Globals */
static FILE *ucached_log = (FILE *)0;
/* Time Structures For Log */
static time_t rawtime;
static struct tm * timeinfo;

/* Forward Function Declarations */
static int run_as_child(char c, char *str); /* Run as child of ucached */
static int execute_command(char command, char *str);
static int create_ucache_shmem(void);
static int destroy_ucache_shmem(void);
static void print_to_log(char *str); /* Logs commands and warnings */

/** Runs the command in a child process */
static int run_as_child(char c, char *str)
{
    pid_t pid;
    pid = fork();
    int rc = 0;
    /* Fork Error? */
    if(pid < 0)
    {
        exit(EXIT_FAILURE);
    }
    /* Child Process */
    else if(pid == 0)
    {
        rc = execute_command(c, str);
        if(rc < 0)
        {
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);
    }
    /* Parent Process */
    else 
    {
        wait(&rc);
        if(WIFEXITED(rc))
        {
            if(WEXITSTATUS(rc) != 0)
            {
                return -1;
            }                  
        }
    }
    return rc;
}

static int execute_command(char command, char *str)
{
    int rc = 0;
    switch(command)
    {
        /* Create the shared memory required by the ucache */
        case 'c':
            rc = create_ucache_shmem();
            break;
        /* Destroy the shared memory required by the ucache */
        case 'd':
            rc = destroy_ucache_shmem();
            break;
        /* Close Daemon */
        case 'x':
            unlink(FIFO1);
            unlink(FIFO2);
            rc = 1;   
            mywrite(writefd, "SUCCESS\tExiting ucached", buffer);         
            exit(EXIT_SUCCESS);
            break;
        /* Print */
        case 'p':
            print_to_log(str);
            rc = 1;
            break;
        default:
            rc = -1;
            mywrite(writefd, "FAILURE\tInvalid command character", buffer);
            break;
     }
    return rc;
}

static int create_ucache_shmem(void)
{
    int rc = 0;
    void *ucache = 0;
    void *ucache_locks = 0;

    rc = shmem_init(KEY_FILE, PROJ_ID1, CACHE_SIZE, &ucache); 
    if(rc < 0 || (ucache == (void *)0))
    {
        char buff[128];
        snprintf(buff, 128, "WARNING: ucache shmem_init: errno == %d\t", errno);
        run_as_child('p', buff);
    }

    rc = shmem_init(KEY_FILE, PROJ_ID2, LOCKS_SIZE, &ucache_locks);
    if(rc < 0 || (ucache_locks == (void *)0))
    {

        char buff[128];
        snprintf(buff, 128, "WARNING: ucache_locks shmem_init: errno == %d", 
                                                                       errno);
        run_as_child('p', buff);
    }
    return rc;
}

static int destroy_ucache_shmem(void)
{
    int rc = 0;

    /* Now Remove Them */
    rc = shmem_destroy(KEY_FILE, PROJ_ID1);
    if(rc < 0)
    {
            char buff[128];
            snprintf(buff, 128, "WARNING: ucache shmem_destroy: errno == %d", 
                                                                       errno);
            run_as_child('p', buff);
    }
    rc = shmem_destroy(KEY_FILE, PROJ_ID2);
    if(rc < 0)
    {
            char buff[128];
            snprintf(buff, 128, 
                    "WARNING: ucache_locks shmem_destroy: errno == %d", 
                                                                errno);
            run_as_child('p', buff);
    }
    return rc;
}

static void print_to_log(char *str)
{
    ucached_log = fopen(LOG, "a");
    time(&rawtime);
    timeinfo = localtime(&rawtime);    
    fprintf(ucached_log, "%s\t%s", str, asctime(timeinfo));
    fclose(ucached_log);
}

/** This program should be run as root on startup to initialize the shared 
 * memory segments required by the user cache in PVFS. 
 */
int main(int argc, char **argv)
{
    int rc = 0;

    /* Continue ucached if it's the only ucached */
    char ps_buff1[256];
    char ps_buff2[256];
    FILE *pipe = popen("ps -e | grep -w ucached", "r");
    /* Should catch 1 line result, but not 2 */
    rc = (int)fgets(ps_buff1, 256, pipe);
    rc = (int)fgets(ps_buff2, 256, pipe); /* Should be zero if only 1 ucached */
    if(rc == 0)
    {
        /* Remove old FIFOs in case daemon was killed last time */
        remove(FIFO1);
        remove(FIFO2);
    }
    else
    {  
        puts("FAILURE: Daemon already started");
        puts(ps_buff1);
        puts(ps_buff2);
        exit(EXIT_FAILURE);
    }

    /* Daemonize! */
    daemon(0, 0);

    /* remove previous log file */   
    remove(LOG);

    run_as_child('p', "ucached running...\t\t\t");
    
    /* Start up with shared memory initialized */
    run_as_child('c', NULL);

    /* Create 2 fifos */
    rc = mkfifo(FIFO1, FILE_MODE);
    if((rc < 0) && (errno != EEXIST))
    {
        printf("FIFO1 already exists\n");
    }
    rc = mkfifo(FIFO2, FILE_MODE);
    if((rc < 0) && (errno != EEXIST))
    {
        printf("FIFO1 already exists\n");
    }

    /* Open FIFO for use */
    readfd = open(FIFO1, O_RDONLY); /* For reading commands to daemon */
    writefd = open(FIFO2, O_WRONLY); /* For sending responses to cmd caller */

    char c = 0;
    while(1)
    {
        if(myread(readfd, buffer) > 0)
        {            
            c = buffer[0];
            char buff[128];
            /* Valid Command */
            if(c == 'c' || c == 'd' || c == 'x')
            {
                snprintf(buff, 128, "Command Received: %c\t\t\t", c);
                run_as_child('p', buff);                    
                /* If creating shmem segments, do so in a child process to 
                 * facilitate destruction later on 
                 */
                if(c == 'c')
                {
                    rc = run_as_child(c, NULL);
                    CHECK_RC(rc);
                }
                else
                {
                    rc = execute_command(c, NULL);
                    CHECK_RC(rc);
                }
            }
            /* Invalid Command */
            else
            {
                snprintf(buff, 128, "Invalid Command Received: %c\t\t", c);
                run_as_child('p', buff);  
                rc = -1;
                CHECK_RC(rc);  
            }
            c = 0;
            memset(buffer, 0, BUFF_SIZE);            
        }
    }
    exit(EXIT_SUCCESS);
}

