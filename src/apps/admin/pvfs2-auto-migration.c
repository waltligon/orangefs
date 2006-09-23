/*
 * This tool supervises given servers and decides when a specific datafile 
 * can be migrated to another better server.
 * Initial revision: Julian M. Kunkel
 *  
 * (C) 2001 Clemson University and The University of Chicago
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <libgen.h>
#include <getopt.h>
#include <signal.h>

#include "pvfs2.h"
#include "pthread.h"
#include "str-utils.h"
#include "pint-sysint-utils.h"
#include "pvfs2-internal.h"
#include "pint-cached-config.h"
#include "red-black-tree.h"
#include "simplequeue.h"

/*
 * TODO: 
 * - load should also represent the number of currently enqueued but unprocessed ops.
 * - short term scheduling
 * - datastructure for metaserver 
 */

#define DFILE_KEY "system.pvfs2." DATAFILE_HANDLES_KEYSTR

#define HISTORY_SIZE 5

#define FATAL_ERR(x) fprintf(stderr, x); exit(1);
#define CONTINUE sleep(user_opts->refresh_time); \
            continue

/* optional parameters, filled in by parse_args() */
struct options
{
    char *filesystem;
    int refresh_time;
    int override;
    int verbose;
    
    float simulate_load;
    int simulate_server_no;
    
    int remove_old_entries_after_iterations; 
    
    float load_tolerance;    
    uint64_t    min_accessed_data;
};

const struct options default_options = 
{
    "/pvfs2", 
    30, /* refresh time */
    0,
    0,
    5.0, /* simulated load */
    0,
    100,
    0.5,
    64*1024 /* min accessed data per iteration*/
};


static struct options *parse_args(
    int argc,
    char *argv[]);
static void usage(
    int argc,
    char **argv);
    
#define print_load(x) (((double) x) / 60000.0)
#define debug(x)                \
        if (user_opts->verbose) \
        {                       \
            x                   \
        }                       

#define debugV(x)                   \
        if (user_opts->verbose > 1) \
        {                           \
            x                       \
        }                       


/*
 * needed for parallelization of requests to multiple servers 
 * with system interface test functions.  
 */

enum dataserver_state
{
    STATE_READY,
    STATE_MIGRATION_IN_PROGRESS,
    STATE_MIGRATION_RECENTLY_DONE
};

/* store a number of snapshots for later referal */
typedef struct 
{
    /*
     * iteration of 0 is not valid !
     */
    int age_iteration;
    int age_stats[HISTORY_SIZE];
    PVFS_handle handle;
    
    int current_stat_pos;
    PVFS_request_statistics stats[HISTORY_SIZE];
} handle_history_t;



typedef struct 
{ 
    pthread_mutex_t       state_mutex;
    enum dataserver_state state;
        
    char * alias;
    PVFS_BMI_addr_t bmi_address; 
    PVFS_handle_extent_array extend;
    
    struct PVFS_mgmt_server_stat * stats;
    
    red_black_tree                        handle_history;
    
    int attr_age_iteration;
    PVFS_sysresp_mgmt_get_scheduler_stats getschedstats_resp;
    double fs_old_accessed_size;
    
    PVFS_sysresp_getattr                  getattr_stats;
    
    PVFS_mgmt_op_id                       migration_id; 
    simplequeue_elem                    * migration_queue_link;
} dataserver_details;


struct history_free_iterator_t{
    red_black_tree * tree;
    int oldest_iteration_no;
};

static dataserver_details * servers = NULL;

static struct options *user_opts = NULL;

static int shutdown = 0;

static int dataserver_cnt = 0;

static pthread_mutex_t  migrating_dataservers_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   migration_in_progress_cond = PTHREAD_COND_INITIALIZER;
static int              migrating_dataservers_cnt = 0;
static simplequeue      migrating_dataservers;

static pthread_t        migration_thread;

    
/* function protypes */

static void migration_sig_handler(int sig);
static int migration_setup_signal_handlers(void);
void * background_migration_thread_func( void *ptr );

int lookup(
    char *pvfs2_file,
    PVFS_credentials * credentials,
    PVFS_fs_id * out_fs_id,
    PVFS_object_ref * out_object_ref,
    PVFS_sysresp_getattr * out_resp_getattr);

int free_history_iterator(tree_node * node, 
    struct history_free_iterator_t* iter_data);
    
void try_to_free_history_tree(red_black_tree * handle_history, 
    int oldest_iteration_no);



void put_sched_resp_into_history_tree_and_sort(PVFS_sysresp_mgmt_get_scheduler_stats * stats,
    red_black_tree * handle_history, int current_iteration, int * inout_max_interesting_cnt, 
    handle_history_t ** out_array);
    
void print_servers(const char * prefix, 
    int cnt, PVFS_BMI_addr_t * bmi, char ** aliases, 
    PVFS_handle_extent_array * extends);

void print_data_servers(const char * prefix, 
    int cnt, dataserver_details * servers);

int handle_history_cmp ( const handle_history_t ** stat_1, 
    const handle_history_t ** stat_2);

int server_stat_cmp ( const dataserver_details * stat_1, 
    const dataserver_details * stat_2);

int compare_internal( handle_history_t * stat_1, 
    handle_history_t * stat_2 );
    
int compare_lookup(handle_history_t * stat_1, PVFS_handle* key);

/* end function protypes */

void print_servers(const char * prefix, 
    int cnt, PVFS_BMI_addr_t * bmi, char ** aliases, 
    PVFS_handle_extent_array * extends)
{
    int i;
    printf("%s - count: %d\n", prefix, cnt);
    for(i=0; i < cnt; i++)
    {
        printf(" %s - %s - extends: %d (%lld-%lld)\n", aliases[i], 
            BMI_addr_rev_lookup(bmi[i]), 
            extends[i].extent_count, lld(extends[i].extent_array[0].first), 
            lld(extends[i].extent_array[0].last));
    }
}

int server_stat_cmp ( const dataserver_details * stat_1, 
    const dataserver_details * stat_2)
{
    return stat_1->stats[0].load_1 < stat_2->stats[0].load_1;
}

int compare_lookup(handle_history_t * stat_1, PVFS_handle* key)
{
    if( stat_1->handle > *key)
    {
        return -1;
    }
    else if(stat_1->handle < *key)
    {
        return 1;
    }
    return 0;
}

int compare_internal( handle_history_t * stat_1, 
    handle_history_t * stat_2 )
{
    if( stat_1->handle > stat_2->handle )
    {
        return -1;
    }
    else if(stat_1->handle < stat_2->handle )
    {
        return 1;
    }
    return 0;
}


void print_data_servers(const char * prefix, 
    int cnt, dataserver_details * servers)
{
    int i;
    printf("%s - count: %d\n", prefix, cnt);
    for(i=0; i < cnt; i++)
    {
        printf(" %s - %s - extends: %d (%lld-%lld)\n", servers[i].alias, 
            BMI_addr_rev_lookup(servers[i].bmi_address),
            servers[i].extend.extent_count, lld(servers[i].extend.extent_array[0].first), 
            lld(servers[i].extend.extent_array[0].last));
    }
}


int free_history_iterator(tree_node * node, 
    struct history_free_iterator_t* iter_data)
{
    handle_history_t * handle_info = node->data;
    
    if( handle_info->age_iteration < iter_data->oldest_iteration_no )
    {
        debugV ( printf("Freed: %lld\n", lld(handle_info->handle) ); ) 
        deleteNodeFromTree(node, iter_data->tree);
        free(handle_info);
        return 0;
    }
    return 0;
}

void try_to_free_history_tree(red_black_tree * handle_history,
    int oldest_iteration_no)
{
    struct history_free_iterator_t iter_data;
    iter_data.oldest_iteration_no = oldest_iteration_no;
    iter_data.tree = handle_history;
    iterate_red_black_tree_nodes((int(*)(tree_node*,void*)) free_history_iterator, 
        handle_history, & iter_data);
}

inline int get_old_pos(int pos)
{
    if (pos -1 != -1) 
    {
        return pos - 1;
    }
    else
    {
       return HISTORY_SIZE-1;
    }
}

inline double get_size_diff(int pos, int oldpos, const handle_history_t * history){
    return (double) /*(history->stats[pos].acc_multiplier[SCHED_LOG_READ] - 
        history->stats[oldpos].acc_multiplier[SCHED_LOG_READ]) * (double) ((uint64_t) -1) +
         
        history->stats[pos].acc_size[SCHED_LOG_READ] - 
        history->stats[oldpos].acc_size[SCHED_LOG_READ] +
        (history->stats[pos].acc_multiplier[SCHED_LOG_WRITE] - 
        history->stats[oldpos].acc_multiplier[SCHED_LOG_WRITE]) * (double) ((uint64_t) -1) + 
        history->stats[pos].acc_size[SCHED_LOG_WRITE] - 
        history->stats[oldpos].acc_size[SCHED_LOG_WRITE];
        */
        (history->stats[pos].acc_size[SCHED_LOG_READ] - 
        history->stats[oldpos].acc_size[SCHED_LOG_READ]) +
        (double) (history->stats[pos].acc_size[SCHED_LOG_WRITE] - 
        history->stats[oldpos].acc_size[SCHED_LOG_WRITE]);
}

inline double get_size(const handle_history_t * hist)
{
    return get_size_diff( hist->current_stat_pos, 
        get_old_pos(hist->current_stat_pos), hist);
}

int get_concrete_attributes_for_datafile(dataserver_details * server, 
    PVFS_fs_id fsid, PVFS_handle handle, PVFS_credentials * credentials)
{
    PVFS_object_ref ref = {handle, fsid};
    int ret;
    /*
     * get concrete file size to approximate migration time. 
     */
    memset(& server->getattr_stats, 0, sizeof(PVFS_sysresp_getattr));
    ret = PVFS_sys_getattr(ref, 
            PVFS_ATTR_SYS_PARENT_HANDLE | PVFS_ATTR_SYS_SIZE,
            credentials, & server->getattr_stats);
    return ret;
}

int handle_history_cmp ( const handle_history_t ** stat_1, 
    const handle_history_t ** stat_2)
{
    int oldpos_1 = get_old_pos((*stat_1)->current_stat_pos);
    int oldpos_2 = get_old_pos((*stat_2)->current_stat_pos);
    double size_1;
    double size_2;
    /*
     * first argument < than second one => return -1
     */
    size_1 = get_size_diff( (*stat_1)->current_stat_pos, oldpos_1, *stat_1 )
        / ((*stat_1)->age_stats[((*stat_1)->current_stat_pos)] - 
            (*stat_1)->age_stats[oldpos_1]);
    size_2 = get_size_diff( (*stat_2)->current_stat_pos, oldpos_2, *stat_2 )
        / ((*stat_2)->age_stats[((*stat_2)->current_stat_pos)] - 
            (*stat_2)->age_stats[oldpos_2]);    
    if( size_1 > size_2)
    {
        return -1;
    }
    else
    {
        return +1;
    }
}

void put_sched_resp_into_history_tree_and_sort(
    PVFS_sysresp_mgmt_get_scheduler_stats * stats,
    red_black_tree * tree,
    int current_iteration,
    int * inout_max_interesting_cnt, 
    handle_history_t ** out_array
    )
{
    int i;
    tree_node * node;
    PVFS_handle_request_statistics * stat;
    handle_history_t * h_history;
    
    int max_out = * inout_max_interesting_cnt;
    int cur_out = 0;
    
    for(i=0; i < stats->handle_stats.count; i++)
    {
        stat = & stats->handle_stats.stats[i];
        
        node = lookupTree(& stat->handle, tree);
        if ( node == NULL )
        {
            h_history = (handle_history_t *) malloc(sizeof(handle_history_t));
            memset(h_history, 0, sizeof(handle_history_t));
            
            if( h_history == NULL) 
            {
                FATAL_ERR("Could not malloc !\n")
            }
            
            h_history->age_iteration = current_iteration;
            h_history->handle = stat->handle;
            h_history->current_stat_pos = 0;
                        
            insertKeyIntoTree(h_history, tree);
        }
        else
        {
            h_history = (handle_history_t *) node->data;            
            h_history->age_iteration = current_iteration;
            h_history->current_stat_pos = 
                (h_history->current_stat_pos + 1) % HISTORY_SIZE; 
        }
        h_history->age_stats[h_history->current_stat_pos] = current_iteration;
        memcpy(& h_history->stats[h_history->current_stat_pos], & stat->stat, 
            sizeof(PVFS_request_statistics));
        
        if ( cur_out < max_out && current_iteration != 1 )
        /*
         * avoid to migrate stuff in the first iteration !
         */ 
        {
            /*
             * only think about files which access statistics have changed.
             */
            int oldpos = get_old_pos(h_history->current_stat_pos);
            if ( h_history->stats[h_history->current_stat_pos].io_number[SCHED_LOG_READ] != 
                 h_history->stats[oldpos].io_number[SCHED_LOG_READ] ||
                 h_history->stats[h_history->current_stat_pos].io_number[SCHED_LOG_WRITE] != 
                 h_history->stats[oldpos].io_number[SCHED_LOG_WRITE] )
            {
                out_array[cur_out] = h_history;
                cur_out++;
            }
        }
    }
    
    *inout_max_interesting_cnt = cur_out;
    
    qsort(out_array, cur_out, sizeof(handle_history_t*), 
         (int(*)(const void *, const void *)) handle_history_cmp);
}


static int migration_setup_signal_handlers(void)
{
    struct sigaction new_action;
    struct sigaction ign_action;

    /* Set up the structure to specify the new action. */
    new_action.sa_handler = migration_sig_handler;
    sigemptyset (&new_action.sa_mask);
    new_action.sa_flags = 0;

    ign_action.sa_handler = SIG_IGN;
    sigemptyset (&ign_action.sa_mask);
    ign_action.sa_flags = 0;

    /* catch these */
    sigaction (SIGILL, &new_action, NULL);
    sigaction (SIGTERM, &new_action, NULL);
    sigaction (SIGHUP, &new_action, NULL);
    sigaction (SIGINT, &new_action, NULL);
    sigaction (SIGQUIT, &new_action, NULL);
    sigaction (SIGSEGV, &new_action, NULL);

    /* ignore these */
    sigaction (SIGPIPE, &ign_action, NULL);
    sigaction (SIGUSR1, &ign_action, NULL);
    sigaction (SIGUSR2, &ign_action, NULL);

    return 0;
}

/*
 * Signal handler allows safe shutdown...
 */
static void migration_sig_handler(int sig)
{
    printf("Caught signal %d, shutdown automigration tool!\n", sig);
    shutdown = 1;
}

/*
 * This function drives the incomplete pvfs2 operations !
 */
void * background_migration_thread_func( void *ptr )
{
    int queue_empty = 0;
    int inout_count;
    
    simplequeue_elem * q_elem;
    
    int ret;
    int i;
    
    /* depending on currently processed objects allocate arrays*/
    PVFS_sys_op_id *op_id_array;
    /* user_ptr points to dataserevr_struct */
    void **user_ptr_array;
    int *error_code_array;
    
    while(! shutdown || ! queue_empty)
    {
        pthread_mutex_lock(& migrating_dataservers_mutex);
        queue_empty =  simple_queue_is_empty(& migrating_dataservers);
        if( queue_empty )
        {
            pthread_cond_wait( & migration_in_progress_cond, 
                & migrating_dataservers_mutex );
            queue_empty =  simple_queue_is_empty(& migrating_dataservers);
            continue;
        }
        
        assert(migrating_dataservers_cnt);
        
        /* allocate memory for enqueued migrations elements */
        op_id_array = (PVFS_sys_op_id*) malloc(sizeof(PVFS_sys_op_id) 
            * migrating_dataservers_cnt);
        user_ptr_array = malloc(sizeof(void*) 
            * migrating_dataservers_cnt);
        error_code_array = (int*) malloc(sizeof(int) 
            * migrating_dataservers_cnt);
        
        /* put information into arrays */
        inout_count = 0;
        
        ITERATE_SIMPLEQUE( migrating_dataservers, q_elem,
            op_id_array[inout_count] = ((dataserver_details *) q_elem->data)->migration_id;
            inout_count++;
         )
         
        assert(migrating_dataservers_cnt == inout_count);
        
        pthread_mutex_unlock(& migrating_dataservers_mutex);
    
        ret = PVFS_sys_testsome(op_id_array, & inout_count, user_ptr_array, 
            error_code_array, 500);
        if (ret < 0 )
        {
            /* this should never happen */
            PVFS_perror("PVFS_sys_testsome error \n", ret);
            exit(1);
        }
        
        if ( inout_count == 0 )
        {
            free(op_id_array);
            free(user_ptr_array);
            free(error_code_array);
            continue;
        }
        
        /* some guys finished up !*/
        pthread_mutex_lock(& migrating_dataservers_mutex);
        for(i=0; i < inout_count; i++)
        {
            dataserver_details * finished_server;
            finished_server= (dataserver_details *) user_ptr_array[i];
            
            pthread_mutex_lock(& finished_server->state_mutex);
            
            if( error_code_array[i] == 0 )
            {
                finished_server->state = STATE_MIGRATION_RECENTLY_DONE;
                debug( printf("Migration finished sucessful for server %s\n",
                    finished_server->alias); )
            }else{
                fprintf(stderr,  "Error migration finished with error for server %s\n",
                    finished_server->alias);
                PVFS_perror("Migration errorcode\n", error_code_array[i]);  
                finished_server->state = STATE_READY;
            }
            /* remove from migration queue */
            migrating_dataservers_cnt--;
            delete_element_from_simple(
                  finished_server->migration_queue_link, 
                  & migrating_dataservers );            
            
            pthread_mutex_unlock(& finished_server->state_mutex);
        }
        pthread_mutex_unlock(& migrating_dataservers_mutex);
                
        free(op_id_array);
        free(user_ptr_array);
        free(error_code_array);
    }
    return NULL;
}


int main(
    int argc,
    char **argv)
{
    int64_t ret;
    int i;
    PVFS_credentials credentials;
    int iteration = 0;
    
    PVFS_fs_id fsid = 0;
    int metaserver_cnt = 0;
    PVFS_BMI_addr_t * metaserver_array = NULL;
    PVFS_BMI_addr_t * dataserver_array = NULL;
    char ** metaserver_aliases = NULL;
    PVFS_handle_extent_array * metaserver_extend_array = NULL;
        
    struct server_configuration_s *config;
    struct PVFS_mgmt_server_stat *stat_array = NULL;
    
    migration_setup_signal_handlers();
    
    init_simple_queue( & migrating_dataservers);
    
/*
    PVFS_hint * hints = NULL;    
    
    PVFS_add_hint(& hints, REQUEST_ID, "pvfs2-migrate");
    PINT_hint_add_environment_hints(& hints);
*/    
    user_opts = parse_args(argc, argv);
    if (!user_opts)
    {
        fprintf(stderr, "Error, failed to parse command line arguments\n");
        return (1);
    }

    ret = PVFS_util_init_defaults();
    if (ret < 0)
    {
        PVFS_perror("PVFS_util_init_defaults", ret);
        return (-1);
    }

    PVFS_util_gen_credentials(&credentials);

    /*
     * Step1: lookup filesystem 
     */
    {
        char out[PVFS_NAME_MAX];   
        ret = PVFS_util_resolve(user_opts->filesystem, &(fsid), 
            out, PVFS_NAME_MAX);
        if ( strlen(out) > 0 && ! (strlen(out) == 1 && out[0] == '/' ) )
        {
             fprintf(stderr, "Error, please specify just a filesystem and no "
                "residing file: %s\n",out);
             exit(1);
        }
    }
    
    if (ret < 0)
    {
        PVFS_perror("PVFS_util_resolve error\n", ret);
        exit(1);
    }
    
    if( user_opts->verbose )
    {
        printf("Starting automatic rebalancing tool for filesystem:"
            " %s (FS_ID:%d)\n", user_opts->filesystem, (int) fsid);
    }
   
    ret = PVFS_mgmt_count_servers(fsid, & credentials, 
        PVFS_MGMT_META_SERVER, & metaserver_cnt);
    if (ret < 0)
    {
        PVFS_perror("PVFS_mgmt_count_servers error\n", ret);
        exit(1);
    }
    
    ret = PVFS_mgmt_count_servers(fsid, & credentials, 
        PVFS_MGMT_IO_SERVER, & dataserver_cnt);
    if (ret < 0)
    {
        PVFS_perror("PVFS_mgmt_count_servers error\n", ret);
        exit(1);
    }

    metaserver_array = (PVFS_BMI_addr_t *) malloc(
         metaserver_cnt * sizeof(PVFS_BMI_addr_t));
    metaserver_aliases = (char ** ) malloc(sizeof(char*) 
        * metaserver_cnt);
        
    metaserver_extend_array = (PVFS_handle_extent_array *) malloc(
        sizeof(PVFS_handle_extent_array) * metaserver_cnt);

    servers = (dataserver_details*) malloc( sizeof(dataserver_details) 
        * dataserver_cnt);
    dataserver_array = (PVFS_BMI_addr_t *) malloc(
         dataserver_cnt * sizeof(PVFS_BMI_addr_t));

    stat_array = (struct PVFS_mgmt_server_stat *) malloc(
        sizeof(struct PVFS_mgmt_server_stat) * dataserver_cnt );
    if ( metaserver_array == NULL || dataserver_array == NULL || 
        metaserver_aliases == NULL || stat_array == NULL ||
        metaserver_extend_array == NULL || servers == NULL )
    {
        fprintf(stderr,"Could not malloc arrays for addresses\n");
        exit(1);
    }
    
    memset(servers,0, sizeof(dataserver_details) * dataserver_cnt);
      
    ret = PVFS_mgmt_get_server_array(fsid, & credentials, 
        PVFS_MGMT_IO_SERVER, dataserver_array, & dataserver_cnt);
    if (ret < 0)
    {
        PVFS_perror("PVFS_mgmt_get_server_array getting IO servers error\n", ret);
        exit(1);
    }
    
    for(i=0; i < dataserver_cnt; i++)
    {
        servers[i].bmi_address = dataserver_array[i];
        servers[i].getschedstats_resp.handle_stats.stats = (PVFS_handle_request_statistics*) 
            malloc(sizeof(PVFS_handle_request_statistics) * MAX_LOGGED_HANDLES_PER_FS );
        servers[i].getschedstats_resp.handle_stats.stats = (PVFS_handle_request_statistics*) 
            malloc(sizeof(PVFS_handle_request_statistics) * MAX_LOGGED_HANDLES_PER_FS );
        servers[i].stats = & stat_array[i];
        initRedBlackTree( & servers[i].handle_history, (int(*)(void *,void*)) compare_lookup, 
            (int(*)(void *,void*))compare_internal );
        pthread_mutex_init(& servers[i].state_mutex, NULL );
        servers[i].state = STATE_READY;
    }

   ret = PVFS_mgmt_get_server_array(fsid, & credentials, 
        PVFS_MGMT_META_SERVER, metaserver_array , & metaserver_cnt);
    if (ret < 0)
    {
        PVFS_perror("PVFS_mgmt_get_server_array getting meta servers error\n", ret);
        exit(1);
    }
   
    config = PINT_get_server_config_struct(fsid);
    PINT_put_server_config_struct(config); 
   
    for( i=0; i < metaserver_cnt; i++ )
    {
        /*
        metaserver_aliases[i] = 
            PINT_config_get_host_alias_ptr(config, 
                (char *) BMI_addr_rev_lookup(metaserver_array[i]));
        */
        ret = PINT_cached_config_get_one_server_alias(
            BMI_addr_rev_lookup( metaserver_array[i] ), 
            config,
            fsid,
            & metaserver_extend_array[i],
            & metaserver_aliases[i],
            0);
        if( ret != 0 )
        {
            PVFS_perror("PINT_cached_config_get_one_server_str\n", ret);
            exit(1);
        }
    }
    
    for( i=0; i < dataserver_cnt; i++ )
    {
        ret = PINT_cached_config_get_one_server_alias(
            BMI_addr_rev_lookup( servers[i].bmi_address ), 
            config,
            fsid,
            & servers[i].extend,
            & servers[i].alias,
            1);
        if( ret != 0 )
        {
            PVFS_perror("PINT_cached_config_get_one_server_str\n", ret);
            exit(1);
        }
    }    
    
        
    if( user_opts->verbose )
    {   
        print_servers("Metaservers", metaserver_cnt, metaserver_array, 
            metaserver_aliases, metaserver_extend_array); 
        print_data_servers("Dataservers", dataserver_cnt, servers);
    }
    
    if ( dataserver_cnt < 2 )
    {
        printf("Warning, at least 2 dataservers have to be available to do"
        " useful migration!\n Abort..\n");
        if ( ! user_opts->override )
        {
            exit(0);
        }
    }
    
    ret = pthread_create( & migration_thread, NULL, 
        background_migration_thread_func, NULL );
    if( ret != 0 ){
        printf("Could not creat background migration thread ! Abort!\n");
        exit(1);
    }
    
    /* now call mgmt functions to determine per-server statistics */

    if( user_opts->simulate_server_no )
    {
        debug( printf("WARNING: Simulation activated !\n"); )
    }

    while(! shutdown)
    {
    int interesting_server_cnt;
    handle_history_t * sorted_history[MAX_LOGGED_HANDLES_PER_FS];
    int sorted_history_size ;
    int interesting_servers_migration_started = 0; 
    int j;

    iteration++;
    if( iteration < 0) {
        printf("Error, wrapping iteration number, fixme !!!\n");
        /* currently wrapping between int max is not handled properly 
         * throughout the code !*/
        iteration = 1;
    }
    
    if( user_opts->verbose )
    {
        printf("\nIteration %d\n",iteration);
    }
    
    /* gather intelligence */
    ret = PVFS_mgmt_statfs_list(
        fsid, &credentials, stat_array, dataserver_array,
        dataserver_cnt, NULL);
    if( ret != 0 )
    {
        /* todo proper error handling, e.g. sleep for a while and retry etc... */
        PVFS_perror("PVFS_mgmt_statfs_list error\n", ret);
        return (ret);
    }
    
    /* make decisions depending on intelligence */
    /* long term scheduling and short term scheduling 
     * long term scheduling policy: 
     *      if the server is under high load now, then get detailed statistics,
     *      if the sample statistics show that some files are hit repetative, migrate a
     *      couple of these... 
     * 
     */
    /* step 1: server with load > 1 are potential candidates, however sort the list */
    
    qsort(servers, dataserver_cnt, sizeof(dataserver_details), 
         (int(*)(const void *, const void *)) server_stat_cmp);
      
    /* for debugging purposes simulate */
    if( user_opts->simulate_server_no )
    {
        if ( user_opts->simulate_server_no > dataserver_cnt )
        {
            fprintf(stderr, "For simulation are only %d dataservers available, "
                "however %d are selected\n Abort!!!\n", 
                dataserver_cnt, user_opts->simulate_server_no);
            exit(1);
        }
        
        for(i=0; i < user_opts->simulate_server_no; i++)
        {
            stat_array[i].load_1 = (long)(60000 * user_opts->simulate_load);
        }
    }
      
    /* figure out load gradient between servers */

       
    /* figure out how much servers possibly migrate, right now, move from 
     * server with the highest load to the server with the smallest load */
    for (i = 0; i < dataserver_cnt /2 ; i++)
    {
        double tmp_double;
        double quotient;
        tmp_double = ((double) stat_array[i].load_1 - 
        (double) stat_array[dataserver_cnt-1-i].load_1 ) ;
        quotient = (double) (stat_array[i].load_1) /
            (double) stat_array[dataserver_cnt-i-1].load_1;

        debug( printf("%i: Load: highest: %f lowest: %f diff:%f quot:%f\n",
            i,
            print_load(stat_array[i].load_1), 
            print_load(stat_array[dataserver_cnt-1-i].load_1), 
            print_load(tmp_double),
            quotient); )
        if ( print_load(tmp_double) < 1.0  || quotient < 
                1.0 + user_opts->load_tolerance || 
            stat_array[i].load_1 == 0 )
        {
            break;
        }
    }
    interesting_server_cnt = i;
    if ( interesting_server_cnt == 0 )
    {
        debug( printf("Load gradient not sufficient, do nothing\n"); )
        /* wait for next iteration if nothing to do */ 
        CONTINUE;
    }
    
    debug( printf("Interesting servers: %d\n", interesting_server_cnt); )
    
    for(i = 0 ; i < interesting_server_cnt ; i ++ )
    {
        debug( printf("%s\n", servers[i].alias); )         
        /* if the server is currently migrating, ignore the server */
        pthread_mutex_lock(& servers[i].state_mutex);
        switch ( servers[i].state )
        {
           case STATE_MIGRATION_IN_PROGRESS:
                pthread_mutex_unlock(& servers[i].state_mutex);
                debug( printf("Server is currently migrating load %f\n", 
                    print_load(servers[i].stats->load_1)  ); )
                continue;
           case STATE_MIGRATION_RECENTLY_DONE:
                 servers[i].state = STATE_READY;
                 pthread_mutex_unlock(& servers[i].state_mutex);
                 debug( printf("Server recently migrates load %f\n", 
                    print_load(servers[i].stats->load_1) ); )
                 continue;
            case STATE_READY:
                break;
        }
        pthread_mutex_unlock(& servers[i].state_mutex);
        
        /* figure out which files have a high access statistic for interesting servers */

        ret = PVFS_mgmt_get_scheduler_stats(
            fsid, &credentials,
            servers[i].bmi_address,
            MAX_LOGGED_HANDLES_PER_FS,
            & servers[i].getschedstats_resp);
        debug(
            printf("Server ready to migrate: load: %f total MByte read:%f -write:%f\n", 
            print_load(servers[i].stats->load_1) ,
            (double)(servers[i].getschedstats_resp.fs_stats.acc_size[SCHED_LOG_READ])/1024.0/1024.0,
            (double) (servers[i].getschedstats_resp.fs_stats.acc_size[SCHED_LOG_WRITE]) / 1024.0/1024.0);
        )
        
        sorted_history_size =  MAX_LOGGED_HANDLES_PER_FS;
        
        put_sched_resp_into_history_tree_and_sort(& servers[i].getschedstats_resp,
            & servers[i].handle_history, iteration, & sorted_history_size , sorted_history);
        
        
        /* now start with the files which have the biggest change since the last update 
         * actually the history might be used to reduce the sampling effects or to adapt 
         * long term scheduling... 
         */
        for (j=0; j < sorted_history_size; j++)
        {
                int oldpos = get_old_pos(sorted_history[j]->current_stat_pos);
                double size;
                size = get_size_diff( sorted_history[j]->current_stat_pos, 
                    oldpos, sorted_history[j] );
                    
                debugV( printf("History element: %lld - accessed: %f MByte\n", 
                    sorted_history[j]->handle , size / 1024.0 / 1024.0 ); )
                
                if ( size < user_opts->min_accessed_data )
                {
                    /* not longer interesting access size */
                    break;
                }
        }
        
        if( j == 1 || sorted_history_size == 1)
        {
            debug( printf("Only one file is accessed during iteration, no migration possible!\n"); )        
        }
        else if ( j == 0 ) 
        { 
            debug( printf("Minimum accessed data for all handles not achieved!\n"); )
        } 
        else
        {
            PVFS_handle best_match_handle;
            double      best_match_access_difference;
            /* now find best possible migration candidate 
             * use total calculate accessed size as a limit, file which accesses
             * ~ 50% of the total calculated accessed size wins right now. 
             */
            double fs_current_accessed_size = (double) servers[i].getschedstats_resp.fs_stats.acc_size[SCHED_LOG_READ] 
               + (double) servers[i].getschedstats_resp.fs_stats.acc_size[SCHED_LOG_WRITE];
            double look_for_accessed_size;
               
            if ( fs_current_accessed_size <  servers[i].fs_old_accessed_size )
            { /* overflow */
                fs_current_accessed_size = (double)(((uint64_t) -1) - 
                    servers[i].fs_old_accessed_size) + (double) fs_current_accessed_size;  
            }
            else
            {
                fs_current_accessed_size = fs_current_accessed_size - servers[i].fs_old_accessed_size;
            }
            debug( printf("server total accessed size MB:%f \n", 
                fs_current_accessed_size/1024.0/1024.0); )
            
            /* now find the guy which is closest to 50% accessed size */
            look_for_accessed_size = fs_current_accessed_size/2.0;
            best_match_handle = sorted_history[0]->handle;
            best_match_access_difference = abs(look_for_accessed_size - 
                get_size(sorted_history[0])); 
            for (j=1; j < sorted_history_size; j++)
            {
                double current_size_diff;
                current_size_diff =  abs(look_for_accessed_size - 
                    get_size(sorted_history[j])); 
                if ( current_size_diff < best_match_access_difference )
                {
                    best_match_access_difference = current_size_diff;
                    best_match_handle = sorted_history[j]->handle;
                }
            }
            
            debug( printf("best migration candidate handle:%lld access diff to optimum:%f \n",
                lld(best_match_handle) , 
                best_match_access_difference/1024.0/1024.0); )                
            
            ret = get_concrete_attributes_for_datafile(& servers[i], fsid, 
                best_match_handle, & credentials);
            if ( ret != 0 )
            {
                debug ( PVFS_perror("migration not possible, dfile attribute fetching returned an error\n",
                    ret); ) 
            }
            else
            {   
                debug ( printf("File size: %lld parent object: %lld \n", 
                    servers[i].getattr_stats.attr.size, 
                    lld(servers[i].getattr_stats.attr.parent_handle)); )
                interesting_servers_migration_started++;
            }
        }

        /*
         * Free data from old iterations !
         */ 
        if( servers[i].attr_age_iteration + user_opts->remove_old_entries_after_iterations
            < iteration )
        {
            debug ( printf("Try to free history\n"); ) 
            try_to_free_history_tree(& servers[i].handle_history,
                iteration - user_opts->remove_old_entries_after_iterations);
                
            servers[i].attr_age_iteration = iteration;                
        }
        
        servers[i].fs_old_accessed_size = servers[i].getschedstats_resp.fs_stats.acc_size[SCHED_LOG_READ] 
               + (double) servers[i].getschedstats_resp.fs_stats.acc_size[SCHED_LOG_WRITE];
    }
    
    /* time if a possible merge pos is found */
    sleep(user_opts->refresh_time);
    } /* end_while */
    
    /* make sure that background migration thread finishes up */
    pthread_mutex_lock(& migrating_dataservers_mutex);
    pthread_cond_signal( & migration_in_progress_cond );
    if ( ! simple_queue_is_empty(& migrating_dataservers) )
    {
        printf("Please wait, finish migrations in the background!\n");
    }
    pthread_mutex_unlock(& migrating_dataservers_mutex);
    
    pthread_join(migration_thread, NULL);
    /* background thread has now finished */
    
    PVFS_sys_finalize();
    free(user_opts);
    return (ret);
}

/* parse_args()
 *
 * parses command line arguments
 *
 * returns pointer to options structure on success, NULL on failure
 */
static struct options *parse_args(
    int argc,
    char *argv[])
{
    int one_opt = 0;
    int tmp;

    struct options *tmp_opts = NULL;

    /* create storage for the command line options */
    tmp_opts = (struct options *) malloc(sizeof(struct options));
    if (!tmp_opts)
    {
        return (NULL);
    }
    memcpy(tmp_opts, & default_options, sizeof(struct options));
    

    /* look at command line arguments */
    while ((one_opt = getopt(argc, argv, "r:t:s:d:m:voV")) != EOF)
    {
        switch (one_opt)
        {
        case ('m'):
            tmp_opts->min_accessed_data = atoi(optarg) * 1024;
            break;
        case ('d'):
            tmp_opts->remove_old_entries_after_iterations = atoi(optarg);
            break;
        case ('s'):
            tmp = sscanf(optarg, "%f:%d", & tmp_opts->simulate_load, 
                & tmp_opts->simulate_server_no);
            if( tmp != 2)
            {
                fprintf(stderr, "Parameter s with unsuplied options %s\n", optarg);
                usage(argc,argv);
                exit(1);
            }
            break;
        case ('t'):
            tmp_opts->load_tolerance = (float) atof(optarg);
            break;
        case ('o'):
            tmp_opts->override = 1;
            break;
        case ('r'):
            tmp_opts->refresh_time = atoi(optarg);
            break;
        case ('v'):
            tmp_opts->verbose++;
            break;
        case ('V'):
            printf("%s\n", PVFS2_VERSION);
            exit(0);
        case ('h'):
        case ('?'):
            usage(argc, argv);
            exit(EXIT_FAILURE);
        }
    }
    
    if (optind != (argc - 1))
    {
        usage(argc, argv);
        exit(EXIT_FAILURE);
    }
    tmp_opts->filesystem = argv[argc - 1];
    
    return (tmp_opts);
}

static void usage(
    int argc,
    char **argv)
{
    fprintf(stderr,
            "Usage: %s ARGS <mount-point-to-observe>\n",
            argv[0]);
    fprintf(stderr,
            "default mount point: %s \n"
            "Where ARGS is one or more of\n"
            "\t-o\t override (default: %d)\n"
            "\t-r\t <rate> refresh rate, time between iterations (default: %d)\n"
            "\t-d\t <iterations> remove old entries after iterations \n"
            "\t-t\t <tolerance> load tolerance in percent (default: %.3f)\n"
            "\t-m\t <size> min accessed size in Kbyte (default: %.1f)\n"
            "\t-v\t verbose output (default: %d), multiple v increase verbosity\n"
            "\t-V\t print version number and exit\n"
            "\t-s\t <load>:<serverNo> simulate load (default:%f) \n" 
            "\t\t\t on a number of servers(default:%d)\n"
            "Explaination: \n"
            "\t Load tolerance means the percentage the load of a high loaded server might\n"
            "\t be higher than a low loaded server\n",
            default_options.filesystem,
            default_options.override,
            default_options.refresh_time,
            default_options.load_tolerance,
            (double) default_options.min_accessed_data/1024.0,
            default_options.verbose,
            default_options.simulate_load,
            default_options.simulate_server_no
            );
    return;
}


int lookup(
    char *pvfs2_file,
    PVFS_credentials * credentials,
    PVFS_fs_id * out_fs_id,
    PVFS_object_ref * out_object_ref,
    PVFS_sysresp_getattr * out_resp_getattr)
{
    char *entry_name;           /* relativ name of the pvfs2 file */
    char str_buf[PVFS_NAME_MAX];        /* basename of pvfs2 file */
    char file[PVFS_NAME_MAX];
    PVFS_sysresp_lookup resp_lookup;
    PVFS_object_ref parent_ref;
    int fsid;
    int64_t ret;

    strncpy(file, pvfs2_file, PVFS_NAME_MAX);

    /*
     * Step1: lookup filesystem 
     */
    ret = PVFS_util_resolve(file, &(fsid), file, PVFS_NAME_MAX);
    if (ret < 0)
    {
        fprintf(stderr, "PVFS_util_resolve error\n");
        return -1;
    }
    *out_fs_id = fsid;

    /*
     * ripped from pvfs2-cp.c lookup filename
     */
    entry_name = str_buf;
    if (strcmp(file, "/") == 0)
    {
        /* special case: PVFS2 root file system, so stuff the end of
         * srcfile onto pvfs2_path */
        char *segp = NULL, *prev_segp = NULL;
        void *segstate = NULL;

        /* can only perform this special case if we know srcname */
        if (file == NULL)
        {
            fprintf(stderr, "unable to guess filename in " "toplevel PVFS2\n");
            return -1;
        }

        memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
        ret = PVFS_sys_lookup(fsid, file,
                              credentials, &resp_lookup,
                              PVFS2_LOOKUP_LINK_FOLLOW);
        if (ret < 0)
        {
            PVFS_perror("PVFS_sys_lookup", ret);
            return (-1);
        }

        while (!PINT_string_next_segment(file, &segp, &segstate))
        {
            prev_segp = segp;
        }
        entry_name = prev_segp; /* see... points to basename of srcname */
    }
    else        /* given either a pvfs2 directory or a pvfs2 file */
    {
        /* get the absolute path on the pvfs2 file system */

        /*parent_ref.fs_id = obj->pvfs2.fs_id; */

        if (PINT_remove_base_dir(file, str_buf, PVFS_NAME_MAX))
        {
            if (file[0] != '/')
            {
                fprintf(stderr, "Error: poorly formatted path.\n");
            }
            fprintf(stderr, "Error: cannot retrieve entry name for "
                    "creation on %s\n", file);
            return (-1);
        }
        ret = PINT_lookup_parent(file, fsid, credentials, &parent_ref.handle);
        if (ret < 0)
        {
            PVFS_perror("PVFS_util_lookup_parent", ret);
            return (-1);
        }
        else    /* parent lookup succeeded. if the pvfs2 path is just a
                   directory, use basename of src for the new file */
        {
            int len = strlen(file);
            if (file[len - 1] == '/')
            {
                char *segp = NULL, *prev_segp = NULL;
                void *segstate = NULL;

                if (file == NULL)
                {
                    fprintf(stderr, "unable to guess filename\n");
                    return (-1);
                }
                while (!PINT_string_next_segment(file, &segp, &segstate))
                {
                    prev_segp = segp;
                }
                strncat(file, prev_segp, PVFS_NAME_MAX);
                entry_name = prev_segp;
            }
            parent_ref.fs_id = fsid;
        }
    }

    memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
    ret = PVFS_sys_ref_lookup(parent_ref.fs_id, entry_name,
                              parent_ref, credentials, &resp_lookup,
                              PVFS2_LOOKUP_LINK_FOLLOW);
    if (ret)
    {
        fprintf(stderr, "Failed to lookup object: %s\n", entry_name);
        return -1;
    }

    *out_object_ref = resp_lookup.ref;

    memset(out_resp_getattr, 0, sizeof(PVFS_sysresp_getattr));
    ret = PVFS_sys_getattr(*out_object_ref, PVFS_ATTR_SYS_ALL_NOHINT,
                           credentials, out_resp_getattr);
    if (ret)
    {
        fprintf(stderr, "Failed to do pvfs2 getattr on %s\n", entry_name);
        return -1;
    }

    if (out_resp_getattr->attr.objtype != PVFS_TYPE_METAFILE)
    {
        fprintf(stderr, "Object is no file ! %s\n", entry_name);
        return -1;
    }

    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
