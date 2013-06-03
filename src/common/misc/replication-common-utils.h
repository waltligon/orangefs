/* 
 *  (C) 2001 Clemson University 
 *  
 *   See COPYING in top-level directory.
 *  
 */

#ifndef __REPLICATION_COMMON_UTILS_H
#define __REPLICATION_COMMON_UTILS_H

#include "pvfs2-types.h"
#include "server-config.h"

/* helper function to calculate the TOTAL size of the layout, so we can allocate
 * all of the space as one contiguous chunk.
 *   
 */
int replication_calculate_layout_size( int32_t *layout_size
                                      ,PVFS_sys_layout *replication_layout );


/* this function ASSUMES that all memory has already been allocated */
int replication_copy_layout( PVFS_sys_layout *src
                            ,void *dest );


const char *get_algorithm_string_value ( uint32_t index );


/* helper function to copy data from one replication structure to another */
int copy_replication_info(replication_s *src_p, replication_s *dest_p);



/* helper function to print the replication structure */
void print_replication_structure(replication_s *replication_p);

/* helper function to print PVFS_sys_layout structure */
void print_sys_layout_structure ( PVFS_sys_layout *layout_p );



/* structures used by replication to determine status between server and client */
enum replication_endpoint_states
{
   RUNNING = 0,
   PENDING,
   FAILED_INITIAL_CONTACT,
   FAILED_BMI_POST_RECV,
   FAILED_BMI_POST_SEND,
   FAILED_TROVE_POST_WRITE,
   FAILED_BMI_RECV,
   FAILED_BMI_SEND,
   FAILED_TROVE_WRITE,
   FLOW_CANCELLED,
   NUMBER_OF_STATES
};

typedef enum replication_endpoint_states replication_endpoint_state;

struct replication_endpoint_status_s
{
   replication_endpoint_state state;
   int error_code;
   PVFS_size bytes_handled;
};

typedef struct replication_endpoint_status_s replication_endpoint_status_t;


/* helper function to print the replication endpoint status information */
void replication_endpoint_status_print(replication_endpoint_status_t *, int);

/* helper funtion to retrieve the string value of a replication state */
const char *get_replication_endpoint_state_as_string(replication_endpoint_state);

#endif /* __REPLICATION_COMMON_UTILS_H */
/*
 * Local variables:
 *   c-indent-level: 4
 *   c-basic-offset: 4
 * End:
 *
 * vim:  ts=8 sts=4 sw=4 expandtab
 */                                                    
