/*
 * (C) 2013 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

extern int bgproc_start(int argc, char *argv[]);

int main(int argc, char *argv[])
{
	int r;
	if ((r = bgproc_start(argc, argv)) != 0) {
		return r;
	}
	bgproc_log_printf("Hello, world!");
	return 42;
}
