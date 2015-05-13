/*
 * (C) 2004 Clemson University and The University of Chicago
 * 
 * See COPYING in top-level directory.
 *
 * 03/19/07 - Added set and get for user.pvfs2.mirror.mode and ..mirror.copies.
 *            Added get for user.pvfs2.mirror.handles and ..mirror.status
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <getopt.h>

#include "orange.h"

#if 0
#define __PINT_REQPROTO_ENCODE_FUNCS_C
#include "str-utils.h"
#include "pint-sysint-utils.h"
#include "pint-util.h"
#include "pvfs2-internal.h"
#include "pvfs2-req-proto.h"
#endif

#include "xattr-utils.h"

#define VALBUFSZ 1024

/* extended attribute name spaces supported in PVFS2 */
const char *PINT_eattr_namespaces[] =
{
    "system.",
    "user.",
    "trusted.",
    "security.",
    NULL
};

typedef enum {
    NONE = 0,
    GET,
    SET,
    DEL,
    LST,
    TXT,
    ATM,
    FAA
} eattr_op_t;;

/* optional parameters, filled in by parse_args() */
struct options
{
    char* srcfile;
    eattr_op_t op;
    char *key;
    int ksize;
    void *val;
    int vsize;
    void *cmp;
    int csize;
    void *resp;
    int rsize;
    int opcode;
    int nflg;
};

enum object_type
{ 
    UNIX_FILE, 
    PVFS2_FILE 
};

static struct options* parse_args(int argc, char* argv[]);
static void usage(int argc, char** argv);
static int permit_set(char *key_p);
static int eattr_is_prefixed(char* key_name);

uint32_t current_meta_hint={0};

int main(int argc, char **argv)
{
    int ret = 0;
    struct options* user_opts = NULL;
    int fd;

    /* look at command line arguments */
    user_opts = parse_args(argc, argv);
    if(!user_opts)
    {
        fprintf(stderr,
                "Error: failed to parse command line arguments.\n");
        return(-1);
    }
    if (!eattr_is_prefixed(user_opts->key))
    {
        fprintf(stderr,
                "extended attribute key is not prefixed %s\n",
                (char *) user_opts->key);
        return -1;
    }

    fd = pvfs_open(user_opts->srcfile, O_RDWR);
    if (fd < 0)
    {
        fprintf(stderr, "failed to open file %s\n", user_opts->srcfile);
        return -1;
    }

    switch (user_opts->op)
    {
    case GET :
        ret = pvfs_fgetxattr(fd,
                             user_opts->key,
                             user_opts->resp,
                             user_opts->rsize);
        if (ret == -1)
        {
            PVFS_perror("pvfs_fgetxattr", ret); 
            return ret;
        }
        break;
    case SET :
    case ATM :
    case FAA :
        if (!permit_set(user_opts->key))
        {
            fprintf(stderr,
                    "Not permitted to set key %s\n",
                    (char *) user_opts->key);
            return -1;
        }
        if (strncmp(user_opts->key,
                    "user.pvfs2.meta_hint",
                    user_opts->ksize) == 0)
        {
            fprintf(stderr,
                    "Not permitted to set key meta hin with this tool\n");
            return -1;
        }
        if (user_opts->op == SET)
        {
            ret = pvfs_fsetxattr(fd,
                                 user_opts->key,
                                 user_opts->val,
                                 user_opts->vsize,
                                 0);
            if (ret == -1)
            {
                PVFS_perror("pvfs_fsetxattr", ret); 
                return ret;
            }
        }
        else if (user_opts->op == ATM)
        {
            ret = pvfs_fatomicxattr(fd,
                                    user_opts->opcode,
                                    user_opts->key,
                                    user_opts->cmp,
                                    user_opts->csize,
                                    user_opts->val,
                                    user_opts->vsize,
                                    user_opts->resp,
                                    user_opts->rsize,
                                    0);
            if (ret == -1)
            {
                PVFS_perror("pvfs_fatomicxattr",ret); 
                return ret;
            }
        }
        else if (user_opts->op == FAA)
        {
            ret = pvfs_fatomicxattr(fd,
                                    user_opts->opcode,
                                    user_opts->key,
                                    NULL,
                                    0,
                                    user_opts->val,
                                    user_opts->vsize,
                                    user_opts->resp,
                                    user_opts->rsize,
                                    0);
            if (ret == -1)
            {
                PVFS_perror("pvfs_fatomicxattr",ret); 
                return ret;
            }
        }
        break;
    case LST :
    case DEL :
    default :
        break;
    }

    switch (user_opts->op)  
    {
    case ATM :
    case GET :
        if ( strncmp(user_opts->key,
                           "user.pvfs2.mirror.mode",
                           user_opts->ksize) == 0)
        {
             printf("Mirroring Mode : ");
             switch(*(uint32_t *)user_opts->val)
             {
                 case NO_MIRRORING :
                 {
                     printf("Turned OFF\n");
                     break;
                 }
                 case MIRROR_ON_IMMUTABLE :
                 {
                     printf("Create Mirror when IMMUTABLE is set\n");
                     break;
                 }
                 default:
                 {
                     printf("Unknown mode(%d)\n",
                            *(int *)user_opts->val);
                     break;
                 }
             } /* end switch */
        }
        else
        {
            if (user_opts->key && user_opts->resp)
            {
                if (user_opts->nflg)
                {
                    printf("key : \"%s\" \tValue : %d\n",
                            (char *)user_opts->key,
                            *(int32_t *)user_opts->resp);
                }
                else
                {
                    printf("key : \"%s\" \tValue : \"%s\"\n",
                            (char *)user_opts->key,
                            (char *)user_opts->resp);
                }
            }
            else
            {
                printf("error: NULL response buffer pointer\n");
            }
        }
        break;
    case FAA :
        if (user_opts->key && user_opts->resp)
        {
            printf("key : \"%s\" \tValue : %d\n",
                    (char *)user_opts->key,
                    *(int32_t *)user_opts->resp);
        }
        else
        {
            printf("error: NULL response buffer pointer\n");
        }
        break;
    default :
        break;
    }
    PVFS_sys_finalize();
    return(ret);
}

static int permit_set(char *key_p)
{
    if (strncmp(key_p, "system.", 7) == 0   ||
        strncmp(key_p, "trusted.", 8) == 0  ||
        strncmp(key_p, "security.", 9) == 0)
    {
        return 0;
    }
    return 1;
}


/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct options* parse_args(int argc, char* argv[])
{
    char flags[] = "k:v:c:n:sgdlafN";
    int one_opt = 0;

    struct options* tmp_opts = NULL;

    /* create storage for the command line options */
    tmp_opts = (struct options*)malloc(sizeof(struct options));
    if(!tmp_opts)
    {
	return(NULL);
    }
    memset(tmp_opts, 0, sizeof(struct options));

    /* fill in defaults */
    tmp_opts->srcfile = strdup(argv[argc-1]);
    tmp_opts->op = NONE;

    /* look at command line arguments */
    opterr = 0; /* suppress stderr error printing */
    while((one_opt = getopt(argc, argv, flags)) != -1)
    {
	switch(one_opt)
        {
            case 'g':
                if (tmp_opts->op)
                {
                    printf("May only specify one operation gsdla\n");
                    usage(argc, argv);
                    exit(-1);
                }
                tmp_opts->op = GET;
                break;
            case 's':
                if (tmp_opts->op)
                {
                    printf("May only specify one operation gsdla\n");
                    usage(argc, argv);
                    exit(-1);
                }
                tmp_opts->op = SET;
                break;
            case 'd':
                if (tmp_opts->op)
                {
                    printf("May only specify one operation gsdla\n");
                    usage(argc, argv);
                    exit(-1);
                }
                tmp_opts->op = DEL;
                break;
            case 'l':
                if (tmp_opts->op)
                {
                    printf("May only specify one operation gsdla\n");
                    usage(argc, argv);
                    exit(-1);
                }
                tmp_opts->op = LST;
                break;
            case 'a':
                if (tmp_opts->op)
                {
                    printf("May only specify one operation gsdla\n");
                    usage(argc, argv);
                    exit(-1);
                }
                tmp_opts->op = ATM;
                tmp_opts->opcode = PVFS_SWAP; /* default for now */
                break;
            case 'f':
                if (tmp_opts->op)
                {
                    printf("May only specify one operation gsdlaf\n");
                    usage(argc, argv);
                    exit(-1);
                }
                tmp_opts->op = FAA;
                tmp_opts->opcode = PVFS_FETCH_AND_ADD;
                break;
            case 'k':
                tmp_opts->key = strdup(optarg);
                tmp_opts->ksize = strlen(tmp_opts->key) + 1;
                break;
            case 'v':
            case 'c':
                if (strncmp(tmp_opts->key,
                            "user.pvfs2.mirror.mode",
                            tmp_opts->ksize) == 0 ||
                    strncmp(tmp_opts->key,
                            "user.pvfs2.mirror.copies",
                            tmp_opts->ksize) == 0)
                {
                    /*convert string argument into numeric argument*/
                    tmp_opts->val = malloc(sizeof(int32_t));
                    if (!tmp_opts->val)
                    {
                       printf("Unable to allocate memory for key value.\n");
                       exit(EXIT_FAILURE);
                    }
                    memset(tmp_opts->val, 0, sizeof(int32_t));
                    *(int *)tmp_opts->val = atoi(optarg);
                    tmp_opts->vsize = sizeof(int32_t);
                }
                else
                {
                    if (one_opt == 'v')
                    {
                        tmp_opts->val = strdup(optarg);
                        tmp_opts->vsize = strlen(tmp_opts->val) + 1;
                    }
                    else
                    {
                        tmp_opts->cmp = strdup(optarg);
                        tmp_opts->csize = strlen(tmp_opts->cmp) + 1;
                    }
                }
                break;
            case 'n':
                /*convert string argument into numeric argument*/
                tmp_opts->val = malloc(sizeof(int32_t));
                if (!tmp_opts->val)
                {
                   printf("Unable to allocate memory for key value.\n");
                   exit(EXIT_FAILURE);
                }
                memset(tmp_opts->val, 0, sizeof(int32_t));
                *(int32_t *)tmp_opts->val = atoi(optarg);
                tmp_opts->vsize = sizeof(int32_t);
                break;
            case 'N':
                tmp_opts->nflg = 1;
                break;
	    case('?'):
                printf("Bad option: %c\n", optopt);
		usage(argc, argv);
		exit(EXIT_FAILURE);
	}
    }

    /*ensure that the given mode is supported by PVFS*/
    if ((tmp_opts->op != GET && tmp_opts->op != TXT) &&
         strncmp(tmp_opts->key
                 ,"user.pvfs2.mirror.mode"
                 ,tmp_opts->ksize) == 0)
    {
        if (tmp_opts->val &&
            (*(int32_t *)tmp_opts->val < BEGIN_MIRROR_MODE ||
             *(int32_t *)tmp_opts->val > END_MIRROR_MODE) )
        {
            fprintf(stderr,"Invalid Mirror Mode ==> %d\n"
                           "\tValid Modes\n"
                           "\t1. %d == No Mirroring\n"
                           "\t2. %d == Mirroring on Immutable\n"
                          ,*(int32_t *)tmp_opts->val
                          ,NO_MIRRORING,MIRROR_ON_IMMUTABLE);

            exit(EXIT_FAILURE);
        }
    }

    if (tmp_opts->op == GET || tmp_opts->op == ATM)
    {
        tmp_opts->resp = calloc(1, VALBUFSZ);
        if (!tmp_opts->resp)
        {
            fprintf(stderr, "Unable to allocate tmp_opts->resp.\n");
            exit(EXIT_FAILURE);
        }
        tmp_opts->rsize = VALBUFSZ;
    }
    else if (tmp_opts->op == FAA)
    {
        tmp_opts->resp = calloc(1, sizeof(int32_t));
        if (!tmp_opts->resp)
        {
            fprintf(stderr, "Unable to allocate tmp_opts->resp.\n");
            exit(EXIT_FAILURE);
        }
        tmp_opts->rsize = sizeof(int32_t);
    }
    else if (tmp_opts->op == SET)
    {
        if (tmp_opts->val == NULL)
        {
            fprintf(stderr,
                    "Please specify value if setting extended "
                    "attributes\n");
            usage(argc, argv);
            exit(EXIT_FAILURE);
        }
    }
    else if (tmp_opts->op == GET || tmp_opts->op == DEL)
    {
        if (tmp_opts->key == NULL)
        {
            fprintf(stderr,
                    "Please specify key if getting extended attributes\n");
            usage(argc, argv);
            exit(EXIT_FAILURE);
        }
    }
    return(tmp_opts);
}


static void usage(int argc, char** argv)
{
    fprintf(stderr,"Usage: %s -s | -g | -d | -l | -a | -f "
                   "-k <key> -v <val> -c <cmp> -n <num> filename\n", argv[0]);
    return;
}

static int eattr_is_prefixed(char* key_name)
{
    int i = 0;
    while(PINT_eattr_namespaces[i])
    {
        if(strncmp(PINT_eattr_namespaces[i],
                   key_name,
                   strlen(PINT_eattr_namespaces[i])) == 0)
        {
            return(1);
        }
        i++;
    }
    return(0);
}



/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

