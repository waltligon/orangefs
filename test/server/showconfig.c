
/* stub program to make sure the build rules are correct
 */

#include "trove.h"
#include "pvfs2-storage.h"
#include "dotconf.h"
#include "server-config.h"

int main(int argc, char **argv)
{
	int i;
	filesystem_configuration_s fileconfig;
	server_configuration_s serverconfig;

	for (i=1; i<argc; i++) {
		printf("parsing %s\n", argv[i]);
	}
	return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
