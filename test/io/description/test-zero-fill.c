
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include "pvfs2-request.h"
#include "pvfs2-sysint.h"
#include "pvfs2-util.h"
#include "pvfs2-dist-simple-stripe.h"

char buff[1000];
char check_buff[1000];
char * dashes = "--------------------------------------------------------";
char * spaces = "                                                        ";
char * as = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
char * bs = "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";
char * cs = "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC";
char * xs = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";

int verbose = 0;

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
    PVFS_object_ref ref, 
    PVFS_credentials * creds, 
    PVFS_offset offset, 
    int length,
    PVFS_Request freq);

int do_write(PVFS_object_ref ref, PVFS_credentials * creds,
	     PVFS_offset offset, PVFS_size size, char * buff);

int check_results(
    PVFS_offset offset,
    int total_read,
    PVFS_offset * offsets,
    int32_t * sizes,
    int count);

int check_smallmem_results(
    PVFS_offset offset,
    int total_read,
    int memsize,
    PVFS_offset * offsets,
    int32_t * sizes,
    int count);

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

int check_smallmem_results(
    PVFS_offset offset,
    int total_read,
    int memsize,
    PVFS_offset * offsets,
    int32_t * sizes,
    int count)
{
    int stop_size;
    int total_size = 0;
    int i = 0;
    int res = 0;

    stop_size = (memsize > total_read) ? total_read : memsize;

    for(; i < count; ++i)
    {
	if(total_size + sizes[i] > stop_size)
	{
	    sizes[i] = stop_size - total_size;
	}

	res = memcmp(check_buff + offset + offsets[i], 
		     buff + total_size, sizes[i]);
	if(res != 0)
	{
	    if(verbose)
	    {
		printf("Invalid result: offset: %d, size: %d\n",
		       (int32_t)offsets[i], (int32_t)sizes[i]);
	    }
	    return res;
	}
	total_size += sizes[i];
    }

    return 0;

}

int check_results(
    PVFS_offset offset,
    int total_read,
    PVFS_offset * offsets,
    int32_t * sizes,
    int count)
{
    int total_size = 0;
    int i = 0;
    int res;

    for(; i < count; ++i)
    {
	if(total_read < (total_size + sizes[i]))
	{
	    sizes[i] = (total_read - total_size);
	}
	   
	res = memcmp(
	    check_buff + offset + offsets[i], buff + total_size, sizes[i]);
	if(res != 0)
	{
	    if(verbose)
	    {
		printf("Invalid result: offset: %u, size: %d\n", 
		       (int32_t)offsets[i], (int32_t)sizes[i]);
	    }
	    return res;
	}
	total_size += sizes[i];
    }

    return 0;
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
    
    if(verbose)
    {
	fprintf(stdout, 
	    "READING:\t");
    }
 
    sizes = malloc(sizeof(int32_t) * chunks);
    offsets = malloc(sizeof(PVFS_size) * chunks);

    va_start(args_list, chunks);

    for(i = 0; i < chunks; ++i)
    {
	offsets[i] = (PVFS_size)va_arg(args_list, int);
	sizes[i] = (int32_t)va_arg(args_list, int);

	if(verbose)
	{
	    total += fprintf(stdout,
			     "%.*s<%.*s>",
			     (int) (offsets[i] - total), spaces,
			     (int) (sizes[i] - 2), spaces);
	}

    }

    va_end(args_list);

    if(verbose)
    {
	fprintf(stdout, "    (memsize = %d)\n", memsize);
    }

    PVFS_Request_indexed(chunks, sizes, offsets, PVFS_BYTE, &readreq);
    PVFS_Request_contiguous(memsize, PVFS_BYTE, &memreq);

    reset_buff();

    res = PVFS_sys_read(
	ref, readreq, 0, buff, memreq, creds, &io_resp, NULL);
    if(res < 0)
    {
	PVFS_perror("read failed with errcode", res);
	return res;
    }

    if(verbose)
    {
	print_buff(0, io_resp.total_completed);
    }
    
    res = check_smallmem_results(
	0, io_resp.total_completed, memsize, offsets, sizes, chunks);

    free(offsets);
    free(sizes);

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
    
    if(verbose)
    {
	fprintf(stdout, 
		"READING:\t");
    }

    sizes = malloc(sizeof(int32_t) * chunks);
    offsets = malloc(sizeof(PVFS_size) * chunks);

    va_start(args_list, chunks);

    for(i = 0; i < chunks; ++i)
    {
	offsets[i] = (PVFS_size)va_arg(args_list, int);
	sizes[i] = (int32_t)va_arg(args_list, int);

	if(verbose)
	{
	    total += fprintf(stdout,
			     "%.*s<%.*s>",
			     (int) (offsets[i] - total), spaces,
			     (int) (sizes[i] - 2), spaces);
	}
    }
    
    va_end(args_list);

    if(verbose)
    {
	fprintf(stdout, "\n");
    }

    PVFS_Request_indexed(chunks, sizes, offsets, PVFS_BYTE, &readreq);
    PVFS_Request_size(readreq, &readreq_size);
    PVFS_Request_contiguous(readreq_size, PVFS_BYTE, &memreq);

    reset_buff();

    res = PVFS_sys_read(
	ref, readreq, 0, buff, memreq, creds, &io_resp, NULL);
    if(res < 0)
    {
	PVFS_perror("read failed with errcode", res);
	return res;
    }

    if(verbose)
    {
	print_buff(0, io_resp.total_completed);
    }

    res = check_results(0, io_resp.total_completed, offsets, sizes, chunks);

    free(offsets);
    free(sizes);

    return res;
}

int do_contig_read(
    PVFS_object_ref ref, 
    PVFS_credentials * creds, 
    PVFS_offset offset, 
    int length,
    PVFS_Request freq)
{
    int res;
    PVFS_sysresp_io     io_resp;
    PVFS_Request memreq, readreq;

    if(verbose)
    {
	fprintf(stdout, 
		"READING:\t"
		"%.*s<%.*s>\n",
		(int)offset, spaces,
		length - 2, spaces);
    }

    res = PVFS_Request_contiguous(length, PVFS_BYTE, &memreq);
    if(res < 0)
    {
	PVFS_perror("request contig for memory failed with errcode", res);
	return res;
    }

    if(freq)
    {
	readreq = freq;
    }
    else
    {
	res = PVFS_Request_contiguous(length, PVFS_BYTE, &readreq);
	if(res < 0)
	{
	    PVFS_perror("request contig for file failed with errcode", res);
	    return res;
	}
    }

    reset_buff();

    res = PVFS_sys_read(
	ref, readreq, offset, buff, memreq, creds, &io_resp, NULL);
    if(res < 0)
    {
	PVFS_perror("read failed with errcode", res);
	return res;
    }

    if(verbose)
    {
	print_buff(offset, io_resp.total_completed);
    }

    res = check_results(0, io_resp.total_completed, 
			(PVFS_offset *)&offset, &length, 1);

    return res;
}

int do_write(PVFS_object_ref ref, PVFS_credentials * creds,
	     PVFS_offset offset, PVFS_size size, char * buff)
{
    PVFS_Request filereq;
    PVFS_Request memreq;
    PVFS_sysresp_io     io_resp;
    int res;

    /* setup check_buff */
    memcpy(check_buff + offset, buff, size);

    if(verbose)
    {
	fprintf(stdout,
		"WRITING:\t"
		"%.*s%.*s\n\n",
		(int32_t)offset, spaces,
		(int32_t)size, buff);
    }

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
	offset, buff, memreq, creds, &io_resp, NULL);
    if(res < 0)
    {
	PVFS_perror("write failed with errcode", res);
	return -1;
    }

    return 0;
}

static void usage(void)
{
    printf("usage: test-zero-fill [<OPTIONS>...]\n");
    printf("\n<OPTIONS> is one of\n");
    printf("-v\t\tverbose mode.  prints out read results\n");
    printf("-s <strip size>\tstrip size to use when creating the file\n");
    printf("-h\t\tprint this help\n");
}

#define ZEROFILL_FILENAME "test-zerofill"

extern char *optarg;
extern int optind, opterr, optopt;

int main(int argc, char * argv[])
{
    PVFS_sysresp_create create_resp;
    PVFS_sysresp_lookup lookup_resp;
    PVFS_fs_id curfs;
    PVFS_sys_attr attr;
    PVFS_credentials creds;
    PVFS_sys_dist * dist;
    int strip_size = 9;
    int half_strip;
    int before_len, after_len;
    char zerofill_fname[100];
    PVFS_simple_stripe_params params;
    int32_t res, realres;
    char c;

    memset(check_buff, 0, 1000);

    while((c = getopt(argc, argv, "vs:")) != EOF)
    {
	switch(c)
	{
	    case 'v':
		verbose = 1;
		break;
	    case 's':
		strip_size = atoi(optarg);
		break;
	    case 'h':
		usage();
		exit(0);
	    case '?':
		usage();
		exit(1);
	    default:
		break;
	}
    }

    half_strip = (int) strip_size / 2;

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

    if(verbose)
    {

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
    }

    res = PVFS_sys_lookup(curfs, "/", &creds, &lookup_resp, 0, NULL);
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

    if(verbose)
    {
	fprintf(stdout, "filename: %s\n", zerofill_fname);
    }

    res = PVFS_sys_create(
	zerofill_fname, lookup_resp.ref, attr, &creds, dist, &create_resp, NULL, NULL);
    if(res < 0)
    {
	PVFS_perror("create failed with errcode", res);
	return -1;
    }

    half_strip = strip_size / 2;

    do_write(create_resp.ref, &creds, 0, half_strip, as);

    do_write(create_resp.ref, &creds, strip_size * 2, half_strip, bs);

    res = do_contig_read(
	create_resp.ref, &creds, 0, strip_size + half_strip, NULL);
    if(res < 0)
    {
	goto exit;
    }
	
    res = do_contig_read(
	create_resp.ref, &creds, 0, strip_size + half_strip, PVFS_BYTE);
    if(res < 0)
    {
	goto exit;
    }

    res = do_contig_read(
	create_resp.ref, &creds, 
	(strip_size + half_strip), (strip_size + half_strip), NULL);
    if(res < 0)
    {
	goto exit;
    }

    res = do_contig_read(
	create_resp.ref, &creds, 
	(strip_size + half_strip), (strip_size + half_strip), PVFS_BYTE);
    if(res < 0)
    {
	goto exit;
    }

    res = do_contig_read(
	create_resp.ref, &creds, 0, (strip_size * 3), NULL);
    if(res < 0)
    {
	goto exit;
    }

    res = do_contig_read(
	create_resp.ref, &creds, 0, (strip_size * 3), PVFS_BYTE);
    if(res < 0)
    {
	goto exit;
    }

    res = do_contig_read(
	create_resp.ref, &creds, 0, half_strip + 2, NULL);
    if(res < 0)
    {
	goto exit;
    }

    res = do_contig_read(
	create_resp.ref, &creds, 0, half_strip + 2, PVFS_BYTE);
    if(res < 0)
    {
	goto exit;
    }

    res = do_contig_read(
	create_resp.ref, &creds, half_strip + 2, strip_size, NULL);
    if(res < 0)
    {
	goto exit;
    }

    res = do_contig_read(
	create_resp.ref, &creds, half_strip + 2, strip_size, PVFS_BYTE);
    if(res < 0)
    {
	goto exit;
    }

    res = do_contig_read(
	create_resp.ref, &creds, strip_size + 1, 3, NULL);
    if(res < 0)
    {
	goto exit;
    }

    res = do_contig_read(
	create_resp.ref, &creds, strip_size + 1, 3, PVFS_BYTE);
    if(res < 0)
    {
	goto exit;
    }

    res = do_contig_read(
	create_resp.ref, &creds, 0, 2, NULL);
    if(res < 0)
    {
	goto exit;
    }

    res = do_contig_read(
	create_resp.ref, &creds, 0, 2, PVFS_BYTE);
    if(res < 0)
    {
	goto exit;
    }

    do_write(create_resp.ref, &creds, strip_size * 4, half_strip, cs);

    res = do_contig_read(
	create_resp.ref, &creds,
	(strip_size * 3 + half_strip), (strip_size + half_strip), NULL);
    if(res < 0)
    {
	goto exit;
    }

    res = do_contig_read(
	create_resp.ref, &creds,
	(strip_size * 3 + half_strip), (strip_size + half_strip), PVFS_BYTE);
    if(res < 0)
    {
	goto exit;
    }

    res = do_contig_read(
	create_resp.ref, &creds,
	0, (strip_size * 5), NULL);
    if(res < 0)
    {
	goto exit;
    }

    res = do_contig_read(
	create_resp.ref, &creds,
	0, (strip_size * 5), PVFS_BYTE);
    if(res < 0)
    {
	goto exit;
    }

    res = do_contig_read(
	create_resp.ref, &creds, 
	(strip_size + half_strip), (strip_size + half_strip), NULL);
    if(res < 0)
    {
	goto exit;
    }

    res = do_contig_read(
	create_resp.ref, &creds, 
	(strip_size + half_strip), (strip_size + half_strip), PVFS_BYTE);
    if(res < 0)
    {
	goto exit;
    }

    res = do_contig_read(
	create_resp.ref, &creds,
	0, (strip_size * 3), NULL);
    if(res < 0)
    {
	goto exit;
    }

    res = do_contig_read(
	create_resp.ref, &creds,
	0, (strip_size * 3), PVFS_BYTE);
    if(res < 0)
    {
	goto exit;
    }

    res = do_noncontig_read(
	create_resp.ref, &creds,
	2,
	0, strip_size + half_strip,
	strip_size * 4, half_strip);
    if(res < 0)
    {
	goto exit;
    }
    
    res = do_noncontig_read(
	create_resp.ref, &creds,
	2,
	0, strip_size,
	strip_size * 5, half_strip);
    if(res < 0)
    {
	goto exit;
    }

    res = do_noncontig_read(
	create_resp.ref, &creds,
	2,
	0, strip_size,
	strip_size * 3, half_strip);
    if(res < 0)
    {
	goto exit;
    }

    res = do_noncontig_read(
	create_resp.ref, &creds,
	2,
	strip_size, strip_size,
	strip_size * 4 + 2, strip_size);
    if(res < 0)
    {
	goto exit;
    }

    res = do_noncontig_read(
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
    if(res < 0)
    {
	goto exit;
    }

    res = do_smallmem_noncontig_read(
	create_resp.ref, &creds,
	strip_size,
	4,
	strip_size, half_strip,
	strip_size * 2, half_strip,
	strip_size * 3, half_strip,
	strip_size * 4, half_strip);
    if(res < 0)
    {
	goto exit;
    }
    
    res = do_smallmem_noncontig_read(
	create_resp.ref, &creds,
	strip_size,
	2,
	strip_size, 2,
	strip_size * 4, strip_size);
    if(res < 0)
    {
	goto exit;
    }
    
    PVFS_sys_remove(
	zerofill_fname,
	lookup_resp.ref,
	&creds,
        NULL);

exit:
    
    realres = res;
    
    res = PVFS_sys_finalize();
    if(res < 0)
    {
	printf("finalizing sysint failed with errcode = %d\n", res);
	realres = res;
    }

    return realres;
}

