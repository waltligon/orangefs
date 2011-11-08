
#include <usrint.h>
#include "ucached.h"

/* FIFO  */
static int readfd = 0;  /* Command File Descriptor */
static int writefd = 0; /* Response File Descriptor */
static char buffer[BUFF_SIZE]; /* For FIFO reads and writes */
char buff[LOG_LEN];

/* Log Globals */
static FILE *ucached_log = (FILE *)0;
/* Time Structures For Log 
static time_t rawtime;
static struct tm * timeinfo;
*/

/* Booleans */
/* 1 if ucache is available for use */
static unsigned char ucache_avail = 0;
/* Set this to one if the ucache doesn't get created, and the 
 * create_ucache_shmem function should be run again. 
 */
//static unsigned char tryAgain = 0;

/* Use this global to determine if the atexit registered function (clean_up)
 * needs to run. A child process is created to create shmem. This facilitates
 * destruction later on, since segments hang around until their creator exits. 
 */
pid_t pid = -1;

/* Hung Lock Detection */
time_t locked_time[BLOCKS_IN_CACHE+1];

/* Forward Function Declarations */
static int run_as_child(char c); /* Run as child of ucached */
static int execute_cmd(char command);
static int create_ucache_shmem(void);
static int destroy_ucache_shmem(char dest_locks, char dest_ucache);
//static void print_to_log(char *str); /* Logs commands and warnings */
static void clean_up(void);
static int ucached_lockchk(void);

void check_rc(int rc)
{
    memset(buffer, 0, BUFF_SIZE);
    if(rc >= 0)
    {
        strcpy(buffer, "SUCCESS");
    }
    else
    {
        strcpy(buffer, "FAILURE: check log: /tmp/ucached.log");
    }
}

/** Function to be run upon successful termination from an exit call */
static void clean_up(void)
{
    int rc = 0;
    /* Only the parent process should execute these lines.
     * Must check the pid since the atexit function registered 
     * clean_up. This registration is passed on to any child
     * processes forked off of the parent. We don't want to execute
     * these lines when any of the children exit. Run only when parent.
     */
    if(pid !=0)
    {
        if(DEST_AT_EXIT)
        {
            rc = destroy_ucache_shmem(1, 1);
        }
        fprintf(ucached_log, "ucached exiting...PID=%d\n", pid);
        rc = fclose(ucached_log);
        rc = unlink(FIFO1);
        rc = unlink(FIFO2);
    }
}

/** Checks ucache lock shmem region for hung locks. 
 * Returns 0 when no hung locks are detected. 
 * Returns 1 when 1 or more hung locks are detected and all are gracefully
 * handled. 
 * Returns -1 when 1 or more  hung locks are detected and couldn't
 * be handled properly. (error) 
 */
static int ucached_lockchk(void)
{
    int rc = 0;
    int i;
    for(i = 0; i < (BLOCKS_IN_CACHE + 1); i++)
    {
        ucache_lock_t * currlock = get_lock((uint16_t)i);
        if(lock_trylock(currlock) == 0)
        {
            /* Lock wasn't held, so set the timer to zero for this lock */
            locked_time[i] = 0;        
        }
        else
        {
            /* Lock was held, so calculate if lock timeout has occured */
            /* First check to see if this lock's timer has been set at all */
            if(!locked_time[i])
            {
                /* Timer for this lock isn't currently set */
                time(&locked_time[i]);
                continue;
            }
            else
            {
                /* Timer was previously set meaning the block had been locked*/
                double time_diff = difftime(time((time_t *)NULL), locked_time[i]); 
                if((int)time_diff >= BLOCK_LOCK_TIMEOUT)
                {
                    /*
                    fprintf(ucached_log, "HUNG LOCK DETECTED @ block index = "
                                                                   "%d!\n", i);
                    TODO: what to do with hung locks?
                    rc = pick_lock(ucache_lock_t * currlock);
                    if(rc == 1)
                    {
                        locked_time[i] = (time_t)0;
                    }
                    */
                }
            }
            fprintf(ucached_log, "%d\n", i);
            fflush(ucached_log);
        }
    } 

    return rc;
}


/** Runs the command in a child process */ 
static int run_as_child(char c)
{
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
        rc = execute_cmd(c);
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


static int execute_cmd(char cmd)
{
    int rc = 0;
    switch(cmd)
    {
        /* Create the shared memory required by the ucache */
        case 'c':
            rc = create_ucache_shmem();
            break;
        /* Destroy the shared memory required by the ucache */
        case 'd':
            rc = destroy_ucache_shmem(1, 1);
            break;
        /* Close Daemon */
        case 'x': 
            writefd = open(FIFO2, O_WRONLY); 
            rc = write(writefd, "SUCCESS\tExiting ucached", BUFF_SIZE);
            while(rc <= 0)
            {
                rc = write(writefd, "SUCCESS\tExiting ucached", BUFF_SIZE);
            }
            close(writefd);
            exit(EXIT_SUCCESS);
            break;
        default:
            strcpy(buffer, "FAILURE\tInvalid command character");
            break;
     }
    return rc;
}

/* Returns -1 on failure, 1 on success */
static int create_ucache_shmem(void)
{
    int rc = 0;

    int old_locks_present = 0;

    /* attempt setup of shmem region for locks (inlcude SYSV later? */
    int id = SHM_ID1;
    key_t key = ftok(KEY_FILE, id);
    size_t size = LOCKS_SIZE;
    int shmflg = SVSHM_MODE;
    int lock_shmid = shmget(key, size, shmflg);

    if(lock_shmid == -1)
    {
        fprintf(ucached_log, "INFO: shmet on lock_shmid returned -1"
                                                    " first try\n");

        /* Shared memory segment used for locks was not previosly created, 
         * so create it.
         */
        shmflg = shmflg | IPC_CREAT | IPC_EXCL;
        lock_shmid = shmget(key, size, shmflg);
        if(lock_shmid == -1)
        {
            fprintf(ucached_log, "ERROR: shmget (IPC_CREATE, IPC_EXCL)"
                                       " on lock_shmid returned -1\n");
            /* Couldn't create the required segment */
            return -1;
        }
        else
        {
            fprintf(ucached_log, "INFO: shmget (using IPC_CREATE, IPC_EXCL)"
                        " on lock_shmid returned shmid = %d\n", lock_shmid);

            /* Attach to shmem and initialize all the locks */
            shmflg = 0;
            /* ucache_locks is defined in src/client/usrint/ucache.h */
            ucache_locks = shmat(lock_shmid, NULL, shmflg);
            if (!ucache_locks)
            {
                fprintf(ucached_log, "ERROR: shmat on lock_shmid returned"
                                                               " NULL\n");
                return -1;
            }

            int i;
            /* Initialize Block Level Locks */
            for(i = 0; i < (BLOCKS_IN_CACHE + 1); i++)
            {
                rc = lock_init(get_lock(i));
                if (rc == -1)
                {
                    fprintf(ucached_log, "ERROR: lock_init returned -1 @"
                                                " lock index = %d\n", i);
                    rc = -1;
                }
            }
        }    
    }
    else
    {
        fprintf(ucached_log, "INFO: first shmget on lock_shmid found segment"
                                               ": shmid = %d\n", lock_shmid);
        old_locks_present = 1;
        /* Shmem for locks was already created, so just attach to it */
        shmflg = 0;
        ucache_locks = shmat(lock_shmid, NULL, shmflg);
        if (!ucache_locks)
        {
            fprintf(ucached_log, "ERROR: shmat on lock_shmid returned NULL\n");
            return -1;
        }    
    }

    /* At this point all the locks should be aquired and initialized.
     * They could also be locked or unlocked */

    /* Set the global lock point to the address of the last lock in the locks
     * shmem segment. Then lock it.
     */
    ucache_lock = get_lock(BLOCKS_IN_CACHE);
    lock_lock(ucache_lock);

    fprintf(ucached_log, "INFO: lock segment successfully retrieved and"
                                              " global lock locked.\n");

    /* Try to get/create the shmem required for the ucache */
    id = SHM_ID2;
    key = ftok(KEY_FILE, id);
    size = CACHE_SIZE;
    shmflg = SVSHM_MODE;
    int ucache_shmid = shmget(key, size, shmflg);
    
    if(ucache_shmid == -1)
    {
        fprintf(ucached_log, "INFO: shgmet on ucache_shmid returned -1"
                                                       " first try\n");

        /* Remember if there was an old lock region detected */
        if(old_locks_present)
        {
            fprintf(ucached_log, "INFO: old locks discovered, attempting"
                             " destruction of old locks and starting\n");

            /* Destroy old lock region and start function over */
            rc = shmctl(lock_shmid, IPC_RMID, (struct shmid_ds *) NULL);

            /* Let this child process exit, since exiting is required to get
             * the shmem segment to be completely removed. Try to create the 
             * shmem again later in another child process. 
             */
            return -1; 
        }

        /* Shared memory segmet used for ucache was not previosly created, 
         * so create it.
         */
        shmflg = shmflg | IPC_CREAT | IPC_EXCL;
        ucache_shmid = shmget(key, size, shmflg);
        if(ucache_shmid == -1)
        { 
            /* Couldn't create the required segment */
            fprintf(ucached_log, "ERROR: shmget (using IPC_CREATE, IPC_EXCL)"
                                           " on ucache_shmid returned -1\n");
            rc = -1;
            goto errout;
        }
        else
        {
            fprintf(ucached_log, "INFO: shmget (using IPC_CREATE, IPC_EXCL)"
                    " on ucache_shmid returned shmid = %d\n", ucache_shmid);


            /* Attach to the ucache shmem region */
            shmflg = 0;
            /* ucache is defined in src/client/usrint/ucache.h */
            ucache = shmat(ucache_shmid, NULL, shmflg);
            if (!ucache)
            {
                fprintf(ucached_log, "ERROR: shmat on ucache_shmid returned"
                                                                 " NULL\n");
                rc = -1;
                goto errout;
            }
  
            /* Initialize the file table */
            rc = ucache_init_file_table(0);
            if(rc != 0)
            {
                fprintf(ucached_log, "ERROR: file table initialization"
                                                          " failed\n");
                /* Couldn't Initialize File Table */
                rc = -1;
                goto errout;
            }
        }
    }
    else
    {
        fprintf(ucached_log, "INFO: first shmget on ucache_shmid found segment"
                                               ": shmid = %d\n", ucache_shmid);

        /* Previously created ucache segment present. Need more info. */
        /* See if marked for deletion, but has users attached still */
        struct shmid_ds buf;     
        int cmd = IPC_STAT;
        rc = shmctl(ucache_shmid, cmd, &buf);
        if(rc == -1)
        {
            fprintf(ucached_log, "ERROR: shmctl failed to IPC_STAT,"
                                                 " ucache_shmid\n");
            goto errout;
        } 

        /* Determine the count of processes attached to this shm segment */
        char hasAttached = (buf.shm_nattch > 0);

        /* Determine if the ucache shmem segment is marked for destruction*/
        uint16_t currentMode = buf.shm_perm.mode;
        char markedForDest = ((currentMode & SHM_DEST) == SHM_DEST);

        if(markedForDest && hasAttached)
        {
            fprintf(ucached_log, "INFO: detected previous ucache shmem segment"
                                       " marked for destruction that still has" 
                                   " one or more processes attached to it.\n");

            shmflg = shmflg | IPC_CREAT; /* Note: CREAT w/o EXCL */
            ucache_shmid = shmget(key, size, shmflg);
            if(ucache_shmid == -1)
            {
                /* Couldn't create the required segment */
                fprintf(ucached_log, "ERROR: shmget (using IPC_CREAT && !EXCL)"
                                             " on ucache_shmid returned -1\n");
                rc = -1;
                goto errout;
            }
            /* Attach to the ucache shmem region */
            shmflg = 0;
            /* ucache is defined in src/client/usrint/ucache.h */
            ucache = shmat(ucache_shmid, NULL, shmflg);
            if (!ucache)
            {
                fprintf(ucached_log, "ERROR: shmat on ucache_shmid returned"
                                                                  " NULL\n");
                rc = -1;
                goto errout;
            }

            /* Initialize the ftbl, and force the creation of it 
             * since the init boolean is set to 1.
             */
            rc = ucache_init_file_table(1); 
            if(rc != 0)
            {
                /* Couldn't Initialize File Table */
                fprintf(ucached_log, "ERROR: file table initialization"
                                                          " failed\n");
                rc = -1;    
                goto errout;
            }
        }
        else
        {
            /* Asume we will keep using the previously allocated segment */
            /* Attach to the ucache shmem region */
            shmflg = 0;
            /* ucache is defined in src/client/usrint/ucache.h */
            ucache = shmat(ucache_shmid, NULL, shmflg);
            if (!ucache)
            {
                fprintf(ucached_log, "ERROR: shmat on ucache_shmid returned"
                                                                 " NULL\n");
                rc = -1;
                goto errout;
            }
        }
    }

    lock_unlock(ucache_lock);
    return 1;

errout:
    lock_unlock(ucache_lock);
    return rc;
}

static int destroy_ucache_shmem(char dest_locks, char dest_ucache)
{
    int rc = 0;
    /* Aquire the main lock then attempt to destroy the ucache shmem segment */
    if(ucache_lock)
    {
        lock_lock(ucache_lock);
    }

    if(dest_ucache)
    {
        fprintf(ucached_log, "INFO: destroying ucache shmem\n");

        /* Destroy shmem segment containing ucache */
        int id = SHM_ID2;
        key_t key = ftok(KEY_FILE, id);
        int shmflg = SVSHM_MODE;
        int ucache_shmid = shmget(key, 0, shmflg);
        if(ucache_shmid == -1)
        {
            fprintf(ucached_log, "ERROR: shmget on ucache_shmid returned"
                                                                " -1\n");
            return -1;
        }
        rc = shmctl(ucache_shmid, IPC_RMID, (struct shmid_ds *) NULL);
        if(rc == -1)
        {
            fprintf(ucached_log, "WARNING: ucache shmem_destroy: errno"
                                                    " == %d\n", errno);
        }
    }

    if(dest_locks)
    {
        fprintf(ucached_log, "INFO: destroying locks' shmem\n");

        /* Destroy shmem segment containing locks */
        int id = SHM_ID1;
        key_t key = ftok(KEY_FILE, id);
        int shmflg = SVSHM_MODE;
        int lock_shmid = shmget(key, 0, shmflg);
        if(lock_shmid == -1)
        {
            fprintf(ucached_log, "ERROR: shmget on lock_shmid returned -1\n");
            return -1;
        }
        rc = shmctl(lock_shmid, IPC_RMID, (struct shmid_ds *) NULL);
        if(rc == -1)
        {
            fprintf(ucached_log,
                    "WARNING: ucache_locks shmem_destroy: errno"
                                             " == %d\n", errno);
        }
    }

    fprintf(ucached_log, "INFO: both shmem segments marked for"
                                            " destruction.\n");
    return rc;
}

/*
static void print_to_log(char *str)
    ucached_log = fopen(LOG, "a");
    time(&rawtime);
    timeinfo = localtime(&rawtime);    
    if(LOG_TIMESTAMP)
    {
        fprintf(ucached_log, "%s\t%s", str, asctime(timeinfo));
    }
    else
    {
        fprintf(ucached_log, "%s\n", str);
    }
    fclose(ucached_log);
}
*/

/** This program should be run as root on startup to initialize the shared 
 * memory segments required by the user cache in PVFS. 
 */
int main(int argc, char **argv)
{
    int rc = 0; 
    void *rp;
    memset(locked_time, 0, (sizeof(time_t) * (BLOCKS_IN_CACHE + 1)));

    /* Direct output of ucache library, TODO: change this later */
    if (!out)
    {
        out = stdout;
    }

    /* Continue ucached if it's the only ucached */
    char ps_buff1[256];
    char ps_buff2[256];
    FILE *pipe = popen("ps -e | grep -w ucached", "r");

    /* Should catch 1 line result, but not 2 */
    rp = fgets(ps_buff1, 256, pipe);
    rp = fgets(ps_buff2, 256, pipe); /* Should be zero if only 1 ucached */
    if(rp == NULL)
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
    rc = daemon(1, 1);

    if(rc != 0)
    {
        perror("daemon-izing failed");
        exit(EXIT_FAILURE);
    }

    ucached_log = fopen(LOG, "w");

    if(!ucached_log)
    {
        exit(EXIT_FAILURE);
    }

    fprintf(ucached_log, "ucached running...\n");    
    fflush(ucached_log);

    /* Start up with shared memory initialized */
    if(CREATE_AT_START)
    {
        run_as_child('c');
        atexit(clean_up);
    }

    /* Create 2 fifos */
    rc = mkfifo(FIFO1, FILE_MODE);
    if(rc != 0)
    {
        /* Couldn't create FIFO */
        return -1;
    }
    rc = mkfifo(FIFO2, FILE_MODE);
    if(rc != 0)
    {
        /* Couldn't create FIFO */
        return -1;
    }

    while(1)
    {
        readfd = open(FIFO1, O_RDONLY | O_NONBLOCK);
        struct pollfd fds[1];
        fds[0].fd = readfd;
        fds[0].events = POLLIN;

        rc = poll(fds, 1, FIFO_TIMEOUT * 1000); 

        if(rc == -1)
        {
            fprintf(ucached_log, "ERROR: poll: errno = %d\n", errno);
        }
        /*
        if(rc == 0)
        {
            //Timeout occured, no descriptors ready
            fprintf(ucached_log, "nothing to read/write after %d seconds\n", 
                                                          FIFO_TIMEOUT);            
        }
        */

        /*
        if(rc > 0)
        {
            fprintf(ucached_log, "poll found descriptors to work on\n");
        }
        */

        if(fds[0].revents & POLLIN)
        {
            /* Data to be read */
            memset(buffer, 0, BUFF_SIZE);
            int count = read(readfd, buffer, BUFF_SIZE);
            while(count <= 0)
            {
                if(count == -1)
                {
                    fprintf(ucached_log, "caught error while trying to read"
                                               " cmd: errno = %d\n", errno);
                }
                /* Try to read again */ 
                count = read(readfd, buffer, BUFF_SIZE);
            } 
            if(count > 0)
            {
                /* Data read into buffer*/ 
                char c = buffer[0];
                /* Valid Command? */
                if(c == 'c' || c == 'd' || c == 'x')
                {
                    fprintf(ucached_log, "Command Received: %c\n", c);
                    fflush(ucached_log);
                    /* Run creation in child process */
                    if(c == 'c')
                    {
                        run_as_child(c);
                    }
                    else
                    {
                        rc = execute_cmd(c);
                    }
                    check_rc(rc);                  
                }
                /* Invalid Command */
                else
                {
                    fprintf(ucached_log, "Invalid Command Received: %c\n", c);
                    fflush(ucached_log);
                    rc = -1;
                    check_rc(rc); 
                }

                /* Data can be written, not guaranteed anything to write */
                int responseLength = 0;
                if((responseLength = strlen(buffer)) != 0)
                {
                    writefd = open(FIFO2, O_WRONLY);
                    if(writefd == -1)
                    {
                        fprintf(ucached_log, "opening write FIFO: errno = %d\n", errno);
                    }
                    rc = write(writefd, buffer, BUFF_SIZE);
                    while(rc <= 0)
                    {
                        rc = write(writefd, buffer, BUFF_SIZE);
                    }
                    memset(buffer, 0, BUFF_SIZE);
                    close(writefd);
                }
            }
        }
        fflush(ucached_log);
        close(readfd);

        if(ucache_avail)
        {
            /* Gather stats */
            /* TODO: which stats? */

            /* Write some dirty blocks out */
            /* TODO: create function to do this or do i already have one that will suffice? */

            /* Check for hung locks */
            rc = ucached_lockchk();
        }
    }
}
