/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-config.h"
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <getopt.h>
#include <assert.h>
#include <sys/xattr.h>
#include <ctype.h>

#include "pvfs2.h"
#include "pvfs2-mirror.h"

struct options_t
{
   char *filename;
   int32_t copies;
   int32_t mode;
};


static int parse_args(int argc, char **argv, struct options_t *my_args);
static void usage(void);

/*This program sets the mirroring mode and/or the number of mirror copies for */
/*a given file.  Since these values are numeric, we could NOT use setfattr(). */
/*This program is to be used when the PVFS client is in kernel mode.          */
int main(int argc, char **argv)
{
    struct options_t my_args = { .filename = NULL
                                ,.copies = -1
                                ,.mode = -1
                               };

    int ret;

    /*Parse the command line*/
    ret = parse_args(argc, argv, &my_args);    
    if (ret)
    {
       printf("Error parsing the command line : %d\n",ret);
       exit(ret);
    }

    /*Set the mirroring attributes for the given file*/
    if (my_args.mode > 0) {
       printf("Setting mirror mode to %d\n"
             ,my_args.mode);
#ifdef HAVE_SETXATTR_EXTRA_ARGS
       ret = setxattr(my_args.filename
                     ,"user.pvfs2.mirror.mode"
                     ,&(my_args.mode)
                     ,sizeof(my_args.mode)
                     ,0
                     ,0);
#else
       ret = setxattr(my_args.filename
                     ,"user.pvfs2.mirror.mode"
                     ,&(my_args.mode)
                     ,sizeof(my_args.mode)
                     ,0);
#endif
       if (ret)
          perror("Failure to set mirror mode");
    }

    if (my_args.copies >= 0){
        printf("Setting number of mirrored copies to %d\n"
          ,my_args.copies);
#ifdef HAVE_SETXATTR_EXTRA_ARGS
        ret = setxattr(my_args.filename
                      ,"user.pvfs2.mirror.copies"
                      ,&(my_args.copies)
                      ,sizeof(my_args.copies)
                      ,0
                      ,0);
#else
        ret = setxattr(my_args.filename
                      ,"user.pvfs2.mirror.copies"
                      ,&(my_args.copies)
                      ,sizeof(my_args.copies)
                      ,0);
#endif
        if (ret)
           perror("Failure to set mirror copies");
    }

    exit(0);
} /*end program*/

/* parse_args()
 *
 * parses command line arguments
 *
 */
static int parse_args(int argc, char **argv, struct options_t *my_args)
{
    int one_opt = 0;
    int j;

    /*c=copies, m=mode, f=filename(may include path), h|? = help*/
    char flags[] = "c:m:f:h?";
  

    /*no arguments*/
    if (argc == 1)
       usage();

    while((one_opt = getopt(argc, argv, flags)) != EOF)
    {
	switch(one_opt)
        {
	    case('c'):
            {   for(j=0; j<strlen(optarg) && isdigit(optarg[j]); j++);
                if (j==strlen(optarg)) 
                    my_args->copies = atoi(optarg);
                else
                   usage();
                break;
            }
            case('m'):
            {   for(j=0; j<strlen(optarg) && isdigit(optarg[j]); j++);
                if (j==strlen(optarg))
                   my_args->mode = atoi(optarg);
                else
                   usage();
                break;
            }
            case('f'):
            {
                my_args->filename = optarg;
                break;
            }
            case('h'):
            case('?'):
            default:
            {
               usage();
            }
	}
    }/*end while*/

    /*filename is required*/
    if (my_args->filename == NULL )
        usage();

    /*at least one (copies|mode) is required*/
    if (my_args->copies < 0 && my_args->mode < 0)
        usage();

    /*mode must be valid*/
    if (my_args->mode > 0 && (my_args->mode != NO_MIRRORING &&
                              my_args->mode != MIRROR_ON_IMMUTABLE) )
        usage();

   return(0);
}/*end function parse_args*/


static void usage(void)
{
    fprintf(stderr,"setmattr {-c copies} {-m mode} {-h} -f file\n");
    fprintf(stderr,"\tcopies : positive numeric value\n"
                   "\t  mode : 100 => No Mirroring\n"
                   "\t         200 => Create Mirror when IMMUTABLE is set\n"
                   "\t    -h : Display this message\n" 
                   "\t  file : file to mirror (may include path)\n");
    exit(0);
}/*end function usage()*/

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

