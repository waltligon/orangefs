#ifndef _TP_ERROR_H
#define _TP_ERROR_H
#include <stdio.h>
#include <errno.h>
enum {
	TP_INVALID_POOL_ID = -1,
	TP_INVALID_POOL_NAME = -2,
	TP_TOO_MANY_RESOURCES = -3,
	TP_INVALID_PARAMETER = -4,
	TP_NOMEMORY = -5,
	TP_FATAL = -6,
	TP_NOPOOL = -7,
	TP_SYSTEM_ERROR = -8,
	TP_UNINITIALIZED = -9,
	TP_MAX_ERROR = -10,
};

extern char *tp_err_list[];


extern void tp_perror(int tp_error);
extern char *tp_strerror(int tp_error);
#endif
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 
