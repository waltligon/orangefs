#ifndef _TP_PROTO_H
#define _TP_PROTO_H
#include "tp_error.h"

/* EXPORTED HEADER FILES TO USER_SPACE CLIENTS ONLY */

typedef void * PVOID;
typedef PVOID (*pthread_work_function)(PVOID);
typedef int tp_id;

/* Parameters for the thread pool */
typedef struct {
	int tpi_count;
	long tpi_stack_size;
	char *tpi_name;
}tp_info;

/* 
   EXPORTED ROUTINES TO CLIENTS 
	All the routines return either an
	error code (which is negative) or 
	a non-negative integer on success. 
*/

extern tp_id   tp_init(tp_info *);
extern int     tp_cleanup_by_id(tp_id);
extern int     tp_cleanup_by_name(const char*);
extern int     tp_assign_work_by_id(tp_id,pthread_work_function,PVOID);
extern int     tp_assign_work_by_name(const char*,pthread_work_function,PVOID);
extern int 	   tp2errno(int tp_error);

/* spinlock routines exported to clients */

typedef int tp_spinlock_t;
extern void tp_spinlock(tp_spinlock_t *spinlock);
extern void tp_spinunlock(tp_spinlock_t *spinlock);
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
