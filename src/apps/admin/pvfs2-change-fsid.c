/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/** \file
 * Update utility for updating fsid in PVFS2 collections
 */

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <getopt.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
#include <limits.h>
#include <inttypes.h>
#include <time.h>

#include <db.h>

#include "pvfs2-config.h"
#include "pvfs2.h"
#include "pvfs2-internal.h"
#include "trove.h"
#include "mkspace.h"
#include "pint-distribution.h"
#include "pint-dist-utils.h"


typedef struct
{
    char db_path[PATH_MAX];
    char fs_conf[PATH_MAX];
    char fs_name[PATH_MAX];
    char storage_path[PATH_MAX];
    int32_t old_fsid;
    int32_t new_fsid;
    char old_fsid_hex[9];
    char new_fsid_hex[9];
    int verbose;
    int view_only;
    int hex_dir_exists;
} options_t;


int update_fs_conf(void);
int update_fsid_in_collections_db(void);
int get_old_fsid_from_conf(void);
int move_hex_dir(void);
int process_args(int argc, char ** argv);
int setup(int argc, char ** argv);
void print_help(char * progname);

static options_t opts;

int main(int argc, char ** argv)
{
    int ret = 0;
    
    ret = setup( argc, argv);
    if(ret)
    {
        printf("Error in setup function\n");
        return -1;
    }

    if(opts.old_fsid == opts.new_fsid)
    {
        if(opts.verbose)
        {
            printf("Nothing to do. Old/New fsids are the same.\n");
        }
        return 0;
    }

    /* Fix up the pvfs2-fs.conf file */
    if(!opts.view_only)
    {
        ret = update_fs_conf();
        if(ret)
        {
            fprintf(stderr,"Error updating %s\n", opts.fs_conf);
            return -1;
        }

        if(opts.verbose)
        {
            printf("Successfully changed ID in [%s] from [%" PRId32 "] " \
                   "to [%" PRId32 "]\n",
                   opts.fs_conf,
                   opts.old_fsid,
                   opts.new_fsid);
        }
    }

    /* Move the hex dir in the storage space */
    ret = move_hex_dir();
    if(ret)
    {
        fprintf(stderr,"Error moving hex directory in storage space.\n");
        return -1;
    }

    if(opts.view_only)
    {
        printf("ID field in [%s] is dec=[%" PRId32 "] hex=[%s]\n",
               opts.fs_conf, 
               opts.old_fsid,
               opts.old_fsid_hex);
        if(opts.hex_dir_exists)
        {
            printf("Found directory [%s] in storage space.\n", 
                   opts.old_fsid_hex);
        }
        else
        {
            printf("Directory [%s] was NOT found in storage space.\n",
                   opts.old_fsid_hex);
        }
    }

    /* Fix up the collections.db file */
    ret = update_fsid_in_collections_db();
    if(ret)
    {
        fprintf(stderr,"Error updating collections.db file\n");
        return -1;
    }

    return 0;
}

int setup( int argc, char ** argv)
{
    int ret = 0;

    /* Process command line args */
    ret = process_args( argc, argv);
    if(ret)
    {
        fprintf(stderr,"Error processing arguments\n");
        return -1;
    }

   
    /* If no old_fsid provided, look it up in the conf file */
    /*   OR if view_only is specified, look it up */
    if(!opts.old_fsid || opts.view_only)
    {
        ret = get_old_fsid_from_conf();
        if(ret)
        {
            fprintf(stderr,"Error getting fsid.\n");
            return -1;
        }
    }

    /* If no new_fsid provided, generate one */
    if(!opts.new_fsid && !opts.view_only)
    {
        srand( time(NULL) );
        /* This number came from genconfig */
        opts.new_fsid = (int32_t)abs(rand() * 2147483647);
        sprintf(opts.new_fsid_hex,"%08" PRIx32, opts.new_fsid);
        if(opts.verbose)
        {
            printf("Generated new fsid of %" PRId32 "\n", opts.new_fsid);
        }
    }

    if(opts.verbose)
    {
        printf("Moving from fsid dec=[%" PRId32 "] hex=[%s] " \
               "to fsid dec=[%" PRId32 "] hex=[%s]\n",
               opts.old_fsid,
               opts.old_fsid_hex,
               opts.new_fsid,
               opts.new_fsid_hex);
    }
     
    return 0;
}

int update_fs_conf(void)
{
    FILE * fptr = NULL;
    char command[PATH_MAX];
    char output[PATH_MAX];
    char file[512][512];
    struct stat buf;
    int ret = 0, i = 0, j = 0;
    
    /* See if fs_conf file exists */
    ret = stat(opts.fs_conf, &buf);
    if(ret)
    {
        fprintf(stderr,
                "Error checking for file's existance. [%s]",
                opts.fs_conf);
        return -1;
    }

    memset(command,0,sizeof(command));
    memset(output,0,sizeof(output));

    /* See if old_fsid is in the file provided */
    sprintf(command, 
            "grep ID %s | grep %" PRId32, 
            opts.fs_conf, 
            opts.old_fsid);
    fptr = popen(command, "r");
    if(fptr == NULL)
    {
        fprintf(stderr,"Error opening pipe. errno=%d",errno);
        exit(-1);
    }
    ret = fscanf(fptr, "%s", output);
    if(ret != 1 || !strncmp(output,"",PATH_MAX))
    {
        printf("fsid [%" PRId32 "] not found in file\n",opts.old_fsid);
        return -1;
    }
    pclose(fptr);

    memset(output,0,sizeof(output));

    /* Replace old_fsid with new_fsid and write file back out */
    sprintf(command, 
            "sed s/%" PRId32 "/%" PRId32 "/ %s", 
            opts.old_fsid, 
            opts.new_fsid, 
            opts.fs_conf);
    fptr = popen(command, "r");
    if(fptr == NULL)
    {
        fprintf(stderr,"Error opening pipe. errno=%d",errno);
        exit(-1);
    }

    i = 0;
    while( fgets(output, sizeof(output), fptr) )
    {
        strncpy(file[i],output,512);
        i++;
    }
    pclose(fptr);

    fptr = fopen(opts.fs_conf,"w");
    for(j = 0; j < i; j++)
    {
        fprintf(fptr,"%s",file[j]);
    }
    fclose(fptr);

    return 0;
}

int get_old_fsid_from_conf(void)
{
    FILE * fptr = NULL;
    int i = 0;
    char buffer[512][512];
    int32_t read_fsid = 0;
    int ret;

    memset(buffer, 0, sizeof(buffer));
    
    fptr = fopen(opts.fs_conf,"r+");
    if(fptr == NULL)
    {
        fprintf(stderr,"Error opening %s\n",opts.fs_conf);
        return -1;
    }

    /* Read in file */
    while(!feof(fptr))
    {
        ret = fscanf(fptr,"%s",buffer[i]);
        if(ret == 1)
        {
            i++;
        }
    }
    fclose(fptr);

    /* Search for ID field */
    i = 0; 
    while(strncmp(buffer[i],"ID",2))
    {
        i++;
    }
    
    /* Read in ID */
    sscanf(buffer[++i],"%" SCNd32, &read_fsid);
    if(!read_fsid) 
    {
        fprintf(stderr,"Error: fsid not found.\n");
        return -1;
    }
    
    opts.old_fsid = read_fsid;
    sprintf(opts.old_fsid_hex,"%08" PRIx32,opts.old_fsid);

    return 0;
}

int move_hex_dir(void)
{
    FILE * fptr = NULL;
    char command[PATH_MAX];
    char output[PATH_MAX];
    struct stat buf;
    char path[PATH_MAX];
    char new_path[PATH_MAX];
    int ret = 0;
    
    memset(path,0,sizeof(path));
    sprintf(path,"%s/%s", opts.storage_path, opts.old_fsid_hex);

    /* See if directory exists */
    ret = stat(path, &buf);
    if(ret)
    {
        fprintf(stderr,
                "Error checking for directory's existance. [%s]\n",
                path);
        return -1;
    }

    opts.hex_dir_exists = 1;

    if(opts.view_only)
    {
        return 0;
    }
    
    memset(command,0,sizeof(command));
    memset(output,0,sizeof(output));
    memset(new_path,0,sizeof(new_path));

    /* Move the directory */
    sprintf(new_path, "%s/%s", opts.storage_path, opts.new_fsid_hex);
    sprintf(command, "mv %s %s", path, new_path);

    fptr = popen(command, "r");
    if(fptr == NULL)
    {
        fprintf(stderr,"Error opening pipe. errno=%d",errno);
        exit(-1);
    }
    ret = fscanf(fptr, "%s", output);
    if(ret && strncmp(output,"",PATH_MAX))
    {
        printf("mv from [%s] to [%s] failed.\n", path, new_path);
        return -1;
    }
    pclose(fptr);

    if(opts.verbose)
    {
        printf("Successful dir move from [%s] to [%s]\n", path, new_path);
    }

    return 0;
}

int update_fsid_in_collections_db(void)
{
    int ret = -1;
    DB * dbp;
    DBT key, data;
    DBC * dbc_p = NULL;
    TROVE_coll_id coll_id;
   
    ret = db_create(&dbp, NULL, 0);
    if(ret != 0)
    {
        fprintf(stderr, "Error: db_create: %s.\n", db_strerror(ret));
        return(-1);
    }

    /* open collections.db */
    ret = dbp->open(dbp,
                    #ifdef HAVE_TXNID_PARAMETER_TO_DB_OPEN
                    NULL,
                    #endif
                    opts.db_path,
                    NULL,
                    DB_UNKNOWN,
                    0,
                    0);
    if(ret != 0)
    {
        fprintf(stderr,"Error:dbp->open:%s.\n", db_strerror(ret));
        return(-1);
    }

    ret = dbp->cursor(dbp, NULL, &dbc_p, 0);
    if (ret != 0)
    {
        fprintf(stderr, "Error: dbp->cursor: %s.\n", db_strerror(ret));
        dbp->close(dbp, 0);
        return(-1);
    }

    memset(&key, 0, sizeof(key));
    memset(&data, 0, sizeof(data));

    key.data = opts.fs_name;
    key.size = strlen(opts.fs_name) + 1;
    data.data = &coll_id;
    data.ulen = sizeof(coll_id);
    data.flags = DB_DBT_USERMEM;

    ret = dbp->get(dbp, NULL, &key, &data, 0);
    if (ret != 0)
    {
        fprintf(stderr, "Error: dbp->get: %s\n", db_strerror(ret));
        return -1;
    }
    
    if(opts.view_only)
    {
        printf("DB entry in [%s] for key [%s] is dec=[%" PRId32 "] " \
               "hex=[%08" PRIx32 "]\n", 
               opts.db_path, 
               opts.fs_name, 
               (int32_t)coll_id,
               (int32_t)coll_id);
        return 0;
    }

    if(opts.verbose)
    {
        printf("Retrieved key [%s] from db. Old value is [%" PRId32 "]\n", 
               (char *)key.data, 
               (int32_t)coll_id);
    }

    if(coll_id != opts.old_fsid)
    {
        fprintf(stderr, "Error: fsid retrieved does not equal old " \
                        "fsid provided! Found:[%" PRId32 "] " \
                        "Provided:[%" PRId32 "]\n",
                        (int32_t)coll_id, 
                        opts.old_fsid);
        return -1;
    }

    /* At this point the fsid (or coll_id) has been retrieved and checked */
    /*  against the user provided fsid (old) */
    /* Replace old fsid with new fsid */

    data.data = &(opts.new_fsid);
    data.ulen = sizeof(opts.new_fsid);
    data.flags = DB_DBT_USERMEM;

    ret = dbp->put(dbp, NULL, &key, &data, 0);
    if (ret != 0)
    {
        fprintf(stderr, "Error: dbp->get: %s\n", db_strerror(ret));
        return -1;
    }

    /* At this point the new fsid should be properly inserted into the db */
    /* Sanity check by retrieving again, and comparing to the new fsid */

    data.data = &coll_id;
    data.ulen = sizeof(coll_id);
    data.flags = DB_DBT_USERMEM;

    ret = dbp->get(dbp, NULL, &key, &data, 0);
    if (ret != 0)
    {
        fprintf(stderr, "Error: dbp->get: %s\n", db_strerror(ret));
        return -1;
    }

    if(coll_id != opts.new_fsid)
    {
        fprintf(stderr, "Error: fsid retrieved after the replace finished " \
                        "does not equal old fsid provided! "\
                        "Found:[%" PRId32 "] Provided:[%" PRId32 "]\n",
                        (int32_t)coll_id, 
                        (int32_t)opts.old_fsid);
        return -1;
    }

    if(opts.verbose)
    {
        printf("Retrieved key [%s] from db. New value is [%" PRId32 "]\n", 
               (char *)key.data, (int32_t)coll_id);
    }

    dbc_p->c_close(dbc_p);
    dbp->close(dbp, 0);
    return 0;
}

int process_args(int argc, char ** argv)
{
    int tmp_fsid = 0;
    int ret = 0, option_index = 0;
    static struct option long_opts[] =
    {
        {"help",0,0,0},
        {"verbose",0,0,0},
        {"oldfsid",1,0,0},
        {"newfsid",1,0,0},
        {"fsname",1,0,0},
        {"dbpath",1,0,0},
        {"fsconf",1,0,0},
        {"storage",1,0,0},
        {"view",0,0,0},
        {0,0,0,0}
    };

    memset(&opts, 0, sizeof(options_t));
    sprintf(opts.fs_name, "pvfs2-fs");

    while ((ret = getopt_long(argc, argv, "",
                              long_opts, &option_index)) != -1)
    {
	switch (option_index)
        {
            case 0: /* help */
                print_help(argv[0]);
                exit(0);

            case 1: /* verbose */
                opts.verbose = 1;
                break;

            case 2: /* oldfsid */
                tmp_fsid = atoi(optarg);
                opts.old_fsid = (int32_t) tmp_fsid;
                sprintf(opts.old_fsid_hex,"%08" PRIx32,opts.old_fsid);
                break;

            case 3: /* newfsid */
                tmp_fsid = atoi(optarg);
                opts.new_fsid = (int32_t) tmp_fsid;
                sprintf(opts.new_fsid_hex,"%08" PRIx32,opts.new_fsid);
                break;

            case 4: /* fsname */
                strncpy(opts.fs_name, optarg, PATH_MAX);
                break;

            case 5: /* dbpath */
                strncpy(opts.db_path, optarg, PATH_MAX);
                break;
            
            case 6: /* fsconf */
                strncpy(opts.fs_conf, optarg, PATH_MAX);
                break;

            case 7: /* storage */
                strncpy(opts.storage_path, optarg, PATH_MAX);
                break;

            case 8: /* view */
                opts.view_only = 1;
                break;

	    default:
                print_help(argv[0]);
		return(-1);
	}
        option_index = 0;
    }
    
    /* db_path must be set */
    if(!strncmp(opts.db_path,"",PATH_MAX))
    {
        fprintf(stderr,"Error: --dbpath option must be given.\n");
        print_help(argv[0]);
        return(-1);
    }

    /* fs_conf must be set */
    if(!strncmp(opts.fs_conf,"",PATH_MAX))
    {
        fprintf(stderr,"Error: --fsconf option must be given.\n");
        print_help(argv[0]);
        return(-1);
    }

    /* storage_path must be set */
    if(!strncmp(opts.storage_path,"",PATH_MAX))
    {
        fprintf(stderr,"Error: --storage option must be given.\n");
        print_help(argv[0]);
        return(-1);
    }

    return 0;
}

void print_help(char * progname)
{
    fprintf(stderr,"\nThis utility will update the fsid for a filesystem.\n");
    fprintf(stderr,"The following arguments are required:\n");
    fprintf(stderr,"--------------\n");
    fprintf(stderr,"  --dbpath=</path/to/collections.db>     "
            "The current file system ID.\n");
    fprintf(stderr,"  --fsconf=</path/to/pvfs2-fs.conf>     "
            "Fs config file for the the file system being modified.\n");
    fprintf(stderr,"  --storage=</path/to/pvfs2-storage-space>     "
            "Local storage space for the the file system being modified.\n");
    fprintf(stderr, "\n");
    fprintf(stderr,"The following arguments are optional:\n");
    fprintf(stderr,"--------------\n");
    fprintf(stderr,"  --oldfsid=<fs_id>     "
            "The current file system ID. Else looked up from fs conf.\n");
    fprintf(stderr,"  --newfsid=<fs_id>     "
            "The desired file system ID. Else generated as in genconfig.\n");
    fprintf(stderr,"  --verbose          "
            "Print verbose messages during execution.\n");
    fprintf(stderr,"  --help             "
            "Show this help listing.\n");
    fprintf(stderr, "\n");
    return;
}


/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

