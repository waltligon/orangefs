#ifndef __CACHE_STORAGE_H
#define __CACHE_STORAGE_H

int NCAC_aio_read_ext( 	PVFS_fs_id coll_id, 
						PVFS_handle handle,
                        PVFS_context_id context, 
						struct aiovec *aiovec,
                        int *ioreq );

int NCAC_aio_write( 	PVFS_fs_id coll_id, 
						PVFS_handle handle, 
                        PVFS_context_id context, 
						int cnt,
                    	PVFS_offset *offset, 
						PVFS_size *offsize,
            		char **mem,  
						PVFS_size *memsize, 
						int *ioreq  );


int do_read_for_rmw( PVFS_fs_id coll_id, 
					 PVFS_handle handle, 
					 PVFS_context_id context, 
					 struct extent *extent, 
					 PVFS_offset pos, 
					 char * off, 
					 int size, 
					 int *ioreq);


#endif  /* __CACHE_STORAGE_H */
