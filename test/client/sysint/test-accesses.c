
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <time.h>
#include "pvfs2.h"
#include "pvfs2-sysint.h"

#if PVFS2_SIZEOF_VOIDP == 32 
#  define llu(x) (x)
#  define lld(x) (x)
#  define SCANF_lld "%lld"
#elif PVFS2_SIZEOF_VOIDP == 64
#  define llu(x) (unsigned long long)(x)
#  define lld(x) (long long)(x)
#  define SCANF_lld "%ld"
#else
#  error Unexpected sizeof(void*)
#endif

int main(int argc, char * argv[])
{
    FILE * f;
    int ret;
    PVFS_fs_id curfs;
    PVFS_Request file_req;
    PVFS_Request mem_req;
    int count;
    char line[255];
    int size;
    PVFS_offset offset=0;
    PVFS_credentials creds;
    PVFS_sysresp_create create_resp;
    PVFS_sysresp_io io_resp;
    PVFS_sysresp_lookup lookup_resp;
    PVFS_sys_attr attr;
    const char * filename = "test-accesses-file";
    int j = 0, i = 0;
    char * membuff;
    char errormsg[255];

    if(argc < 2)
    {
	fprintf(stderr, "test-accesses <sizes file>\n");
	exit(1);
    }
    
    f = fopen(argv[1], "r");
    if(!f)
    {
	fprintf(stderr, "error opening file\n");
	return errno;
    }
    
    if(fgets(line, 255, f) == NULL)
    {
	fprintf(stderr, "error in file\n");
	exit(1);
    } 

    if(sscanf(line, "%d", &count) < 1)
    {
	fprintf(stderr, "error in file\n");
	exit(1);
    }
    
    ret = PVFS_util_init_defaults();
    if(ret < 0) goto error;

    ret = PVFS_util_get_default_fsid(&curfs);
    if(ret < 0) goto error;

    ret = PVFS_sys_lookup(curfs, "/", &creds, &lookup_resp, 0, NULL);
    if(ret < 0) goto error;

    PVFS_util_gen_credentials(&creds);

    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
    attr.owner = creds.uid;
    attr.group = creds.gid;
    attr.perms = 0644;
    attr.atime = attr.ctime = attr.mtime = time(NULL);

    ret = PVFS_sys_create(
	(char*)filename, 
	lookup_resp.ref, attr, &creds, NULL, &create_resp, NULL, NULL);
    if(ret < 0) goto error;

    for(; i < count; ++i)
    {
	if(fgets(line, 255, f) == NULL)
	{
	    fprintf(stderr, "error in file\n");
	    exit(1);
	} 

	if(sscanf(line, "%d", &size) < 1)
	{
	    fprintf(stderr, "error in file\n");
	    exit(1);
	}

	membuff = malloc(size);
	assert(membuff);

	for(j = 0; j < size; ++j)
	{
	    membuff[j] = j;
	}

	ret = PVFS_Request_contiguous(
	    size, PVFS_BYTE, &file_req);
	if(ret < 0) goto error;

	ret = PVFS_Request_contiguous(
	    size, PVFS_BYTE, &mem_req);
	if(ret < 0) goto error;

	printf("Performing Write: offset: %llu, size: %d\n",
	       llu(offset), size);

	ret = PVFS_sys_io(
	    create_resp.ref, file_req, offset, membuff, mem_req,
	    &creds, &io_resp, PVFS_IO_WRITE, NULL);
	if(ret < 0) goto error;

	printf("Write response: size: %llu\n", llu(io_resp.total_completed));
	offset += size;

	PVFS_Request_free(&mem_req);
	PVFS_Request_free(&file_req);
	free(membuff);
    }

    return 0;
error:

    fclose(f);

    PVFS_sys_remove(
	(char*)filename,
	lookup_resp.ref,
	&creds,
        NULL);

    PVFS_perror_gossip(errormsg, ret);
    fprintf(stderr, "%s\n", errormsg);
    return PVFS_get_errno_mapping(ret);
}

