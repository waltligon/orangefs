#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>
#include <arpa/inet.h>

#define __PINT_REQPROTO_ENCODE_FUNCS_C
#include "pvfs2.h"
#include "pvfs2-mgmt.h"
#include "pvfs2-types.h"
#include "pvfs2-debug.h"
#include "gossip.h"
#include "vec_prot.h"
#include "vec_prot_client.h"
#include "vec_common.h"

#define PORT 4334

static void serialize_handle(char **synch_handle, PVFS_object_ref *ref)
{
    encode_PVFS_fs_id(synch_handle, &ref->fs_id);
    encode_PVFS_handle(synch_handle, &ref->handle);
    return;
}

static void deserialize_handle(char **synch_handle, PVFS_object_ref *ref)
{
    decode_PVFS_fs_id(synch_handle, &ref->fs_id);
    decode_PVFS_handle(synch_handle, &ref->handle);
}

#define NSERVERS 		1
#define STRIPE_SIZE  65536
#define MAX_NVECTORS  1
#define OFFSET       0
#define SIZE         131072

static void usage(char *str)
{
	fprintf(stderr, "Usage: %s "
			" -s <size> -n <nservers> -z <stripe size> -m <max vectors> -o <offset> -w {write}\n", str);
	return;
}

int main(int argc, char *argv[])
{
	struct vec_options opt;
	struct sockaddr_in addr;
	int c, nservers, stripe_size, max_nvectors = 1;
	vec_offset_t offset;
	vec_size_t  size;
	vec_handle_t h;
	char *ptr = h;
	PVFS_object_ref ref = {1048576, 10};
	int reader;

	nservers = NSERVERS;
	stripe_size = STRIPE_SIZE;
	max_nvectors = MAX_NVECTORS;
	offset = OFFSET;
	size = SIZE;
	reader = 1;
	while ((c = getopt(argc, argv, "s:n:z:m:o:w")) != EOF) {
		switch (c) {
			case 'w':
			{
				reader = 0;
				break;
			}
			case 'n':
			{
				nservers = atoi(optarg);
				break;
			}
			case 's':
			{
				char *ptr = NULL;
				size = strtoll(optarg, &ptr, 10);
				break;
			}
			case 'o':
			{
				char *ptr = NULL;
				offset = strtoll(optarg, &ptr, 10);
				break;
			}
			case 'z':
			{
				stripe_size = atoi(optarg);
				break;
			}
			case 'm':
			{
				max_nvectors = atoi(optarg);
				break;
			}
			case '?':
			default:
				usage(argv[0]);
				exit(1);
		}
	}
	gossip_enable_stderr();
	gossip_set_debug_mask(1, GOSSIP_VEC_DEBUG);
	serialize_handle((char **) &ptr, &ref);
	opt.tcp = 1;
	memset(&addr, 0, sizeof(addr));
	addr.sin_port = htons(PORT);
	if (inet_aton("127.0.0.1", &addr.sin_addr) == 0) {
		exit(1);
	}
	opt.vec_addr = (struct sockaddr *) &addr;
	if (vec_ping(&opt) < 0) {
		printf("Version server: Not Ok\n");
		exit(1);
	}
	else {
		printf("Version server: Ok\n");

		if (reader)
		{
			vec_svectors_t sv;
			int err;

			if ((err = vec_get(&opt, h, VEC_READ_MODE, offset, size, 
							stripe_size, nservers, max_nvectors, NULL, &sv)) < 0) {
				fprintf(stderr, "vec_get failed with error %d\n", err);
				exit(1);
			}
			printf("%d\n", sv.vec_svectors_t_len);
			svec_print(&sv);
			svec_dtor(&sv, sv.vec_svectors_t_len);
		}
		else {
			int err;
			vec_vectors_t v;

			if ((err = vec_put(&opt, h, VEC_WRITE_MODE, offset, size,
							stripe_size, nservers, &v)) < 0) {
				fprintf(stderr, "vec_put failed with error %d\n", err);
				exit(1);
			}
			vec_print(&v);
			vec_dtor(&v);
		}
		return 0;
	}
}
