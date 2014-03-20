/*
 * (C) 2013 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <unistd.h>

#include <pvfs2-bgproc.h>

int main(int argc, char *argv[])
{
	int r;
	int i = 0;
	if ((r = bgproc_start(argc, argv)) != 0) {
		return r;
	}
	bgproc_log("Hello, world!");

	bgproc_newprop("numfiles", BGPROC_TYPE_INT);
	bgproc_newprop("numdirs", BGPROC_TYPE_INT);
	bgproc_newprop("i", BGPROC_TYPE_INT);

	bgproc_set_int("numdirs", 48);
	bgproc_set_int("numfiles", 84);
	bgproc_set_int("i", i);

	bgproc_flushprop();

	while (i < 60) {
		sleep(1);
		bgproc_set_int("i", i++);
		bgproc_flushprop();
	}

	return 42;
}
