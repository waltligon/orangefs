/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>

#include "pvfs2-internal.h"
#include "gossip.h"
#include "pvfs2-debug.h"
#include "pvfs3-handle.h"
#include "sidcache.h"
#include "pvfs2-server.h"
#include "server-config.h"
#include "server-config-mgr.h"

/* Length of string representation of PVFS_SID */
/* #define SID_STR_LEN (37) in sidcache.h */

/* Used by bulk retrieve function to keep track of the sid obtained
 * from the database during bulk retrieval 
 */
DBT bulk_next_key;

/* Global Number of records (SIDs) in the cache
 */
static int sids_in_cache = 0;


/* Global database variables */
DB *SID_db = NULL;                  /* Primary database (sid cache) */
DB_ENV *SID_envp = NULL;            /* Env for sid cache and secondary dbs */
DB *SID_attr_index[SID_NUM_ATTR];   /* Array of secondary databases */
DBC *SID_attr_cursor[SID_NUM_ATTR]; /* Array of secondary database cursors */
DB_TXN *SID_txn = NULL;             /* Main transaction variable */
                                    /* (transactions are currently not used */
                                    /*     with the sid cache) */
DB  *SID_type_db = NULL;            /* secondary db for server type */
DBC *SID_type_cursor = NULL;        /* cursor for server type db */
DB  *SID_type_index = NULL;         /* index on sid for server type db */
DBC *SID_index_cursor = NULL;       /* cursor for server type sid index */

/* THese are the key structures for the type db */
typedef struct SID_type_db_key_s
{
    uint32_t typeval;
    PVFS_SID sid;
} SID_type_db_key;

typedef struct SID_type_index_key_s
{
    PVFS_SID sid;
    uint32_t typeval;
} SID_type_index_key;


/* <========================= STATIC FUNCTIONS ============================> */
static int SID_initialize_secondary_dbs(DB *secondary_dbs[]);
static int SID_initialize_database_cursors(DBC *db_cursors[]);
static int SID_cache_parse_header(FILE *inpfile,
                                  int *records_in_file,
                                  int *attrs_in_file,
                                  int **attr_positions);
static int SID_create_type_table(void);
static char *SID_type_to_string(const uint32_t typeval);
static int SID_type_store(PVFS_SID *sid, FILE *outpfile);
static int SID_cache_update_type(const PVFS_SID *sid_server,
                                 uint32_t new_type_val);
static int SID_cache_update_attrs(DB *dbp,
                                  const PVFS_SID *sid_server,
                                  int new_attr[]);

/* <====================== INITIALIZATION FUNCTIONS =======================> */

/* 
 * This function initializes the indices of a secondary DB to all be equal
 * to NULL
 *
 * On success 0 is returned, otherwise the index that is not equal to NULL
 * is returned unless the index is 0, then -1 is returned
 */
static int SID_initialize_secondary_dbs(DB *secondary_dbs[])
{
    int ret =  0;
    int i = 0;

    memset(secondary_dbs, '\0', (sizeof(DB *) * SID_NUM_ATTR));

    for(i = 0; i < SID_NUM_ATTR; i++)
    {
        if(secondary_dbs[i] != NULL)
        {
            if(i == 0)
            {
                return(-1);
            }
            else
            {
                return(i);
            }
        }
    }

    return(ret);
}

/*
 * This function initializes the indices of a DBC array to all be equal to NULL
 *
 * On success 0 is returned, otherwise the index that is not equal to NULL
 * is returned unless the index is 0, then -1 is returned
 */
static int SID_initialize_database_cursors(DBC *db_cursors[])
{
    int ret = 0;
    int i = 0;

    memset(SID_attr_cursor, '\0', (sizeof(DBC *) * SID_NUM_ATTR));

    for(i = 0; i < SID_NUM_ATTR; i++)
    {
        if(SID_attr_cursor[i] != NULL)
        {
            if(i == 0)
            {
                return(-1);
            }
            else
            {
                return(i);
            }
        }
    }

    return(ret);
}
/* <=========================== HELPER FUNCTIONS =========================> */

/* These are defined in sidcacheval.h */

struct type_conv
{
    uint32_t typeval;
    char *typestring;
} type_conv_table[] =
{
    {SID_SERVER_ROOT        , "ROOT"} ,
    {SID_SERVER_PRIME       , "PRIME"} ,
    {SID_SERVER_CONFIG      , "CONFIG"} ,
    {SID_SERVER_LOCAL       , "LOCAL"} ,
    {SID_SERVER_META        , "META"} ,
    {SID_SERVER_DATA        , "DATA"} ,
    {SID_SERVER_DIRDATA     , "DIRDATA"} ,
    {SID_SERVER_SECURITY    , "SECURITY"} ,
    /* these should always be last */
    {SID_SERVER_ME          , "ME"} ,
    {SID_SERVER_VALID_TYPES , "ALL"} ,
    {SID_SERVER_NULL        , "INVALID"}
};
/* This must be defined as the size of the longest typestring */
#define MAX_TYPE_STR 8
    
static char *SID_type_to_string(const uint32_t typeval)
{
    int i;
    for (i = 0;
         type_conv_table[i].typeval != typeval &&
         type_conv_table[i].typeval != SID_SERVER_NULL;
         i++);
    return type_conv_table[i].typestring;
}
 
/* This function is exported for parsing the config file */
uint32_t SID_string_to_type(const char *typestring)
{
    int i;
    char *mytype;
    int len;

    if(!typestring)
    {
        return SID_SERVER_NULL;
    }

    len = strnlen(typestring, MAX_TYPE_STR) + 1;

    if (len > MAX_TYPE_STR + 1)
    {
        return SID_SERVER_NULL;
    }

    mytype = (char *)malloc(len);

    for (i = 0; i < len; i++)
    {
        mytype[i] = toupper(typestring[i]);
    }
    mytype[len] = 0;

    for (i = 0;
         strcmp(type_conv_table[i].typestring, mytype) &&
         type_conv_table[i].typeval != SID_SERVER_NULL;
         i++);

    free(mytype);

    return type_conv_table[i].typeval;
}

/* We are expecting a string of the form "someattr=val" where val is an
 * integer.  If attributes is not careted yet, we create it.  If
 * something goes wrong we pretty much just return -1 without trying to
 * diagnose it sinice this is a very limited parse
 */
int SID_set_attr(const char *attr_str, int **attributes)
{
    int i = 0;;
    int len = 0;;
    int eqflag = 0;
    int atflag = 0;
    char *myattr = NULL;
    char *myval = NULL;

    if (!attributes || !attr_str)
    {
        return -1;
    }

    len = strnlen(attr_str, MAX_ATTR_STR);

    if (len > MAX_ATTR_STR)
    {
        return -1;
    }

    myattr = (char *)malloc(len + 1);
    for (i = 0; i < len; i++)
    {
        if (attr_str[i] == '=')
        {
            myattr[i] = 0;
            if (i == len - 1)
            {
                /* badly formed string */
                free(myattr);
                return -1;
            }
            myval = &myattr[i + 1];
            eqflag = 1;
        }
        else
        {
            myattr[i] = tolower(attr_str[i]);
        }
    }
    myattr[len] = 0;
    if (!eqflag)
    {
        /* badly formed string */
        free(myattr);
        return -1;
    }

    if (!*attributes)
    {
        *attributes = (int *)malloc(SID_NUM_ATTR * sizeof(int));
        memset(*attributes, -1, SID_NUM_ATTR * sizeof(int));
        atflag = 1;
    }

    for (i = 0; i < SID_NUM_ATTR; i++)
    {
        if(!strcmp(myattr, SID_attr_map[i]))
        {
            /* found the one we want */
            *attributes[i] = atoi(myval);
            free(myattr);
            return 0;
        }
    }
    /* didn't find it, just return */

    if (atflag)
    {
        /* we created this but didn't add any data so get rid of it */
        free(*attributes);
        *attributes = NULL;
    }
    free(myattr);
    return -1;
}

/* <====================== SID_cacheval_t FUNCTIONS =======================> */

/*
 * This function initializes a SID_cacheval_t struct to default values
*/
void SID_cacheval_init(SID_cacheval_t **cacheval)
{
    memset((*cacheval)->attr, -1, (sizeof(int) * SID_NUM_ATTR));
    memset(&((*cacheval)->bmi_addr), -1, sizeof(BMI_addr));
    (*cacheval)->url[0] = 0;
}

/** HELPER - assigns DBT for cacheval
 *
 * This function marshalls the data for the SID_cacheval_t to store in the 
 * sidcache
*/
void SID_cacheval_pack(const SID_cacheval_t *cacheval, DBT *data)
{
    data->data = (SID_cacheval_t *)cacheval;
    data->size = (sizeof(int) * SID_NUM_ATTR) +
                 sizeof(BMI_addr) +
                 strlen(cacheval->url) +
                 1;
}

/** HELPER - LOADS cacheval
 *
 * This function creates a SID_cacheval_t struct with the
 * attributes that are passed to
 * this function by dynamically creating the SID_cacheval_t.
 * The url attribute cannot
 * be null otherwise the SID_cacheval_t is not dynamically created
 *
 * Returns 0 on success, otherwise -1 is returned
 */
int SID_cacheval_alloc(SID_cacheval_t **cacheval,
                       int sid_attributes[],
                       BMI_addr sid_bmi,
                       char *sid_url)
{
    if(!sid_url)
    {
        gossip_err("The url passed to SID_cacheval_alloc is NULL\n");
        *cacheval = NULL;
        return(-1);
    }

    /* Mallocing space for the SID_cacheval_t struct */
    *cacheval = (SID_cacheval_t *)malloc(sizeof(SID_cacheval_t) +
                                          (strlen(sid_url) + 1));
    SID_cacheval_init(cacheval);
    
    /* Setting the values of the SID_cacheval_t struct */    
    memcpy((*cacheval)->attr, sid_attributes, (sizeof(int) * SID_NUM_ATTR));
    memcpy(&((*cacheval)->bmi_addr), &sid_bmi, sizeof(BMI_addr));
    strncpy((*cacheval)->url, sid_url, (strlen(sid_url) + 1));

    return(0);
}

/*
 * This function clean up a SID_cacheval_t struct by freeing the dynamically
 * created SID_cacheval_t struct
 */
void SID_cacheval_free(SID_cacheval_t **cacheval_t)
{
    free(*cacheval_t);
}

/** HELPER - copies cacheval out of DBT
 *
 * This function unpacks the data recieved from the database, mallocs the 
 * SID_cacheval_t struct, and sets the values inside of the SID_cacheval_t 
 * struct with the data retrieved from the database
 */
void SID_cacheval_unpack(SID_cacheval_t **cacheval, DBT *data)
{              
    *cacheval = malloc(data->size);
    memcpy(*cacheval, data->data, data->size);
}

/* <====================== SID CACHE FUNCTIONS ============================> */

/** HELPER for SID_LOAD
 *
 * This function parses the first two lines in the input file and gets
 * the number of attributes per sid, number of sids,
 * and the string representations of the int attributes for the sids.
 * It gets the attributes that each sid has
 * parsing the strings representations of the attributes.
 * This function also sets the attr_positions array to make sure that the
 * attributes in the input file go into their correct positions in the
 * SID_cacheval_t attrs array.
 *
 * Returns 0 on success, otherwise returns an error code
 * The attr_position array is malloced inside of this function and is freed
 * inside the SID_load function which calls this one.
 *
 * fmemopen may be used to read from a message buffer.
 */
static int SID_cache_parse_header(FILE *inpfile,
                                  int *records_in_file,
                                  int *attrs_in_file,
                                  int **attr_positions)
{
    int i = 0, j = 0;             /* Loop index variables */
    char **attrs_strings;         /* Strings of the attributes in the file */
    char tmp_buff[TMP_BUFF_SIZE]; /* Temporary string buffer to read the
                                     attribute strings from the input file */

    /* Checking to make sure the input file is open, the function
     * load_sid_cache_from_file should have opened the file
     */
    if(!inpfile)
    {
        gossip_err("File is not opened. Exiting load_sid_cache_from_file\n");
        return(-1);
    }
    
    /* Getting the total number of attributes from the file */
    fscanf(inpfile, "%s", tmp_buff);
    fscanf(inpfile, "%d", attrs_in_file);
    if(*attrs_in_file > SID_NUM_ATTR || *attrs_in_file < 1)
    {
        gossip_err("The number of attributes in the input file "
                   "was not within the proper range\n");
        gossip_err("The contents of the database will not be read "
                   "from the inputfile\n");
        return(-1);
    }
    
    /* Getting the number of sids in from the file */
    fscanf(inpfile, "%s", tmp_buff);
    fscanf(inpfile, "%d", records_in_file);

    /* Checking to make sure the input file has the number of sids as the
     *  entry in the input file
     */
    if(*records_in_file == 0)
    {
        gossip_err("There are no sids in the input file\n");
        return(-1);
    }
    
    /* Mallocing space to hold the name of the attributes in the file 
     * and initializing the attributes string array 
     */
    attrs_strings = (char **)malloc(sizeof(char *) * *attrs_in_file);
    memset(attrs_strings, '\0', sizeof(char *) * *attrs_in_file);

    /* Mallocing space to hold the positions of the attributes in the file for
     * the cacheval_t attribute arrays and initializing the position array 
     */
    *attr_positions = (int *)malloc(sizeof(int) * *attrs_in_file);
    memset(*attr_positions, 0, (sizeof(int) * *attrs_in_file));
    
    /* Getting the attribute strings from the input file */
    for(i = 0; i < *attrs_in_file; i++)
    {
        fscanf(inpfile, "%s", tmp_buff);
        attrs_strings[i] = (char *)malloc(sizeof(char) *
                                          (strlen(tmp_buff) + 1));
        strncpy(attrs_strings[i], tmp_buff, (strlen(tmp_buff) + 1));
    }

    /* Getting the correct positions from the input file for the
     * attributes in the SID_cacheval_t attrs array 
     */
    for(i = 0; i < *attrs_in_file; i++)
    {
        for(j = 0; j < SID_NUM_ATTR; j++)
        {
            if(!strcmp(attrs_strings[i], SID_attr_map[j]))
            {
                (*attr_positions)[i] = j;
                break;
            }
        }
        /* If the attribute string was not a valid option it will be ignored
         * and not added to the sid cache 
         */
        if(j == SID_NUM_ATTR)
        {
            gossip_debug(GOSSIP_SIDCACHE_DEBUG, 
                         "Attribute: %s is an invalid attribute, "
                         "and it will not be added\n",
                         attrs_strings[i]);
            (*attr_positions)[i] = -1;
        }
    }

    /* Freeing the dynamically allocated memory */
    for(i = 0; i < *attrs_in_file; i++)
    {
        free(attrs_strings[i]);
    }
    free(attrs_strings);

    return(0);
}

/** SID_type_load
 * This function reads from a file and locates type identifiers
 * Each one is added to the type_db
 * Invalid items or EOL cause the scan to stop
 */
#define TYPELINELEN 1024
static int SID_type_load(FILE *inpfile, const PVFS_SID *sid)
{
    int ret = 0;
    DBT type_key;
    DBT type_val;
    char linebuff[TYPELINELEN];
    char *lineptr = linebuff;
    char *saveptr = NULL;
    char *typeword;
    SID_type_db_key kbuf;

    kbuf.sid = *sid;

    SID_zero_dbt(&type_key, &type_val, NULL);

    type_key.data = &kbuf;
    type_key.size = sizeof(SID_type_db_key);
    type_key.ulen = sizeof(SID_type_db_key);

    /* read a line */
    memset(linebuff, 0, TYPELINELEN);
    fgets(linebuff, TYPELINELEN, inpfile);

    while(1)
    {
        /* read next word */
        memset(typeword, 0, 50);
#if 1
        /* strtok is not reentrant, strtok_r is not standard (gcc
         * extension) need to add config support to select the right
         * approach - V3
         */
# if 1
        typeword = strtok_r(lineptr, " \t\n", &saveptr);
# else
        typeword = strtok(lineptr, " \t\n");
# endif
        if (!typeword)
        {
            return 0; /* no more type tokens */
        }
        lineptr = NULL; /* subsequent calls from saved string pointer */
#else
        /* This is incomplete code using sscanf - has problems that must
         * be fixed to be usable, namely, the lineptr has to be moved by
         * the number of chars consumed in the sscanf, which is not
         * generally possible to determine without lots of effort
         */
        ret = sscanf(lineptr, " %49s", typeword);
        if(ret <= 0)
        {
            break; /* scanf error or end of string tokens */
        }
        /* move lineptr */
#endif

        if ((kbuf.typeval = SID_string_to_type(typeword)))
        {

            /* insert into type database */
            ret = SID_type_db->put(SID_type_db,
                                   NULL,
                                   &type_key,
                                   &type_val,
                                   0);
            if (!ret)
            {
                break; /* DB error */
            }
        }
        else
        {
            ret = -PVFS_EINVAL;
            break; /* invalid type string */
        }
    }
    return(ret);    
}
#undef TYPELINELEN

/** SID_LOAD
 *
 * This function loads the contents of an input file into the cache.
 * SID records in the file are added to the current contents of the
 * cache.  Duplicates are rejected with an error, only the first value
 * is kept.
 *
 * Returns 0 on success, otherwise returns an error code
 * The number of sids in the file is returned through the
 * parameter db_records.
 */
int SID_cache_load(DB *dbp, FILE *inpfile, int *num_db_records)
{
    int ret = 0;                      /* Function return value */
    int i = 0;                        /* Loop index variables */
    int *attr_positions = NULL;
    int attr_pos_index = 0;           /* Index of the attribute from the */
                                      /*   file in the SID_cacheval_t attrs */
    int attrs_in_file = 0;            /* Number of attrs in input file */
    int records_in_file = 0;          /* Total number of sids in input file */
    int sid_attributes[SID_NUM_ATTR]; /* Temporary attribute array to hold */
                                      /*   the attributes from the input file */
    int throw_away_attr = 0;          /* If the attribute is not a valid */
                                      /*   choice its value will be ignored */
    BMI_addr tmp_bmi = 0;             /* Temporary BMI_addr for the bmi */
                                      /*   addresses from input file */
    char tmp_url[TMP_BUFF_SIZE];      /* Temporary string for the url's from */
                                      /*   the input file */
    char tmp_sid_str[SID_STR_LEN];    /* Temporary string to get the PVFS_SID */
                                      /*   string representation from input */
                                      /*   file */
    PVFS_SID current_sid;             /* Temporary location to create the */
                                      /*   PVFS_SID from the tmp_SID_str */
    SID_cacheval_t *current_sid_cacheval; /* Sid's attributes from the input file */

    /* Getting the attributes from the input file */
    ret = SID_cache_parse_header(inpfile,
                                 &records_in_file,
                                 &attrs_in_file,
                                 &attr_positions);
    if(ret)
    {
        fclose(inpfile);
        return(ret);
    }

    for(i = 0; i < records_in_file; i++)
    {
        /* Read the sid's string representation from the input file */
        fscanf(inpfile, "%s", tmp_sid_str);

        /* convert to binary */
        ret = PVFS_SID_str2bin(tmp_sid_str, &current_sid);
        if(ret)
        {
            /* Skips adding sid to sid cache */
            gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                         "Error parsing PVFS_SID in "
                         "SID_load_cache_from_file function\n");
            continue;
        }

        /* Read the bmi address */
        fscanf(inpfile, SCANF_lld, &tmp_bmi);

        /* Read the url */
        fscanf(inpfile, "%s", tmp_url);

        /* Initializing the temporary attribute array */
        /* so all index's are currently -1 */
        memset(sid_attributes, -1, (sizeof(int) * SID_NUM_ATTR));

        /* Read the attributes from the input file and place them in the
         * correct position in the attrs array in the SID_cacheval_t struct 
         */
        for(attr_pos_index = 0;
            attr_pos_index < attrs_in_file;
            attr_pos_index++)
        {
            if(attr_positions[attr_pos_index] != -1)
            {
                fscanf(inpfile,
                       "%d",
                       &(sid_attributes[attr_positions[attr_pos_index]]));
            }
            else
            {
                fscanf(inpfile, "%d", &throw_away_attr);
            }
        }

        /* Read the type indicators from the file */
        SID_type_load(inpfile, &current_sid);

        /* This allocates and fills in the cacheval */
        ret = SID_cacheval_alloc(&current_sid_cacheval,
                                 sid_attributes,
                                 tmp_bmi,
                                 tmp_url);
        if(ret)
        {
            /* Presumably out of memory */
            return(ret);
        }
    
        /* Storing the current sid from the input file into the sid cache */
        ret = SID_cache_add_server(dbp,
                                   &current_sid,
                                   current_sid_cacheval,
                                   num_db_records);
        if(ret)
        {
            /* Need to examine error and decide if we should continue or
             * not - for now we stop and close the input file
             */
            fclose(inpfile);
            SID_cacheval_free(&current_sid_cacheval);
            return(ret);
        }

        SID_cacheval_free(&current_sid_cacheval);
    }

    free(attr_positions);
   
    return(ret);
}

/** SID_ADD
 * 
 * This function stores a sid into the sid_cache.
 * Pass a pointer to a record counter if we are adding a new record
 * and we will get an error if there is already a record with the same key.
 * Otherwise we assume that we are updating a record, though it will
 * insert silently if the record does not exist.
 *
 * Returns 0 on success, otherwise returns error code
*/
int SID_cache_add_server(DB *dbp,
                         const PVFS_SID *sid_server,
                         const SID_cacheval_t *cacheval,
                         int *num_db_records)
{
    int ret = 0;          /* Function return value */
    DBT key, value;       /* BerekeleyDB k/v pair sid value(SID_cacheval_t) */
    int putflags = 0;

    if(PVFS_SID_is_null(sid_server))
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                     "Error: PVFS_SID in SID_store_sid_into "
                     "sid_cache function is currently NULL\n");
        return(-1);
    }

    SID_zero_dbt(&key, &value, NULL);

    key.data = (PVFS_SID *)sid_server;
    key.size = sizeof(PVFS_SID);

    /* Marshalling the data of the SID_cacheval_t struct */
    /* to store it in the primary db */
    SID_cacheval_pack(cacheval, &value);

    if (num_db_records != NULL)
    {
        /* we are adding a new record 
         * we should not overwrite an old one
         */
        putflags |= DB_NOOVERWRITE;
    }

    /* Placing the data into the database */
    ret = (dbp)->put(dbp,            /* Primary database pointer */
                     NULL,            /* Transaction pointer */
                     &key,            /* PVFS_SID */
                     &value,          /* Data is marshalled SID_cacheval_t */
                     putflags);
    /* with DB_NOOVERWRITE this returns an error on a dup */
    if(ret)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                     "Error inserting sid into the sid cache : %s\n",
                     db_strerror(ret));
        return(ret);
    }

    /* Updating the record count if the sid was successfully added
     * to the sid cache 
     */
    if(num_db_records != NULL)
    {
        *num_db_records += 1;
    }

    return(ret);
}

/** SID_LOOKUP
 *
 * This function searches for a SID in the sidcache. The PVFS_SID value
 * (sid_server parameter) must be initialized before this function is used.
 * The *cacheval will be malloced and set to the values of the attibutes
 * in the database for the sid if it is found in the database
 *
 * Returns 0 on success, otherwise returns an error code
 * Caller is expected to free the cacheval via SID_cacheval_free
 */
int SID_cache_lookup_server(DB *dbp,
                            const PVFS_SID *sid_server,
                            SID_cacheval_t **cacheval)
{
    int ret = 0;
    DBT key, data;

    SID_zero_dbt(&key, &data, NULL);

    key.data = (PVFS_SID *)sid_server;
    key.size = sizeof(PVFS_SID);
   
    ret = (dbp)->get(dbp,   /* Primary database pointer */
                     NULL,  /* Transaction Handle */
                     &key,  /* Sid_sid_t sid */
                     &data, /* Data is marshalled SID_cacheval_t struct */
                     0);    /* get flags */
    if(ret)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                     "Error getting sid from sid cache : %s\n",
                     db_strerror(ret));
        return(ret);
    }

    /* Unmarshalling the data from the database for the SID_cacheval_t struct */
    SID_cacheval_unpack(cacheval, &data);
    
    return(ret);
}

/** HELPER - SID_LOOKUP and assign bmi_addr
 *
 * This function searches for a sid in the sid cache, retrieves the struct,
 * malloc's the char * passed in, and copies the bmi URI address of the retrieved
 * struct into that char *.
 *
 * Caller is expected to free the bmi_addr memory
 */
int SID_cache_lookup_bmi(DB *dbp, const PVFS_SID *search_sid, char **bmi_url)
{
    SID_cacheval_t *temp;
    int ret;

    /* Retrieve the corresponding value */
    ret = SID_cache_lookup_server(dbp, search_sid, &temp);

    if (ret)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                     "Error retrieving from sid cache\n");
        return ret;
    }

    /* Malloc the outgoing BMI address char * to be size of retrieved one */
    *bmi_url = malloc(strlen(temp->url) + 1);

    /* Copy retrieved BMI address to outgoing one */
    strcpy(*bmi_url, temp->url);

    /* Free any malloc'ed memory other than the outgoing BMI address */
    free(temp);

    return ret;
}

/** SID_UPDATE
 *
 * This function updates a record in the sid cache to any new values 
 * (attributes, bmi address, or url) that are in the SID_cacheval_t parameter
 * to this function if a record with a SID matching the sid_server parameter
 * is found in the sidcache
 *
 * Returns 0 on success, otherwise returns error code
 */
int SID_cache_update_server(DB *dbp,
                            const PVFS_SID *sid_server,
                            SID_cacheval_t *new_attrs,
                            uint32_t sid_types)
{
    int ret = 0;                   /* Function return value */
    SID_cacheval_t *current_attrs; /* Temp SID_cacheval_t used to get current 
                                      attributes of the sid from the sid */
   
    if(new_attrs == NULL)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                     "The new attributes passed to "
                     "SID_cache_update_server is NULL\n");
        return(-1);
    }
 
    /* Getting the sid from the sid cache */
    ret = SID_cache_lookup_server(dbp, sid_server, &current_attrs);
    if(ret)
    {
        return(ret);
    }

    /* Updating the old attributes to the new values */
    SID_cache_copy_attrs(current_attrs, new_attrs->attr);

    /* Updating the old bmi address to the new bmi_address */
    SID_cache_copy_bmi(current_attrs, new_attrs->bmi_addr);
   
    /* Updating the old url to the new url */
    SID_cache_copy_url(&current_attrs, new_attrs->url);

/* SHOULD NOT NEED THIS */
#if 0
    /* Deleting the old record from the sid cache */
    ret = SID_cache_delete_server(dbp, sid_server, NULL);
    if(ret)
    {
        return(ret);
    }
#endif
    
    /* writing the updated SID_cacheval_t struct back to the sid cache */
    ret = SID_cache_add_server(dbp, sid_server, current_attrs, NULL);
    if(ret)
    {
        return(ret);
    }

    /* Update the type db with the type bitfield provided */
    ret = SID_cache_update_type(sid_server, sid_types);
    if(ret)
    {
        return(ret);
    }
        
    /* Freeing the dynamically created bmi address in the SID_cacheval_t
     * struct 
     */
    SID_cacheval_free(&current_attrs);

    return(ret);
}

/** I don't understand what this function is for
 *  seems like we already have an update function, and there doesn't
 *  seem to be a reason to delete an old record and insert a new one
 *  if we can update.  Also return code is sketch.
 *  Maybe this should be a helper to update attrs in memory?
 *
 * This function updates the attributes for a sid in the database if a sid
 * with a matching sid_server parameter is found in the database
 *
 * Returns 0 on success, otherwise returns an error code
 */
int SID_cache_update_attrs(DB *dbp,
                           const PVFS_SID *sid_server,
                           int new_attr[])
{
    int ret = 0;
    int i = 0;   /* Loop index variable */
    SID_cacheval_t *sid_data;

    /* If sid_server is not passed in as NULL then only the attr's 
     * will be changed for the sid 
     */
    if(!dbp || !sid_server)
    {
        return(-EINVAL);
    }
    /* Get the sid from the cache */
    ret = SID_cache_lookup_server(dbp, sid_server, &sid_data);
    if(ret)
    {
        return(ret);
    }
        
    for(i = 0; i < SID_NUM_ATTR; i++)
    {
        /* a -1 indicates the new_attr does not need an update */
        if(new_attr[i] != -1)
        {
            sid_data->attr[i] = new_attr[i];
        }
    }

/* SHOULD NOT NEED THIS */
#if 0
    /* Deleting the old record from the cache */
    ret = SID_cache_delete_server(dbp, sid_server, NULL);
    if(ret)
    {
        return(ret);
    }
#endif

    ret = SID_cache_add_server(dbp, sid_server, sid_data, NULL);
    if(ret)
    {
        SID_cacheval_free(&sid_data);
        return(ret);
    }

    SID_cacheval_free(&sid_data);
    return(ret);    
}

/* The attr's are being updated with other attributes with
 * SID_cache_update_server
 * function, so only the current_sid_attrs need to change 
 */
int SID_cache_copy_attrs(SID_cacheval_t *current_attrs,
                         int new_attr[])
{
    int ret = 0;
    int i;

    for(i = 0; i < SID_NUM_ATTR; i++)
    {
        current_attrs->attr[i] = new_attr[i];
    }
    return(ret);
}

/* Add a type to an existing server in the SIDcache
 * The DB_NODUPDATA flag suppresses an extra error message that would
 * occur if attempt to put a dup key/val pair (IOW the exact same
 * record, not just the same key) which could happen here.  We still get
 * an error code but we can test for that.
 */
int SID_cache_update_type(const PVFS_SID *sid_server, uint32_t new_type_val)
{
    int ret = 0;
    DBT type_key;
    DBT type_val;
    uint32_t mask = 0;
    SID_type_db_key kbuf;

    kbuf.sid = *sid_server;

    SID_zero_dbt(&type_key, &type_val, NULL);

    type_key.data = &kbuf;
    type_key.size = sizeof(SID_type_db_key);
    type_key.ulen = sizeof(SID_type_db_key);

#if 0
    type_val.data = &(kbuf.typeval);
    type_val.size = sizeof(uint32_t);
    type_val.ulen = sizeof(uint32_t);
#endif

    for(mask = 1; mask != 0 && new_type_val != 0; mask <<= 1)
    {
        if (new_type_val & mask)
        {
            if ((mask & SID_SERVER_VALID_TYPES))
            {
                kbuf.typeval = mask;
                ret = SID_type_db->put(SID_type_db,
                                       NULL,
                                       &type_key,
                                       &type_val,
                                       0);
                /* if KEYEXIST it was just a duplicate - keep going */
                if (!ret && ret != DB_KEYEXIST)
                {
                    break;
                }
            }
            new_type_val &= ~mask;
        }
    }
 
    return(ret);    
}

/*
 * This function updates the bmi address for a sid in the database if a sid
 * with a matching sid_server parameter is found in the database
 *
 * Returns 0 on success, otherwise returns an error code
 */
int SID_cache_update_bmi(DB *dbp,
                         const PVFS_SID *sid_server,
                         BMI_addr new_bmi_addr)
{
    int ret = 0;
    SID_cacheval_t *sid_attrs;

    /* If sid_server is not passed in as NULL then only the bmi address 
     * will be changed for the sid 
     */
    if(!dbp || !sid_server)
    {
        return(-EINVAL);
    }
    /* Getting the sid from the sid cache */
    ret = SID_cache_lookup_server(dbp, sid_server, &sid_attrs);
    if(ret)
    {
        return(ret);
    }
        
    if(sid_attrs->bmi_addr != new_bmi_addr)
    {
        sid_attrs->bmi_addr = new_bmi_addr;

/* SHOULD NOT NEED THIS */
#if 0
        /* Deleting the old record from the sid cache */
        ret = SID_cache_delete_server(dbp, sid_server, NULL);
        if(ret)
        {
            return(ret);
        }
#endif

        ret = SID_cache_add_server(dbp, sid_server, sid_attrs, NULL);
        if(ret)
        {
            SID_cacheval_free(&sid_attrs);
            return(ret);
        }
    }

    SID_cacheval_free(&sid_attrs);
    return(ret);    
}

/* The bmi address is being updated with other attributes with
 * SID_cache_update_server
 * function, so only the current_sid_attrs need to change 
 */
int SID_cache_copy_bmi(SID_cacheval_t *current_attrs, BMI_addr new_bmi_addr)
{
    int ret = 0;

    current_attrs->bmi_addr = new_bmi_addr;
    return(ret);
}

/*
 * This function updates the url address for a sid in the database if a sid
 * with a matching sid_server parameter is found in the database
 *
 * Returns 0 on success, otherwise returns an error code
 */
int SID_cache_update_url(DB *dbp, const PVFS_SID *sid_server, char *new_url)
{
    int ret = 0;
    int tmp_attrs[SID_NUM_ATTR];
    BMI_addr tmp_bmi;
    SID_cacheval_t *sid_attrs;

    if(!dbp || !sid_server || !new_url)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                     "The url passed into SID_cache_update_url "
                     "function is currently NULL\n");
        return(-EINVAL);
    }

    /* Getting the sid from the sid cache */
    ret = SID_cache_lookup_server(dbp, sid_server, &sid_attrs);
    if(ret)
    {
        return(ret);
    }
   
    if(!(strcmp(sid_attrs->url, new_url)))
    {
        /* The new url is the same as the current */
        /* url so no changes need to happen */
        SID_cacheval_free(&sid_attrs);
        return(ret);
    }
    else
    {
        memcpy(tmp_attrs, sid_attrs->attr, sizeof(int) * SID_NUM_ATTR);
        memcpy(&tmp_bmi, &(sid_attrs->bmi_addr), sizeof(BMI_addr));
            
        SID_cacheval_free(&sid_attrs);
        ret = SID_cacheval_alloc(&sid_attrs, tmp_attrs, tmp_bmi, new_url);    
        if(ret)
        {
            return(ret);
        }        

/* SHOULD NOT NEED THIS */
#if 0
        /* Deleting the old record from the sid cache */
        ret = SID_cache_delete_server(dbp, sid_server, NULL);
        if(ret)
        {
            return(ret);
        }
#endif

        /* Writing the updated SID_cacheval_t struct back to the sid cache */
        ret = SID_cache_add_server(dbp, sid_server, sid_attrs, NULL);
        if(ret)
        {
            SID_cacheval_free(&sid_attrs);
            return(ret);
        }
    
        SID_cacheval_free(&sid_attrs);
        return(ret);    
    }
}


/* The url is being updated with other attributes with
 * SID_cache_update_server
 * function, so only the current_sid_attrs need to change 
 */
int SID_cache_copy_url(SID_cacheval_t **current_attrs, char *new_url)
{
    int ret = 0;
    BMI_addr tmp_bmi;
    int tmp_attrs[SID_NUM_ATTR];

    /* The new new url is the same as the old url so nothing needs
     * to be updated
     */
    if((!strcmp((*current_attrs)->url, new_url)) || (!new_url))
    {
        return(ret);
    }
    else
    {
        memcpy(tmp_attrs,
               (*current_attrs)->attr,
               sizeof(int) * SID_NUM_ATTR);
        memcpy(&tmp_bmi,
               &((*current_attrs)->bmi_addr),
               sizeof(BMI_addr));
        
        /* Freeing the current SID_cacheval_t struct to change the
         * url address to the new address 
         */
        SID_cacheval_free(current_attrs);
        
        /* Setting the current bmi address to the new values */
        ret = SID_cacheval_alloc(current_attrs, tmp_attrs, tmp_bmi, new_url);
        if(ret)
        {
            return(ret);
        }
            
        return(ret);
    }
}

/** SID_REMOVE
  *
  * This function deletes a record from the sid cache if a sid with a matching
  * sid_server parameter is found in the database
  *
  * Returns 0 on success, otherwise returns an error code 
  */
int SID_cache_delete_server(DB *dbp,
                            const PVFS_SID *sid_server,
                            int *db_records)
{
    int ret = 0; /* Function return value */
    DBT key;     /* Primary sid key  */

    /* Initializing the DBT key value */
    SID_zero_dbt(&key, NULL, NULL);
    
    /* Setting the values of DBT key to point to the sid to 
     * delete from the sid cache 
     */
    key.data = (PVFS_SID *)sid_server;
    key.size = sizeof(PVFS_SID);

    ret = (dbp)->del(dbp,  /* Primary database (sid cache) pointer */
                     NULL, /* Transaction Handle */
                     &key, /* SID_cacheval_t key */
                     0);   /* Delete flag */
    if(ret)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                     "Error deleting record from sid cache : %s\n",
                     db_strerror(ret));
        return(ret);
    }
  
    if(db_records != NULL)
    { 
        *db_records -= 1;
    }

    return(ret);
}

/* loop over records matching sid and write type data */
int SID_type_store(PVFS_SID *sid, FILE *outpfile)
{
    int ret = 0;
    DBT type_sid_key;
    DBT type_sid_val;
    char *buff;
    SID_type_index_key kbuf;

    kbuf.sid = *sid;

    SID_zero_dbt(&type_sid_key, &type_sid_val, NULL);

    type_sid_key.data = &kbuf;
    type_sid_key.size = sizeof(PVFS_SID);
    type_sid_key.ulen = sizeof(SID_type_index_key);

    ret = SID_index_cursor->get(SID_index_cursor,
                                   &type_sid_key,
                                   &type_sid_val,
                                   DB_SET);

    /* halt on error or DB_NOTFOUND */
    if (ret == 0)
    {
        fprintf(outpfile, "\t\tType ");
    }
    while(ret == 0)
    {
        /* write type to file */
        buff = SID_type_to_string(kbuf.typeval);
        fprintf(outpfile, "%s ", buff);
        /* should have been reset by get */
        type_sid_key.size = sizeof(PVFS_SID);
        ret = SID_index_cursor->get(SID_index_cursor,
                                       &type_sid_key,
                                       &type_sid_val,
                                       DB_NEXT);
    }

    fprintf(outpfile, "\n");
    /* Normal return should be DB_NOTFOUND, 0 is no error */
    if (ret != 0 && ret != DB_NOTFOUND)
    {
        return ret;
    }
    return 0;
}

/** SID_STORE
 *
 * This function writes the contents of the sid cache in ASCII to the file
 * specified through the outpfile parameter parameter
 *
 * Returns 0 on success, otherwise returns error code
 *
 * Use open_memstream to write to a message buffer
 */
int SID_cache_store(DBC *cursorp,
                    FILE *outpfile,
                    int db_records,
                    SID_server_list_t *sid_list)
{
    int ret = 0;                   /* Function return value */
    int i = 0;                     /* Loop index variable */
    DBT key, data;          	   /* Database variables */
/* V3 old version */
#if 0
    char *ATTRS = "ATTRS: ";       /* Tag for the number of attributes on */
                                   /*     the first line of dump file */
    char *SIDS = "SIDS: ";         /* Tag for the number of sids on the */
                                   /*     first line of the dump file */
#endif
    char tmp_sid_str[SID_STR_LEN]; /* Temporary string to hold the string */
                                   /*     representation of the sids */
    PVFS_SID tmp_sid;              /* Temporary SID to hold contents of */
                                   /*     the database */
    SID_cacheval_t *tmp_sid_attrs; /* Temporary SID_cacheval_t struct to */
                                   /*     hold contents of database */
    PVFS_SID sid_buffer;           /* Temporary SID */
    uint32_t db_flags;
    struct server_configuration_s *config_s = NULL;

    /* First Write SID File Header */
    fprintf(outpfile, "<ServerDefines>\n");

/* V3 old version */
#if 0
    /* Write the number of attributes in
     * the cache to the dump file's first line 
     */
    fprintf(outpfile, "%s", ATTRS);
    fprintf(outpfile, "%d\t", SID_NUM_ATTR);

    /* Write the number of sids in the cache
     * to the dump file's first line 
     */
    fprintf(outpfile, "%s", SIDS);
    fprintf(outpfile, "%d\n", db_records);
    
    /* Write the string representation of
     * the attributes in the sid cache to
     * the dump file's second line 
     */
    for(i = 0; i < SID_NUM_ATTR; i++)
    {
       fprintf(outpfile, "%s ", SID_attr_map[i]);
    }
    fprintf(outpfile, "%c", '\n');
#endif

    /* Now Write SID Records */

    /* Initialize the database variables */
    SID_zero_dbt(&key, &data, NULL);

    /* this routine can either output a whole db or a list of SIDs
     */
    if (sid_list)
    {
        db_flags = DB_SET;
        key.data = &sid_buffer;
        key.size = sizeof(PVFS_SID);
        SID_pop_query_list(sid_list, &sid_buffer, NULL, NULL, 0);
    }
    else
    {
        db_flags = DB_FIRST;
    }

    config_s = PINT_server_config_mgr_get_config(PVFS_FS_ID_NULL);

    /* Iterate over the database to get the sids */
    while(cursorp->get(cursorp, &key, &data, db_flags) == 0)
    {
        char *alias;
        SID_cacheval_unpack(&tmp_sid_attrs, &data);
    
        PVFS_SID_cpy(&tmp_sid, (PVFS_SID *)key.data);
        PVFS_SID_bin2str(&tmp_sid, tmp_sid_str);            

        /* this is a sequential search - probably should make hash tbl */
        alias = PINT_config_get_host_alias_ptr(config_s, tmp_sid_attrs->url);

        fprintf(outpfile,"\t<ServerDef>\n");
        if (alias)
        {
            fprintf(outpfile,"\t\tAlias %s\n", alias);
        }
        fprintf(outpfile,"\t\tSID %s\n", tmp_sid_str);
        fprintf(outpfile,"\t\tAddress %s\n", tmp_sid_attrs->url);

/* V3 old version */
#if 0
        /* Write SID and address info */
        fprintf(outpfile, "%s ", tmp_sid_str);
        fprintf(outpfile, "%lld ", lld(tmp_sid_attrs->bmi_addr));
        fprintf(outpfile, "%s ", tmp_sid_attrs->url);
#endif

        /* Write the user attributes to the dump file */
        fprintf(outpfile,"\t\tAttributes ");
        for(i = 0; i < SID_NUM_ATTR; i++)
        {
            
            fprintf(outpfile,
                    "%s=%d ",
                    SID_attr_map[i],
                    tmp_sid_attrs->attr[i]);      
/* V3 old version */
#if 0
            fprintf(outpfile, "%d ", tmp_sid_attrs->attr[i]);      
#endif
        }
        fprintf(outpfile,"\n"); /* end of attributes */

        /* Write system attributes for the server */
        SID_type_store(&tmp_sid, outpfile);

        fprintf(outpfile, "\t</ServerDef>\n");

        SID_cacheval_free(&tmp_sid_attrs);

        if (sid_list)
        {
            if (qlist_empty(&sid_list->link))
            {
                break;
            }
            SID_pop_query_list(sid_list, &sid_buffer, NULL, NULL, 0);
        }
        else
        {
            db_flags = DB_NEXT;
        }
    }

    /* End of servers to write to file */
    fprintf(outpfile, "</ServerDefines>\n");
    return(ret);
}


/*
 * This function retrieves entries from a primary database and stores
 * them into a bulk buffer DBT. 
 *
 * The minimum amount that can be retrieved is 8 KB of entries.
 * 
 * You must specify a size larger than 8 KB by putting an amount into the
 * two size parameters.
 *
 * The output DBT is malloc'ed so take care to make sure it is freed,
 * either by using it in a bulk_insert or by manually freeing.
 *
 * If the cache is larger than the buffer, the entry that does not fit is
 * saved in bulk_next_key global variable.
*/
int SID_bulk_retrieve_from_sid_cache(int size_of_retrieve_kb,
                                     int size_of_retrieve_mb,
                                     DB  *dbp,
                                     DBC **dbcursorp,
                                     DBT *output)
{
    int ret = 0, size;
    DBT key = bulk_next_key;

    /* If the input size of the retrieve is
     * smaller than the minimum size then exit function with error 
     */
    if (BULK_MIN_SIZE > (size_of_retrieve_kb * KILOBYTE) +
                        (size_of_retrieve_mb * MEGABYTE))
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                  "Size of bulk retrieve buffer must be greater than 8 KB\n");
        return (-1);
    }

    /* If cursor is open, close it so we can reopen it as a bulk cursor */
    if (*dbcursorp != NULL)
    {
        (*dbcursorp)->close(*dbcursorp);
    }

    /* Calculate size of buffer as size of kb + size of mb */
    size = (size_of_retrieve_kb * KILOBYTE) + (size_of_retrieve_mb * MEGABYTE);

    /* Malloc buffer DBT */
    output->data = malloc(size);
    if (output->data == NULL)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG, "Error sizing buffer\n");
        return (-1);
    }

    output->ulen = size;
    output->flags = DB_DBT_USERMEM;

    /* Open bulk cursor */
    if ((ret = dbp->cursor(dbp, NULL, dbcursorp, DB_CURSOR_BULK)) != 0) {
        free(output->data);
        gossip_debug(GOSSIP_SIDCACHE_DEBUG, "Error creating bulk cursor\n");
        return (ret);
    }

    /* Get items out of db as long as no error,
     * if error is not end of entries in db then save 
     * last unstored entry into global bulk_next_key
     */
    while(ret == 0)
    {
        ret = (*dbcursorp)->get(*dbcursorp,
                                &key,
                                output,
                                DB_MULTIPLE_KEY | DB_NEXT);
        if (ret != 0 && ret != DB_NOTFOUND)
        {
            bulk_next_key = key;
        }
    }

    (*dbcursorp)->close(*dbcursorp);

    return ret;
}


/*
 * This function inserts entries from the input bulk buffer DBT into a database.
 *
 * The function uses the output from SID_bulk_retrieve as its input.
 *
 * The malloc'ed bulk buffer DBT's are freed at the end of this function.
 */
int SID_bulk_insert_into_sid_cache(DB *dbp, DBT *input)
{
    int ret;
    void *opaque_p, *op_p;
    DBT output;
    size_t retklen, retdlen;
    void *retkey, *retdata;

    /* Malloc buffer DBT */
    output.data = malloc(input->size);
    output.ulen = input->size;
    output.flags = DB_DBT_USERMEM;

    /* Initialize bulk DBTs */
    DB_MULTIPLE_WRITE_INIT(opaque_p, &output);
    DB_MULTIPLE_INIT(op_p, input);

    /*
     * Read key and data from input bulk buffer into output bulk buffer
     *
     */
    while(1)
    {
        DB_MULTIPLE_KEY_NEXT(op_p,
                             input,
                             retkey,
                             retklen,
                             retdata,
                             retdlen);
        if (op_p == NULL)
        {
            break;
        }

        DB_MULTIPLE_KEY_WRITE_NEXT(opaque_p,
                                   &output,
                                   retkey,
                                   retklen,
                                   retdata,
                                   retdlen);
        if (opaque_p == NULL)
        {
            gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                           "Error cannot fit into write buffer\n");
            break;
        }
    }

    /* Bulk insert of output bulk buffer */
    ret = dbp->put(dbp,
                   NULL,
                   &output,
                   NULL,
                   DB_MULTIPLE_KEY | DB_OVERWRITE_DUP);

    /* Free both bulk buffers */
    free(input->data);
    free(output.data);

    return ret;
}

/* <======================== DATABASE FUNCTIONS ==========================> */
/*
 * This function zeros out the DBT's and should be used before any
 * element is placed into the database
 */
void SID_zero_dbt(DBT *key, DBT *val, DBT *pkey)
{
    if(key != NULL)
    {
        memset(key, 0, sizeof(DBT));
    }
    if(val != NULL)
    {
        memset(val, 0, sizeof(DBT));
    }
    if(pkey != NULL)
    {
        memset(pkey, 0, sizeof(DBT));
    }
}

/*
 * This function creates and opens the environment handle
 *
 * Returns 0 on success, otherwise returns an error code.
 */
int SID_create_environment(DB_ENV **envp)
{
    int ret = 0;

    /* Setting the opening environement flags */
    u_int32_t flags =
        DB_CREATE     | /* Creating environment if it does not exist */
        DB_INIT_MPOOL | /* Initializing the memory pool (in-memory cache) */
        DB_PRIVATE;     /* Region files are not backed up by the file system
                           instead they are backed up by heap memory */

    /* Creating the environment. Returns 0 on success */
    ret = db_env_create(envp, /* Globacl environment pointer */
                        0);    /* Environment create flag (Must be set to 0) */

    if(ret)
    {
        gossip_err("Error creating environment handle : %s\n", db_strerror(ret));
        return(ret);
    }

    /* Setting the in cache memory size. Returns 0 on success */
    ret = (*envp)->set_cachesize(
                        /* Environment pointer */
                        *envp,
                        /* Size of the cache in GB. Defined in sidcache.h */
                        CACHE_SIZE_GB,
                        /* Size of the cache in MB. Defined in sidcache.h */
                        CACHE_SIZE_MB * MEGABYTE,
                        /* Number of caches to create */
                        1);

    if(ret)
    {
	gossip_err(
              "Error setting cache memory size for environment : %s\n",
              db_strerror(ret));
        return(ret);
    }

    /* Opening the environment. Returns 0 on success */
    ret = (*envp)->open(*envp, /* Environment pointer */
                        NULL,  /* Environment home directory */
                        flags, /* Environment open flags */
                        0);    /* Mmode used to create BerkeleyDB files */

    if(ret)
    {
        gossip_err("Error opening environment database handle : %s\n",
                   db_strerror(ret));
        return(ret);
    }

    return(ret);
}

/*
 * This function creates and opens the primary database handle, which
 * has DB_HASH as the access method
 *
 * Returns 0 on success. Otherwise returns error code.
 */
int SID_create_sid_cache(DB_ENV *envp, DB **dbp)
{
    int ret = 0;

    u_int32_t flags = DB_CREATE; /* Database open flags. Creates the database if
                                    it does not already exist */

    /* Setting the global bulk next key to zero */
    SID_zero_dbt(&bulk_next_key, NULL, NULL);

    ret = db_create(dbp,   /* Primary database pointer */
                    envp,  /* Environment pointer */
                    0);    /* Create flags (Must be set to 0 or DB_XA_CREATE) */
    if(ret)
    {
        gossip_err("Error creating handle for database : %s\n",
                   db_strerror(ret));
        return(ret);
    }

    ret = (*dbp)->open(*dbp,    /* Primary database pointer */
                       NULL,    /* Transaction pointer */
                       NULL,    /* On disk file that holds database */
                       NULL,    /* Optional logical database */
                       DB_HASH, /* Database access method */
                       flags,   /* Database opening flags */
                       0);      /* File mode. Default is 0 */
   if(ret)
   {
        gossip_err("Error opening handle for database : %s\n",
                   db_strerror(ret));
        return(ret);
   }

   return(ret);
}

/* 
 * This function creates, opens, and associates the secondary attribute
 * database handles and sets the database pointers in the secondary_dbs 
 * array to point at the correct database. The acccess method for the
 * secondary databases is DB_BTREE and allows for duplicate records with
 * the same key values
 *
 * Returns 0 on success, otherwise returns an error code
 */
int SID_create_secondary_dbs(
                        DB_ENV *envp,
                        DB *dbp,
                        DB *secondary_dbs[], 
                        int (* key_extractor_func[])(DB *pri,
                                                     const DBT *pkey,
                                                     const DBT *pdata,
                                                     DBT *skey))
{
    int ret = 0;
    int i = 0;
    DB *tmp_db = NULL;
    u_int32_t flags = DB_CREATE;

    ret = SID_initialize_secondary_dbs(secondary_dbs);
    if(ret)
    {
        gossip_err("Could not initialize secondary atttribute db array\n");
        return(ret);
    }

    for(i = 0; i < SID_NUM_ATTR; i++)
    {
        /* Creating temp database */
        ret = db_create(&tmp_db, /* Database pointer */
                        envp,    /* Environment pointer */
                        0);      /* Create flags (Must be 0 or DB_XA_CREATE) */
        if(ret)
        {
            gossip_err("Error creating handle for database :  %s\n",
                       db_strerror(ret));
            return(ret);
        }

        /* Pointing database array pointer at secondary attribute database */
        secondary_dbs[i] = tmp_db;

        /* Setting opening flags for secondary database to allow duplicates */
        ret = tmp_db->set_flags(tmp_db, DB_DUPSORT);
        if(ret)
        {
            gossip_err("Error setting duplicate flag for database : %s\n",
                       db_strerror(ret));
            return(ret);
        }

        /* Opening secondary database and setting its type as DB_BTREE */
        ret = tmp_db->open(tmp_db,   /* Database pointer */
                           NULL,     /* Transaction pointer */
                           NULL,     /* On disk file that holds database */
                           NULL,     /* Optional logical database */
                           DB_BTREE, /* Database access method */
                           flags,    /* Open flags */
                           0);       /* File mode. 0 is the default */

        if(ret)
        {
            gossip_err("Error opening handle for database : %s\n",
                       db_strerror(ret));
            return(ret);
        }

        /* Associating the primary database to the secondary.
           Returns 0 on success */
        ret = dbp->associate(dbp,                      /* Primary db ptr */
                             NULL,                     /* TXN id */
                             tmp_db,                   /* Secondary db ptr */
                             key_extractor_func[i],    /* key extractor func */
                             0);                       /* Associate flags */

        if(ret)
        {
            gossip_err("Error associating the two databases %s\n",
                        db_strerror(ret));
            return(ret);
        }

    }

   return(0);
}

/* get the aggregate SID type for a given SID */
int SID_get_type(PVFS_SID *sid, uint32_t *typeval)
{
    int ret = 0;
    DBT type_sid_key;
    DBT type_sid_val;
    SID_type_index_key kbuf;

    SID_zero_dbt(&type_sid_key, &type_sid_val, NULL);

    type_sid_key.data = &kbuf;
    type_sid_key.size = sizeof(PVFS_SID);
    type_sid_key.ulen = sizeof(SID_type_index_key);

    ret = SID_index_cursor->get(SID_index_cursor,
                                   &type_sid_key,
                                   &type_sid_val,
                                   DB_SET);
    /* don't modify typeval unless there is data present */
    if (ret == 0)
    {
        *typeval = 0;
    }
    /* halt on error or DB_NOTFOUND */
    while(ret == 0)
    {
        *typeval |= kbuf.typeval;
        /* should have been reset byt get */
        type_sid_key.size = sizeof(PVFS_SID);
        ret = SID_index_cursor->get(SID_index_cursor,
                                       &type_sid_key,
                                       &type_sid_val,
                                       DB_NEXT);
    }
    if (ret != 0 && ret != DB_NOTFOUND)
    {
        return ret;
    }
    return 0;
}

/*-------------------------------------------------------------------
 * The server type tables map SID to individual types (one bit set in a
 * field).  There will often be multiple types for a given SID, and many
 * SIDs with the same type.  We need to be able to look these up by SID,
 * or by type so we have a main db (SID_type_db) and a secondary index
 * (SID_type_index) which is associated with the main db.  The main
 * db cannot be configured with dups, so we will make the key of
 * these tables type concatenated with SID, which is unique, for the
 * main db and SID concatenated with type, which is unique, for the
 * secondary index.  Neither will have anything in the value field.
 * Using this we can use a partial lookup to either find all types for a
 * SID, or all SIDs for a type.
 * ------------------------------------------------------------------
 */

/* Extractor for type database secondard index */
static int SID_type_sid_extractor(DB *pri,
                                  const DBT *pkey,
                                  const DBT *pval,
                                  DBT *skey)
{
    SID_type_index_key *sixbuf;
    SID_type_db_key *dbbuf;

    sixbuf = (SID_type_index_key *)malloc(sizeof(SID_type_index_key));
    dbbuf = (SID_type_db_key *)pkey->data;

    sixbuf->sid = dbbuf->sid;
    sixbuf->typeval = dbbuf->typeval;

    skey->data = sixbuf;
    skey->size = sizeof(SID_type_index_key);
    skey->flags = DB_DBT_APPMALLOC;
    return 0;
}

/* Creates table for storing server types.  There may be multiple types
 * for each server, but most servers are not all types.  We need to be
 * able to search for all servers of a given type.  We can't do this
 * easily with a field in the main table or by using a secondary index
 * so we record these attributes in this table.
 */
static int SID_create_type_table(void)
{
    int ret = 0;
    u_int32_t flags = DB_CREATE;

    /* Create type database */
    ret = db_create(&SID_type_db, /* Database pointer */
                    SID_envp,    /* Environment pointer */
                    0);          /* Create flags (Must be 0 or DB_XA_CREATE) */
    if(ret)
    {
        gossip_err("Error creating type database :  %s\n",
                   db_strerror(ret));
        return(ret);
    }

#if 0
    /* Set open flags for type database to allow duplicates */
    ret = SID_type_db->set_flags(SID_type_db, DB_DUPSORT);
    if(ret)
    {
        gossip_err("Error setting duplicate flag for type database : %s\n",
                   db_strerror(ret));
        return(ret);
    }
#endif

    /* Open type database */
    ret = SID_type_db->open(SID_type_db,   /* Database pointer */
                            NULL,     /* Transaction pointer */
                            NULL,     /* On disk file that holds database */
                            NULL,     /* Optional logical database */
                            DB_HASH,  /* Database access method */
                            flags,    /* Open flags */
                            0);       /* File mode. 0 is the default */

    if(ret)
    {
        gossip_err("Error opening type database : %s\n",
                   db_strerror(ret));
        return(ret);
    }

    /* Create cursor for type db */
    ret = SID_type_db->cursor(SID_type_db,      /* Type db pointer */
                              NULL,             /* TXN id */
                              &SID_type_cursor, /* Cursor pointer */
                              0);               /* Cursor opening flags */
    if(ret)
    {
        gossip_err("Error creating cursor :  %s\n", db_strerror(ret));
        return(ret);
    }

    /* Now create a secondary index for looking up types by SID */
    ret = db_create(&SID_type_index, /* SID index pointer */
                    SID_envp,    /* Environment pointer */
                    0);          /* Create flags (Must be 0 or DB_XA_CREATE) */
    if(ret)
    {
        gossip_err("Error creating handle for database :  %s\n",
                   db_strerror(ret));
        return(ret);
    }

#if 0
    /* Set open flags for type SID index to allow duplicates */
    ret = SID_type_index->set_flags(SID_type_index, DB_DUPSORT);
    if(ret)
    {
        gossip_err("Error setting duplicate flag for type SID index : %s\n",
                   db_strerror(ret));
        return(ret);
    }
#endif

    /* Open type SID index database */
    ret = SID_type_index->open(SID_type_index,   /* Database pointer */
                            NULL,     /* Transaction pointer */
                            NULL,     /* On disk file that holds database */
                            NULL,     /* Optional logical database */
                            DB_HASH,  /* Database access method */
                            flags,    /* Open flags */
                            0);       /* File mode. 0 is the default */
    if(ret)
    {
        gossip_err("Error opening type SID index : %s\n",
                   db_strerror(ret));
        return(ret);
    }

    /* Associate the type database to the type SID index */
    ret = SID_type_index->associate(SID_type_db,   /* Type db ptr */
                         NULL,                     /* TXN id */
                         SID_type_index,           /* Secondary db ptr */
                         SID_type_sid_extractor,   /* key extractor func */
                         0);                       /* Associate flags */
    if(ret)
    {
        gossip_err("Error associating the type SID index %s\n",
                    db_strerror(ret));
        return(ret);
    }

    /* Create cursor for type SID index */
    ret = SID_type_index->cursor(SID_type_index,    /* SID index pointer */
                              NULL,                 /* TXN id */
                              &SID_index_cursor,    /* Cursor pointer */
                              0);                   /* Cursor opening flags */
    if(ret)
    {
        gossip_err("Error creating type SID cursor: %s\n", db_strerror(ret));
        return(ret);
    }
    return(ret);
}

/* 
 * This function creates and opens the database cursors set to the secondary
 * attribute databases in the database cursor pointers array
 *
 * Returns 0 on success, otherwise returns an error code
 */
int SID_create_dbcs(DB *secondary_dbs[], DBC *db_cursors[])
{
    int i = 0;                /* Loop index variable */
    int  ret = 0;             /* Function return value */
    DBC *tmp_cursorp = NULL;  /* BerkeleyDB cursor used for opening
                                 cursors for all secondary databases */

    /* Initializing all the database cursors in the array to be
     * set to NULL 
     */
    ret = SID_initialize_database_cursors(db_cursors);
    if(ret)
    {
        gossip_err("Error initializing cursor array :  %s\n", db_strerror(ret));
        return(ret);
    }

    for(i = 0; i < SID_NUM_ATTR; i++)
    {
        ret = secondary_dbs[i]->cursor(
                                secondary_dbs[i], /* Secondary db pointer */
                                NULL,             /* TXN id */
                                &tmp_cursorp,     /* Cursor pointer */
                                0);               /* Cursor opening flags */
        
        /* Checking to make sure the cursor was created */
        if(ret)
        {
            gossip_err("Error creating cursor :  %s\n", db_strerror(ret));
            return(ret);
        }
        /* If the cursor was successfully created it is added to the
         * array of database cursors 
         */
        else
        {
            db_cursors[i] = tmp_cursorp;
        }
    }
    return(ret);
}

/*
 * This function closes the database cursors in the cursors pointer array
 *
 * On success 0 is returned, otherwise returns an error code
 */
int SID_close_dbcs(DBC *db_cursors[])
{
    int i = 0;
    int ret = 0;

    for(i = 0; i < SID_NUM_ATTR; i++)
    {
        /* If the cursor is opened then it is closed */
        if(db_cursors[i] != NULL)
        {
            ret = db_cursors[i]->close(db_cursors[i]);
            if(ret)
            {
                gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                             "Error closing cursor :  %s\n", db_strerror(ret));
                return(ret);
            }
        }
    }
    return(ret);
}

/*
 * This function closes the primary database, secondary attribute databases,
 * and environment handles
 *
 * Returns 0 on success, otherwisre returns an error code
 */
int SID_close_dbs_env(DB_ENV *envp, DB *dbp, DB *secondary_dbs[])
{
    int ret = 0;
    int i = 0;

    /* Closing the array of secondary attribute databases */ 
    for(i = 0; i < SID_NUM_ATTR; i++)
    {
        if(secondary_dbs[i] != NULL)
        {

            ret = secondary_dbs[i]->close(secondary_dbs[i], /*Secondary db ptr*/
                                          0);         /* Database close flags */

            /* Checking to make sure the secondary database has been closed */
            if(ret)
            {
                gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                             "Error closing attribute database : %s\n",
                             db_strerror(ret));
                return(ret);
            }
        }
    }

    /* Checking to make sure that database handle has been opened */
    if(dbp != NULL)
    {
        /* Closing the primary database. Returns 0 on success */
        ret = dbp->close(dbp, /* Primary database pointer */
                         0);  /* Database close flags */
    }

     if(ret)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                     "Error closing primary database : %s\n", db_strerror(ret));
        return(ret);
    }


    /* Checking to make sure the environment handle is open */
    if(envp != NULL)
    {
        ret  = envp->close(envp, /* Environment pointer */
                           0);   /* Environment close flags */
    }

    if(ret)
    {
        gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                     "Error closing environment :  %s\n",
                     db_strerror(ret));
        return(ret);
    }

    return(ret);
}

/* <======================== EXPORTED FUNCTIONS ==========================> */

/* called from startup routines, this gets the SID cache up and running
 * should work on server and client
 */
int SID_initialize(void)
{
    int ret = -1;
    static int initialized = 0;

    /* if already initialized bail out - we assume higher levels
     * prevent multiple threads from initializing so we won't deal
     * with that here.
     */
    if (initialized)
    {
        ret = 0;
        goto errorout;
    }

    /* create DBD env */
    ret = SID_create_environment(&SID_envp);
    if (ret < 0)
    {
        goto errorout;
    }

    /* create main DB */
    ret = SID_create_sid_cache(SID_envp, &SID_db);
    if (ret < 0)
    {
        goto errorout;
    }

    /* create secondary (attribute index) DBs */
    ret = SID_create_secondary_dbs(SID_envp,
                                   SID_db,
                                   SID_attr_index,
                                   SID_extract_key);
    if (ret < 0)
    {
        goto errorout;
    }

    /* create server type table */
    ret = SID_create_type_table();
    if (ret < 0)
    {
        goto errorout;
    }

    /* create cursors */
    ret = SID_create_dbcs(SID_attr_index, SID_attr_cursor);
    if (ret < 0)
    {
        SID_close_dbs_env(SID_envp, SID_db, SID_attr_index);
        goto errorout;
    }

    /* load entries from the configuration */

errorout:
    return ret;
}

/* called to load the contents of the SID cache from a file
 * so we do not have to discover everything
 */
int SID_load(const char *path)
{
    int ret = -1;
    char *filename = NULL;
    FILE *inpfile = NULL;
//    struct server_configuration_s *srv_conf;
//    int fnlen;
    struct stat sbuf;
    PVFS_fs_id fsid __attribute__ ((unused)) = PVFS_FS_ID_NULL;

    if (!path)
    {
#if 0
        /* figure out the path to the cached data file */
        srv_conf = PINT_server_config_mgr_get_config(fsid);
        fnlen = strlen(srv_conf->meta_path) + strlen("/SIDcache");
        filename = (char *)malloc(fnlen + 1);
        strncpy(filename, srv_conf->meta_path, fnlen + 1);
        strncat(filename, "/SIDcache", fnlen + 1);
        PINT_server_config_mgr_put_config(srv_conf);
#endif
    }
    else
    {
        filename = (char *)path;
    }

    /* check if file exists */
    ret = stat(filename, &sbuf);
    if (ret < 0)
    {
        if (errno == EEXIST)
        {
            /* file doesn't exist - not an error, but bail out */
            errno = 0;
            ret = 0;
        }
        goto errorout;
    }

    /* Opening input file */
    inpfile = fopen(filename, "r");
    if(!inpfile)
    {
        gossip_err("Could not open the file %s in "
                   "SID_load_cache_from_file\n",
                   filename);
        return(-1);
    }

    /* load cache from file */
    ret = SID_cache_load(SID_db, inpfile, &sids_in_cache);
    if (ret < 0)
    {
        /* something failed, close up the database */
        SID_close_dbcs(SID_attr_cursor);
        SID_close_dbs_env(SID_envp, SID_db, SID_attr_index);
        goto errorout;
    }

    fclose(inpfile);

errorout:
    return ret;
}

/* This loads all of the records in the given memory buffer into the
 * cache.  This is intended for servers to pass groups of SID cache
 * records around
 */
int SID_loadbuffer(const char *buffer, int size)
{
    int ret = 0;
    FILE *inpfile = NULL;

    /* Opening the buffer as a file to load the contents of the database from */
    inpfile = fmemopen((void *)buffer, size, "r");
    if(!inpfile)
    {
        gossip_err("Error opening dump buffer in SID_loadbuffer function\n");
        return(-1);
    }
    /* load buffer into the cache */
    ret = SID_cache_load(SID_db, inpfile, &sids_in_cache);
    fclose(inpfile);
    return ret;
}

/* THis writes the entire contents of the cache into a file specified by
 * path.
 *
 * called periodically to save the contents of the SID cache to a file
 * so we can reload at some future startup and not have to discover
 * everything
 */
int SID_save(const char *path)
{
    int ret = -1;
    char *filename = NULL;
    FILE *outpfile = NULL;
    DBC *cursorp = NULL;
//    struct server_configuration_s *srv_conf;
//    int fnlen;
    PVFS_fs_id fsid __attribute__ ((unused)) = PVFS_FS_ID_NULL;

    if (!path)
    {
#if 0
        /* figure out the path to the cached data file */
        srv_conf = PINT_server_config_mgr_get_config(fsid);
        fnlen = strlen(srv_conf->meta_path) + strlen("/SIDcache");
        filename = (char *)malloc(fnlen + 1);
        strncpy(filename, srv_conf->meta_path, fnlen + 1);
        strncat(filename, "/SIDcache", fnlen + 1);
        PINT_server_config_mgr_put_config(srv_conf);
#endif
    }
    else
    {
        filename = (char *)path;
    }

    /* Opening the file to dump the contents of the database to */
    outpfile = fopen(filename, "w");
    if(!outpfile)
    {
        gossip_err("Error opening dump file in SID_save function\n");
        return(-1);
    }

    /* Creating a cursor to iterate over the database contents */
    ret = (SID_db)->cursor(SID_db,   /* Primary database pointer */
                           NULL,     /* Transaction handle */
                           &cursorp, /* Database cursor that is created */
                           0);       /* Cursor create flags */

    if(ret)
    {
        gossip_err("Error occured when trying to create cursor in \
                    SID_save function: %s",
                    db_strerror(ret));
        goto errorout;
    }

    /* dump cache to the file */
    ret = SID_cache_store(cursorp, outpfile, sids_in_cache, NULL);

errorout:

    if (cursorp)
    {
        ret = cursorp->close(cursorp);
        if(ret)
        {
            gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                         "Error closing cursor in SID_save "
                         "function: %s\n",
                         db_strerror(ret));
        }
    }
    fclose(outpfile);
    return ret;
}

/* this dumps the contents of the cache corresponding to the
 * SIDs in list into a memory buffer where it can then be dealt
 * with
 */
int SID_savelist(char *buffer, int size, SID_server_list_t *slist)
{
    int ret = -1;
    FILE *outpfile = NULL;
    DBC *cursorp = NULL;

    /* Opening the buffer as a file to dump the contents of the database to */
    outpfile = fmemopen(buffer, size, "w");
    if(!outpfile)
    {
        gossip_err("Error opening dump buffer in SID_savelist function\n");
        return(-1);
    }

    /* Creating a cursor to iterate over the database contents */
    ret = (SID_db)->cursor(SID_db,   /* Primary database pointer */
                           NULL,     /* Transaction handle */
                           &cursorp, /* Database cursor that is created */
                           0);       /* Cursor create flags */

    if(ret)
    {
        gossip_err("Error occured when trying to create cursor in \
                    SID_savelist function: %s",
                    db_strerror(ret));
        goto errorout;
    }

    /* dump cache to the buffer */
    ret = SID_cache_store(cursorp, outpfile, size, slist);

errorout:

    if (cursorp)
    {
        ret = cursorp->close(cursorp);
        if(ret)
        {
            gossip_debug(GOSSIP_SIDCACHE_DEBUG,
                         "Error closing cursor in SID_savelist "
                         "function: %s\n",
                         db_strerror(ret));
            return(ret);
        }
    }
    fclose(outpfile);
    return ret;
}

/* Add one entry to the SID cache - does not add attributes */
/* should we add a mechanism to add attributes? */
int SID_add(const PVFS_SID *sid,
            PVFS_BMI_addr_t bmi_addr,
            const char *url,
            int attributes[])
{
    int ret = 0;
    int i = 0;
    SID_cacheval_t *cval;

    cval = (SID_cacheval_t *)malloc(sizeof(SID_cacheval_t) + strlen(url) + 1);
    if(!cval)
    {
        return -1;
    }
    /* load up the cval */
    memset(cval, 0, sizeof(SID_cacheval_t) + strlen(url) + 1);
    cval->bmi_addr = bmi_addr;
    /* if bmi_addr is zero, should be register with BMI? */
    strcpy(cval->url, url);
    if (attributes)
    {
        /* this assumes we are adding a new record so we are not
         * overwriting existing attribtes
         */
        for (i = 0; i < SID_NUM_ATTR; i++)
        {
            cval->attr[i] = attributes[i];
        }
    }
    /* should we check for an existing server?
     * at this point if a server already exists this should fail and we
     * just move on to the next one - might want to do some kind of
     * update, not sure
     */
    ret = SID_cache_add_server(SID_db, sid, cval, &sids_in_cache);
    free(cval);
    return ret;
}

/* Delete one entry from the SID cache */
int SID_delete(const PVFS_SID *sid)
{
    int ret = 0;
    ret = SID_cache_delete_server(SID_db, sid, &sids_in_cache);
    return ret;
}

/* called from shutdown routines, this closes up the BDB constructs
 * and saves the contents to a file
 */
int SID_finalize(void)
{
    int ret = -1;

#if 0
    /* save cache contents to a file */
    ret = SID_save(NULL);
    if (ret < 0)
    {
        return ret;
    }
#endif

    /* close cursors */
    ret = SID_close_dbcs(SID_attr_cursor);
    if (ret < 0)
    {
        return ret;
    }

    /* close DBs and env */
    ret = SID_close_dbs_env(SID_envp, SID_db, SID_attr_index);
    if (ret < 0)
    {
        return ret;
    }

    return ret;
}

int SID_update_type(const PVFS_SID *sid, int new_server_type)
{
    int ret = 0;
    ret = SID_cache_update_type(sid, new_server_type);
    return ret;
}

int SID_update_attributes(const PVFS_SID *sid_server, int new_attr[])
{   
    int ret = 0;
    ret = SID_cache_update_attrs(SID_db, sid_server, new_attr);
    return ret;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
