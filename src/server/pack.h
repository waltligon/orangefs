

#ifndef PVFS_PACK_UNPACK_STRUCT
#define PVFS_PACK_UNPACK_STRUCT

void *pack_pvfs_struct(void *p,int type,bmi_addr_t q,int z);

void *unpack_pvfs_struct(void *p,int type,int q,int z);

typedef struct package_pvfs
{
	void* (*free)(void *buffer);
	void *buffer;
	void *packed;
} package_pvfs_s;

#endif
