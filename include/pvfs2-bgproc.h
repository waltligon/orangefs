/*
 * (C) 2013 Omnibond Systems, L.L.C.
 *
 * See COPYING in top-level directory.
 */

#ifndef PVFS2_BGPROC_H
#define PVFS2_BGPROC_H

/* bgproc-log.h */
int bgproc_printf(char *fmt, ...);

/* bgproc-prop.h */
struct bgproc_prop {
	char *key;
	int type;
	void *value;
};

#define BGPROC_TYPE_INT 1
#define BGPROC_TYPE_STR 2

int bgproc_newprop(char *key, int type);
int bgproc_delprop(char *key);
int bgproc_iterstart(void);
char *bgproc_iternext(int i);
int bgproc_unset(char *key);
int bgproc_gettype(char *key);
int bgproc_set_int(char *key, int value);
int bgproc_get_int(char *key, int *value);
int bgproc_set_str(char *key, char *value);
int bgproc_get_str(char *key, char **value);
int bgproc_flushprop(void);

/* bgproc-start.h */
int bgproc_start(int argc, char *[]);
char *bgproc_getdir(void);
void bgproc_setdir(char *dir);

#endif
