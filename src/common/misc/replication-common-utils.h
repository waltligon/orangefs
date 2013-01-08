
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

#endif /* __REPLICATION_COMMON_UTILS_H */
/*
 * Local variables:
 *   c-indent-level: 4
 *   c-basic-offset: 4
 * End:
 *
 * vim:  ts=8 sts=4 sw=4 expandtab
 */                                                    
