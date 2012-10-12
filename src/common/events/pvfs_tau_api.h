/*****************************************************
 * File    : pvfs_tau_api.h 
 * Version : $Id: pvfs_tau_api.h,v 1.2 2008-11-20 01:16:52 slang Exp $
 ****************************************************/

/* Author: Aroon Nataraj */
#ifndef _PVFS_TAU_API_H
#define _PVFS_TAU_API_H

#ifdef __cplusplus
extern "C" {
#endif

/* Some macros *
 ***************/
#define TP_MAX_THREADS 1024
#define TP_MAX_EVENTS 128
#define TP_MAX_STR 256
#define TAU_Log_get_event_number(THR) ((THR)->tau_no++)
#define TAUPVFS_MAX TP_MAX_THREADS

#include <stdarg.h>
#include <TAU_tf_writer.h>

/* tau_thread_group_info: 
 * Grouping threads based on equiv-classes. Here
 * this functionality is used to allow multiple
 * threads (which are not alive at the same time) to
 * share a single trace file.
 */
struct tau_thread_group_info {
    char name[20];
    int max;
    int blocking; 
    int buffer_size;
};

int Ttf_init(
    int process_id, char* folder, char* filename_prefix, int buffer_size);
int Ttf_finalize(void);

int Ttf_thread_start(
    struct tau_thread_group_info* info, int* thread_id, int* pisnew);
int Ttf_thread_stop(void);

int Ttf_event_define(char* name, char* format_start_event_info, char* format_end_event_info, int* event_type);
int Ttf_EnterState_info(int event_type, int process_id, int* thread_id, int* event_id, ...);
int Ttf_EnterState_info_va(int event_type, int process_id, int* thread_id, int* event_id, va_list ap);
int Ttf_LeaveState_info(int event_type, int process_id, int* thread_id, int event_id, ...);
int Ttf_LeaveState_info_va(int event_type, int process_id, int* thread_id, int event_id, va_list ap);

#ifdef __cplusplus
}
#endif

#endif /* _PVFS_TAU_API_H */
