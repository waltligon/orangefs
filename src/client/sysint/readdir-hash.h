/* 
 * (C) 2016 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#ifndef _READDIR_HASH_H_
#define _READDIR_HASH_H_


int readdir_token_hash_initialize(void);
void readdir_token_hash_finalize(void);

int readdir_token_add(PVFS_object_ref ref,
                      PVFS_ds_position token,
                      int32_t dirdata_index);
int readdir_token_remove(PVFS_object_ref ref,
                         PVFS_ds_position token);
void readdir_token_print_table(void);
int readdir_token_count(void);
int readdir_token_lookup(PVFS_object_ref ref,
                        PVFS_ds_position token);

#endif /* _READDIR_HASH_H_ */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
