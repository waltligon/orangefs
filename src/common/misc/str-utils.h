/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#ifndef __STR_UTILS_H
#define __STR_UTILS_H

#include "pvfs2-types.h"
#include "pvfs2-storage.h"

int PINT_get_path_element(
    char *pathname,
    int segment_num,
    char *out_segment,
    int out_max_len);
int PINT_string_count_segments(
    char *pathname);
int PINT_get_base_dir(
    char *pathname, 
    char *out_base_dir, 
    int out_max_len);
int PINT_remove_base_dir(
    char *pathname, 
    char *out_dir, 
    int out_max_len);
int PINT_string_next_segment(
    char *pathname,
    char **inout_segp,
    void **opaquep);
int PINT_remove_dir_prefix(
    char *pathname, 
    char* prefix, 
    char *out_path, 
    int out_max_len);
int PINT_parse_handle_ranges(
    char *range, 
    PVFS_handle_extent *out_extent,
    int *status);
#endif
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */


