
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "pvfs2-request.h"
#include "pvfs2-sysint.h"
#include "pvfs2-util.h"
#include "pvfs2-dist-simple-stripe.h"

char buff[1000];
char * dashes = "--------------------------------------------------------";
char * spaces = "                                                        ";
char * as = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
char * bs = "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";
char * cs = "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC";
char * xs = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";

void reset_buff(void);
void print_buff(int padding, int len);

int do_smallmem_noncontig_read(
    PVFS_object_ref ref, PVFS_credentials * creds,
    int memsize,
    int chunks,
    ...);

int do_noncontig_read(
    PVFS_object_ref ref, PVFS_credentials * creds, 
    int chunks, 
    ...);

int do_contig_read(
    PVFS_object_ref ref, PVFS_credentials * creds, int offset, int length);

int do_write(PVFS_object_ref ref, PVFS_credentials * creds,
	     PVFS_offset offset, PVFS_size size, char * buff);

void reset_buff(void)
{
    memset(buff, 'X', 1000);
}

void print_buff(int padding, int len)
{
    int i = 0;
    
    fprintf(stdout, "RESULT: \t");
    fprintf(stdout, "%.*s", padding, spaces);
    for(; i < 50; ++i)
    {
	fprintf(stdout, "%c", (buff[i] == 0 ? '-' : buff[i]));
    }
    fprintf(stdout, "\t (%d bytes)\n\n", len);
}

int do_smallmem_noncontig_read(
    PVFS_object_ref ref, PVFS_credentials * creds,
    int memsize,
    int chunks,
    ...)
{
    int i;
    int res;
    PVFS_Request memreq, readreq;
    PVFS_sysresp_io io_resp;
    PVFS_size * offsets;
    int32_t * sizes;
    va_list args_list;
    int total = 0;
    
    fprintf(stdout, 
	    "READING:\t");
 
    sizes = malloc(sizeof(int32_t) * chunks);
    offsets = malloc(sizeof(PVFS_size) * chunks);

    va_start(args_list, chunks);

    for(i = 0; i < chunks; ++i)
    {
	offsets[i] = (PVFS_size)va_arg(args_list, int);
	sizes[i] = (int32_t)va_arg(args_list, int);

	total += fprintf(stdout,
		"%.*s<%.*s>",
		(int) (offsets[i] - total), spaces,
		(int) (sizes[i] - 2), spaces);

    }

    va_end(args_list);

    fprintf(stdout, "    (memsize = %d)\n", memsize);

    PVFS_Request_indexed(chunks, sizes, offsets, PVFS_BYTE, &readreq);
    PVFS_Request_contiguous(memsize, PVFS_BYTE, &memreq);

    free(offsets);
    free(sizes);

    reset_buff();

    res = PVFS_sys_read(
	ref, readreq, 0, buff, memreq, creds, &io_resp);
    if(res < 0)
    {
	PVFS_perror("read failed with errcode", res);
	return res;
    }

    print_buff(0, io_resp.total_completed);
    return res;
} 

int do_noncontig_read(
    PVFS_object_ref ref, PVFS_credentials * creds, 
    int chunks, 
    ...)
{
    int i;
    int res;
    PVFS_Request memreq, readreq;
    PVFS_sysresp_io io_resp;
    PVFS_size * offsets;
    int32_t * sizes;
    PVFS_size readreq_size;
    va_list args_list;
    int total = 0;
    
    fprintf(stdout, 
	    "READING:\t");
 
    sizes = malloc(sizeof(int32_t) * chunks);
    offsets = malloc(sizeof(PVFS_size) * chunks);

    va_start(args_list, chunks);

    for(i = 0; i < chunks; ++i)
    {
	offsets[i] = (PVFS_size)va_arg(args_list, int);
	sizes[i] = (int32_t)va_arg(args_list, int);

	total += fprintf(stdout,
		"%.*s<%.*s>",
		(int) (offsets[i] - total), spaces,
		(int) (sizes[i] - 2), spaces);

    }
    
    va_end(args_list);

    fprintf(stdout, "\n");

    PVFS_Request_indexed(chunks, sizes, offsets, PVFS_BYTE, &readreq);
    PVFS_Request_size(readreq, &readreq_size);
    PVFS_Request_contiguous(readreq_size, PVFS_BYTE, &memreq);

    free(offsets);
    free(sizes);

    reset_buff();

    res = PVFS_sys_read(
	ref, readreq, 0, buff, memreq, creds, &io_resp);
    if(res < 0)
    {
	PVFS_perror("read failed with errcode", res);
	return res;
    }

    print_buff(0, io_resp.total_completed);
    return res;
}

int do_contig_read(
    PVFS_object_ref ref, PVFS_credentials * creds, int offset, int length)
{
    int res;
    PVFS_sysresp_io     io_resp;
    PVFS_Request memreq, readreq;

    fprintf(stdout, 
	    "READING:\t"
	    "%.*s<%.*s>\n",
	    offset, spaces,
	    length - 2, spaces);

    res = PVFS_Request_contiguous(length, PVFS_BYTE, &memreq);
    if(res < 0)
    {
	PVFS_perror("request contig for memory failed with errcode", res);
	return res;
    }

    res = PVFS_Request_contiguous(length, PVFS_BYTE, &readreq);
    if(res < 0)
    {
	PVFS_perror("request contig for file failed with errcode", res);
	return res;
    }

    reset_buff();

    res = PVFS_sys_read(
	ref, readreq, offset, buff, memreq, creds, &io_resp);
    if(res < 0)
    {
	PVFS_perror("read failed with errcode", res);
	return res;
    }

    print_buff(offset, io_resp.total_completed);

    /* check size */
    
    return res;
}

int do_write(PVFS_object_ref ref, PVFS_credentials * creds,
	     PVFS_offset offset, PVFS_size size, char * buff)
{
    PVFS_Request filereq;
    PVFS_Request memreq;
    PVFS_sysresp_io     io_resp;
    int res;

    fprintf(stdout,
	    "WRITING:\t"
	    "%.*s%.*s\n\n",
	    (int32_t)offset, spaces,
	    (int32_t)size, buff);

    res = PVFS_Request_contiguous(size, PVFS_BYTE, &filereq);
    if(res < 0)
    {
	PVFS_perror("request contig for memory failed with errcode", res);
	return -1;
    }

    res = PVFS_Request_contiguous(size, PVFS_BYTE, &memreq);
    if(res < 0)
    {
	PVFS_perror("request contig for memory failed with errcode", res);
	return -1;
    }

    res = PVFS_sys_write(
	ref, filereq, 
	offset + 2, buff, memreq, creds, &io_resp);
    if(res < 0)
    {
	PVFS_perror("write failed with errcode", res);
	return -1;
    }

    return 0;
}

#define ZEROFILL_FILENAME "test-zerofill"

int main(int argc, char * argv[])
{
    PVFS_sysresp_create create_resp;
    PVFS_sysresp_lookup lookup_resp;
    PVFS_fs_id curfs;
    PVFS_sys_attr attr;
    PVFS_credentials creds;
    PVFS_sys_dist * dist;
    int strip_size = 9;
    int half_strip = (int) strip_size / 2;
    int before_len, after_len;
    char zerofill_fname[100];
    PVFS_simple_stripe_params params;
    int32_t res;

    res = PVFS_util_init_defaults();
    if(res < 0)
    {
	PVFS_perror("PVFS_util_init_defaults", res);
	return (-1);
    }

    res = PVFS_util_get_default_fsid(&curfs);
    if(res < 0)
    {
	PVFS_perror("PVFS_util_get_default_fsid", res);
	return (-1);
    }
  
    before_len = half_strip - 1;
    after_len = half_strip + (strip_size % 2 == 0 ? 0 : 1) - 2;

    fprintf(stdout, 
	    "unset  = XXXXX\n"
	    "zeroed = -----\n\n"
	    "DISTRIBUTION (strip size == %d):\n"
	    "        \t|%.*s0%.*s||%.*s1%.*s||%.*s2%.*s|"
	    "|%.*s0%.*s||%.*s1%.*s||%.*s2%.*s|\n", 
	    strip_size,
	    before_len, dashes, after_len, dashes,
	    before_len, dashes, after_len, dashes,
	    before_len, dashes, after_len, dashes,
	    before_len, dashes, after_len, dashes,
	    before_len, dashes, after_len, dashes,
	    before_len, dashes, after_len, dashes);

    res = PVFS_sys_lookup(curfs, "/", &creds, &lookup_resp, 0);
    if(res < 0)
    {
	PVFS_perror("lookup failed with errcode", res);
    }
    
    dist = PVFS_sys_dist_lookup("simple_stripe");
    params.strip_size = strip_size;

    res = PVFS_sys_dist_setparam(dist, "strip_size", (void *)&params);
    if(res < 0)
    {
	PVFS_perror("dist setparam failed with errcode", res);
    }

    PVFS_util_gen_credentials(&creds);

    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
    attr.owner = creds.uid;
    attr.group = creds.gid;
    attr.perms = 1877;
    attr.atime = attr.ctime = attr.mtime = time(NULL);

    sprintf(zerofill_fname, "%s-%d", ZEROFILL_FILENAME, rand());

    fprintf(stdout, "filename: %s\n", zerofill_fname);

    res = PVFS_sys_create(
	zerofill_fname, lookup_resp.ref, attr, &creds, dist, &create_resp);
    if(res < 0)
    {
	PVFS_perror("create failed with errcode", res);
	return -1;
    }

    half_strip = strip_size / 2;

    do_write(create_resp.ref, &creds, 0, half_strip, as);

    do_write(create_resp.ref, &creds, strip_size * 2, half_strip, bs);

    do_contig_read(
	create_resp.ref, &creds, 0, strip_size + half_strip);

    do_contig_read(
	create_resp.ref, &creds, 
	(strip_size + half_strip), (strip_size + half_strip));

    do_contig_read(
	create_resp.ref, &creds, 0, (strip_size * 3));

    do_contig_read(
	create_resp.ref, &creds, 0, half_strip + 2);

    do_contig_read(
	create_resp.ref, &creds, half_strip + 2, strip_size);

    do_contig_read(
	create_resp.ref, &creds, strip_size + 1, 3);

    do_contig_read(
	create_resp.ref, &creds, 0, 2);

    do_write(create_resp.ref, &creds, strip_size * 4, half_strip, cs);

    do_contig_read(
	create_resp.ref, &creds,
	(strip_size * 3 + half_strip), (strip_size + half_strip));

    do_contig_read(
	create_resp.ref, &creds,
	0, (strip_size * 5));

    do_contig_read(
	create_resp.ref, &creds, 
	(strip_size + half_strip), (strip_size + half_strip));

    do_contig_read(
	create_resp.ref, &creds,
	0, (strip_size * 3));

    do_noncontig_read(
	create_resp.ref, &creds,
	2,
	0, strip_size + half_strip,
	strip_size * 4, half_strip);
    
    do_noncontig_read(
	create_resp.ref, &creds,
	2,
	0, strip_size,
	strip_size * 5, half_strip);

    do_noncontig_read(
	create_resp.ref, &creds,
	2,
	0, strip_size,
	strip_size * 3, half_strip);

    do_noncontig_read(
	create_resp.ref, &creds,
	2,
	strip_size, strip_size,
	strip_size * 4 + 2, strip_size);

    do_noncontig_read(
	create_resp.ref, &creds,
	9,
	2, 2,
	8, 2,
	14, 2,
	20, 2,
	26, 2,
	32, 2,
	38, 2,
	44, 2,
	50, 2);

    do_smallmem_noncontig_read(
	create_resp.ref, &creds,
	strip_size,
	4,
	strip_size, half_strip,
	strip_size * 2, half_strip,
	strip_size * 3, half_strip,
	strip_size * 4, half_strip);

    do_smallmem_noncontig_read(
	create_resp.ref, &creds,
	strip_size,
	2,
	strip_size, 2,
	strip_size * 4, strip_size);
    
    PVFS_sys_remove(
	zerofill_fname,
	lookup_resp.ref,
	&creds);

    res = PVFS_sys_finalize();
    if(res < 0)
    {
	printf("finalizing sysint failed with errcode = %d\n", res);
	return -1;
    }

    return 0;
}

