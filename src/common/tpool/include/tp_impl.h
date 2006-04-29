#ifndef _TP_IMPL_H
#define _TP_IMPL_H
#include "tp_proto.h"
extern int tp_setup(void);
extern void tp_cleanup(void);
tp_id tp_register(tp_info *info);
int tp_unregister_by_id(tp_id id);
int tp_unregister_by_name(const char* name);
int do_assign_work_by_id(tp_id, pthread_work_function,PVOID); 
int do_assign_work_by_name(const char *, pthread_work_function , PVOID);
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
