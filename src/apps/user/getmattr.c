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

/*This program retrieves the mirroring mode and/or the number of mirror copies*/
/*for a given file.  Since these values are numeric, we could NOT use         */
/*getfattr(). This program is intended to be used when the PVFS client is in  */
/*kernel mode.                                                                */
int main(int argc, char **argv)
{
    struct options_t my_args = { .filename = NULL
                                ,.copies   = 0
                                ,.mode     = 0
                               };

    int ret;
    int copies=0, mode=0;

    /*Parse the command line*/
    ret = parse_args(argc, argv, &my_args);    
    if (ret)
    {
       printf("Error parsing the command line : %d\n",ret);
       exit(ret);
    }

    /*Get the mirroring attributes for the given file*/
    if (my_args.mode) {
#ifdef HAVE_GETXATTR_EXTRA_ARGS
       ret = getxattr(my_args.filename
                     ,"user.pvfs2.mirror.mode"
                     ,&mode
                     ,sizeof(mode)
                     ,0
	             ,0 );
#else
       ret = getxattr(my_args.filename
                     ,"user.pvfs2.mirror.mode"
                     ,&mode
                     ,sizeof(mode) );
#endif
       if (!ret)
          perror("Failure to get mirror mode");
       else {
          printf("Mirroring Mode : ");

          switch((MIRROR_MODE)mode)
          {
             case NO_MIRRORING:
             {
                 printf("Turned OFF \n");
                 break;
             }
             case MIRROR_ON_IMMUTABLE:
             {
                 printf("Create Mirror when IMMUTABLE is set\n");
                 break;
             }
             default:
             {
                 printf("currently unsupported(%d).\n",mode);
                 break;
             }
          }/*end switch*/       
       }/*end if*/
    }/*end if mode*/

    if (my_args.copies){
#ifdef HAVE_GETXATTR_EXTRA_ARGS
        ret = getxattr(my_args.filename
                      ,"user.pvfs2.mirror.copies"
                      ,&(copies)
                      ,sizeof(copies)
		      ,0
                      ,0);
#else
        ret = getxattr(my_args.filename
                      ,"user.pvfs2.mirror.copies"
                      ,&(copies)
                      ,sizeof(copies) );
#endif
        if (!ret)
           perror("Failure to get mirror copies");
        else
           printf("Number of Mirrored Copies : %d\n",copies);
    }/*end if copies*/

    exit(0);
} /*end main*/



/* parse_args()
 *
 * parses command line arguments
 *
 */
static int parse_args(int argc, char **argv, struct options_t *my_args)
{
    int one_opt = 0;

    /*c=copies, m=mode, f=filename(may include path), h|? = help*/
    char flags[] = "cmf:h?";

    /*must have the filename, at a minimum*/
    if (argc == 1)
       usage();  

    while((one_opt = getopt(argc, argv, flags)) != EOF)
    {
	switch(one_opt)
        {
	    case('c'):
            {   my_args->copies = 1;
                break;
            }
            case('m'):
            {   my_args->mode = 1;
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
               break;
            }
	}
    }/*end while*/

    /*filename is required*/
    if (my_args->filename == NULL )
        usage();

    /*get all attributes if none is specified*/
    if (my_args->copies == 0 && my_args->mode == 0) {
       my_args->copies = my_args->mode = 1;
    }

   return(0);
}/*end function parse_args*/


static void usage(void)
{
    fprintf(stderr,"getmattr [-c] [-m] [-h] -f file\n");
    fprintf(stderr,"\t-c : Retrieve the number of mirror copies\n"
                   "\t-m : Retrieve the mirroring mode\n"
                   "\t-h : Display this message\n\n"
                   "Retrieve copies and mode when none specified.  Filename "
                   "is required.\n");
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

