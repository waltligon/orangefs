/*****************************************************
 * File    : pvfs_tau_api.cpp 
 * Version : $Id: pvfs_tau_api.c,v 1.2 2008-11-20 01:16:52 slang Exp $
 ****************************************************/

/* Author: Aroon Nataraj */

#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdarg.h>

#include <map>
using namespace std;

#include <Profile/Profiler.h>
#include <Profile/RtsLayer.h>
#include "TAU_tf.h"

#include <pvfs_tau_api.h>

#include <fmt_fsm.h>

/* Ttf_closed_event_def: 
 * Stuct to hold descriptions about Start/Stop events
 * , in particular, the format expected of the var-arg
 * calls.
 */ 
struct Ttf_closed_event_def {
    char name[TP_MAX_STR];
    ff_format fmt_start;
    int start_no;
    ff_format fmt_end;
    int end_no;
    int index;
    int inited;
      
    //cons
    Ttf_closed_event_def() {
	start_no = end_no = 0;
	inited = 0;
	name[0] = '\0';
	index = 0;
    }

    Ttf_closed_event_def& operator=(const Ttf_closed_event_def& thecopy) {
	strncpy(name, thecopy.name, TP_MAX_STR);
        fmt_start.init(thecopy.fmt_start);
        fmt_end.init(thecopy.fmt_end);
        start_no = thecopy.start_no;
        end_no = thecopy.end_no;
	index = thecopy.index;

	inited = 1;
	
	return *this;
    }

    void init(char* _name, char* _fmt_start, char* _fmt_end, int _index, int _start_no, int _end_no) {
        strncpy(name, _name, TP_MAX_STR);
	fmt_start.init(_fmt_start);
	fmt_start.parse();
	fmt_end.init(_fmt_end);
	fmt_end.parse();
	start_no = _start_no;
	end_no = _end_no;
	index = _index;
	inited = 1;
   }

   Ttf_closed_event_def(char* _name, char* _fmt_start, char* _fmt_end, int _index, int _start_no, int _end_no) {
	init(_name, _fmt_start, _fmt_end, _index, _start_no, _end_no);
   }

};


/* Ttf_thread_bundle: 
 * Several things are managed on a per-thread basis.
 * This allows eliminating or minimizing locking.
 * The thread-bundle, for lack of a better name, is
 * meant to keep track of the per-thread state.
 */ 
struct Ttf_thread_bundle {
    Ttf_closed_event_def* events;
    Ttf_FileHandleT taufile;
    int tau_no;
    x_uint32 event_no;

    #define TP_SCRATCH_SZ (8*50)
    char scratch[TP_SCRATCH_SZ];
    int scratch_pos;

    //cons
    Ttf_thread_bundle() {
        events = NULL;
        tau_no = 0;
        taufile = NULL;
        event_no = 0;
	scratch_pos = 0;
    }
    //des?
    //TODO
};


/* g_t_bundles:
 * For now we declare a bunch of these bundle-ptrs
 * and allocate them as and when required.
 * TODO: should we do better?
 ****************************/
extern Ttf_thread_bundle* g_t_bundles[TP_MAX_THREADS];


//Debugging stuff
//---------------
//#define TAU_DEBUG 1   //comment out to unset debugging
#define TAU_NAME "pvfs_tau_api"
#include "debug.h"


//Declarations for some TAU routines (these arent in TauAPI.h now)
//----------------------------------------------------------------
extern "C" {
extern double Tau_get_timestamp(int tid);
extern int Tau_get_tid(void);
}


//Fwd decl of utils
//-----------------
int tau_thread_init(int tid, int max_elements);
static int init_master_bundle();
static Ttf_thread_bundle* get_master_bundle();
static void put_master_bundle(Ttf_thread_bundle* master_bundle);
static Ttf_thread_bundle* get_t_bundle(int tid);
static int refresh_bundle(Ttf_thread_bundle *gbl_bundle, Ttf_thread_bundle *lc_bundle);


//We need an enclosing dummy main for instances where no such overall enclosing events are present
static int PINT_dummy_main;


//The Global Master bundle and locks
static Ttf_thread_bundle* g_master_bundle = NULL;
static pthread_mutex_t ttf_MasterBundleMutex;
static pthread_mutexattr_t ttf_MasterBundleMutexAttr;

//The per-thread state management bundles
Ttf_thread_bundle* g_t_bundles[TP_MAX_THREADS];


//Process-level vars (safe to be globals - since process-level)
//these names are far too generic to be externally visible - 
//TODO: give them a prefix 
//-------------------------
int g_pid = -1;
char g_traceloc[TP_MAX_STR];
char g_filepfx[TP_MAX_STR];
int g_defbufsz = -1;


//The API definition follows
//--------------------------

////////////////////////////////////////////////////////////////////////////////
extern "C" int Ttf_init(int process_id, char* folder, char* filename_prefix, int buffer_size) {

    info("Ttf_init: pid:%d folder:%s pfx:%s bsize:%d\n", process_id, folder, filename_prefix, buffer_size);    

    g_pid = process_id;
    strncpy(g_traceloc, folder, TP_MAX_STR);   
    strncpy(g_filepfx, filename_prefix, TP_MAX_STR);
    g_defbufsz = buffer_size;

    TAU_INIT(NULL, NULL);
#ifndef TAU_MPI
    TAU_PROFILE_SET_NODE(0);
#endif /* TAU_MPI */

    if(init_master_bundle()) {
        err("Ttf_init: init_master_bundle() failed.");
    return -1;
    }

    int i = 0;
    for(i=0; i<TP_MAX_THREADS; i++) {
        g_t_bundles[i] = NULL;
    }

    Tau_get_tid(); //this is sort of a hack. Internal code assumes this code path is traversed before other thread-related code. To be fixed...

    tau_thread_init(0, buffer_size);

    info("Ttf_init: Exit\n");

    return 0;
}


////////////////////////////////////////////////////////////////////////////////
extern "C" int Ttf_thread_start(struct tau_thread_group_info* info, int* thread_id, int* pisnew) {
    int err = 0;
    int tmp_sz = info->buffer_size;

    info("Ttf_thread_start: *info:%p\t name:%s\t max:%d\n", info, info->name, info->max);

    Tau_get_tid(); //this is sort of a hack. Internal code assumes this code path is traversed before other thread-related code. To be fixed...
    RtsLayer::RegisterThreadInGroup(info->name, info->max, pisnew);
    *thread_id = Tau_get_tid();
    if(tmp_sz <= 0) tmp_sz = g_defbufsz;
    err = tau_thread_init(*thread_id, tmp_sz);

    Ttf_thread_bundle *bundle = get_t_bundle(*thread_id);

    //copy over master-bundle to local bundle
    Ttf_thread_bundle *gbl_bundle = get_master_bundle();
    refresh_bundle(gbl_bundle, bundle);
    put_master_bundle(gbl_bundle);

    info("Ttf_thread_start: Exit\n");

    return err;
}


////////////////////////////////////////////////////////////////////////////////
extern "C" int Ttf_thread_stop() {
    int tid = Tau_get_tid();
    info("Ttf_thread_stop:%d\n", tid);
    RtsLayer::ReleaseThreadInGroup();
    info("Ttf_thread_stop: Exit: %d\n", tid);
    return 0;
}


////////////////////////////////////////////////////////////////////////////////
extern "C" int Ttf_finalize(void)
{
    int i, no_t = 0;

    info("Ttf_finalize: Enter.\n");

    for(i =0; i<TP_MAX_THREADS; i++) {
        if((g_t_bundles[i] == NULL) || (g_t_bundles[i]->taufile == NULL)) {
            continue;
        }
    Ttf_LeaveState(g_t_bundles[i]->taufile, (x_uint64) Tau_get_timestamp(i), g_pid, i, PINT_dummy_main);
    Ttf_CloseOutputFile(g_t_bundles[i]->taufile);
    no_t++;
    }

    //TODO - finalize may be called before end of program - MUST not leak mem - clean up here
    
    info("Ttf_finalize: Exit (finned %d threads).\n", no_t);

    return 0;
}


////////////////////////////////////////////////////////////////////////////////
extern "C" int Ttf_event_define(char* name, char* format_start_event_info, char* format_end_event_info, int* event_type) {

    int tid = Tau_get_tid();

    info("Ttf_event_define: tid:%d name:%s fmt_start:%s fmt_end:%s *event_type:%p\n", tid, name, format_start_event_info, format_end_event_info, event_type);

    //1. Register event definition on global master bundle
    Ttf_thread_bundle *gbl_bundle = get_master_bundle();

    int the_type = TAU_Log_get_event_number(gbl_bundle); 
    *event_type = the_type;
    
    int start_type = 0, end_type = 0;

    start_type = TAU_Log_get_event_number(gbl_bundle);
    end_type = TAU_Log_get_event_number(gbl_bundle);

    gbl_bundle->events[the_type].init(name, format_start_event_info, format_end_event_info, *event_type, start_type, end_type);
    
    //2. Bring local bundle up to date with master bundle
    Ttf_thread_bundle *lc_bundle = get_t_bundle(tid);

    if(lc_bundle->events == NULL) {
        lc_bundle->events = new Ttf_closed_event_def[TP_MAX_EVENTS];
        if(!lc_bundle->events) {
            err("refresh_bundle: new lc_bundle->events returned NULL.\n");
            return -1;
        }
    }

    if(lc_bundle->tau_no < gbl_bundle->tau_no) {
        refresh_bundle(gbl_bundle, lc_bundle);
    } else {
        err("Ttf_event_define: No.Evs in local bundle(%d) >= global bundle(%d).", lc_bundle->tau_no, gbl_bundle->tau_no);
    }

    //3. Return master bundle
    put_master_bundle(gbl_bundle);

    info("Ttf_event_define: name:%s event_type:%d\t *bundle:%p *event:%p\n", name, *event_type, lc_bundle, &(lc_bundle->events[the_type]));

    return 0;
}


////////////////////////////////////////////////////////////////////////////////
extern "C" int Ttf_EnterState_info(int event_type, int process_id, int* thread_id, int* event_id, ...) {

	int ret;
	va_list ap;
	va_start(ap, event_id);
	ret = Ttf_EnterState_info_va(event_type, process_id, thread_id, event_id, ap);
	va_end(ap);
	return ret;
}

////////////////////////////////////////////////////////////////////////////////
extern "C" int Ttf_EnterState_info_va(int event_type, int process_id, int* thread_id, int* event_id, va_list ap) {
    int tid = Tau_get_tid();

    x_uint64 tstamp = (x_uint64) Tau_get_timestamp(tid);

    info("Ttf_EnterState_info: tid:%d event_type:%d pid:%d *event_id:%p\n", tid, event_type, process_id, event_id);

    Ttf_thread_bundle *bundle = get_t_bundle(tid);
    
    if(bundle->events == NULL) {
        err("Ttf_EnterState_info: lc_bundle->events is NULL - no events defined yet? (tau_no==%d)", bundle->tau_no);
        return -1;
    }

    //check if "lookup" fails - requires bringing local bundle uptodate
    if(event_type >= bundle->tau_no) {
        Ttf_thread_bundle *gbl_bundle = get_master_bundle();
        refresh_bundle(gbl_bundle, bundle);
        put_master_bundle(gbl_bundle);
    }

    //if lookup continues to fail...
    if(event_type >= bundle->tau_no) {
        err("Ttf_EnterState_info: event_type(%d) not yet defined.", event_type);
        return -1;
    }

    Ttf_closed_event_def *event_info = &(bundle->events[event_type]);

    Ttf_EnterState(bundle->taufile, tstamp, g_pid, tid, event_type);

    *event_id = bundle->event_no++;

    x_uint32 eid = *event_id;

    bundle->scratch_pos = 0;

    memcpy(bundle->scratch+bundle->scratch_pos, &(eid), sizeof(eid));//1st encode the event id
    bundle->scratch_pos += sizeof(eid);

    //suck from var-args
    if(event_info->fmt_start.v_tot_size > 0) {
        int tmp_sz = 0;
        event_info->fmt_start.suck(ap, bundle->scratch + bundle->scratch_pos, &tmp_sz);
        bundle->scratch_pos += tmp_sz;
    }

    memset(bundle->scratch + bundle->scratch_pos, 0, 8); //just to zero out any unused fragment (since record is 8bytes long - memsetting 8 bytes is enough)
    for(x_uint64* ptr = (x_uint64*)bundle->scratch; (unsigned long) ptr < (unsigned long)(bundle->scratch + bundle->scratch_pos); ptr++) {
      Ttf_EventTrigger(bundle->taufile, tstamp, g_pid, tid, event_info->start_no, *ptr);
    }

    info("Enter(After): Encoded eid:%d into scratch as:%d\n", eid, (*((x_uint32*)bundle->scratch)));

    info("Ttf_EnterState_info: *bundle:%p\t *event_info:%p\t v_parsed_fmt:%s\t v_tot_size:%d\n", bundle, event_info, event_info->fmt_start.v_parsed_fmt, event_info->fmt_start.v_tot_size);

    info("Ttf_EnterState_info: Exit: tid:%d event_type:%d pid:%d event_id:%d\n", tid, event_type, process_id, *event_id);

    return 0;
}


extern "C" int Ttf_LeaveState_info(int event_type, int process_id, int* thread_id, int event_id, ...) {

	int ret;
	va_list ap;

	va_start(ap, event_id);
	ret = Ttf_LeaveState_info_va(event_type, process_id, thread_id, event_id, ap);
	va_end(ap);
	return ret;
}

////////////////////////////////////////////////////////////////////////////////
extern "C" int Ttf_LeaveState_info_va(int event_type, int process_id, int* thread_id, int event_id, va_list ap) {
    int tid = Tau_get_tid();

    x_uint64 tstamp = (x_uint64) Tau_get_timestamp(tid);

    info("Ttf_LeaveState_info: tid:%d event_type:%d pid:%d event_id:%d\n", tid, event_type, process_id, event_id);

    Ttf_thread_bundle *bundle = get_t_bundle(tid);
    
    if(bundle->events == NULL) {
        err("Ttf_LeaveState_info: lc_bundle->events is NULL - no events defined yet? (tau_no==%d)", bundle->tau_no);
        return -1;
    }

    //check if "lookup" fails - requires bringing local bundle uptodate
    if(event_type >= bundle->tau_no) {
        Ttf_thread_bundle *gbl_bundle = get_master_bundle();
        refresh_bundle(gbl_bundle, bundle);
        put_master_bundle(gbl_bundle);
    }
    //if lookup continues to fail...
    if(event_type >= bundle->tau_no) {
        err("Ttf_LeaveState_info: event_type(%d) not yet defined.", event_type);
        return -1;
    }


    Ttf_closed_event_def *event_info = &(bundle->events[event_type]);

    Ttf_LeaveState(bundle->taufile, tstamp, g_pid, tid, event_type);

    x_uint32 eid = event_id;

    bundle->scratch_pos = 0;

    memcpy(bundle->scratch + bundle->scratch_pos, &(eid), sizeof(eid));
    bundle->scratch_pos += sizeof(eid);

    if(event_info->fmt_end.v_tot_size > 0) {
        int tmp_sz = 0;
        event_info->fmt_end.suck(ap, bundle->scratch + bundle->scratch_pos, &tmp_sz);
            bundle->scratch_pos += tmp_sz;
    }

    memset(bundle->scratch + bundle->scratch_pos, 0, 8); //just to zero out any unused fragment (since record is 8bytes long - memsetting 8 bytes is enough)
    for(x_uint64* ptr = (x_uint64*)bundle->scratch; (unsigned long) ptr < (unsigned long)(bundle->scratch + bundle->scratch_pos); ptr++) {
      Ttf_EventTrigger(bundle->taufile, tstamp, g_pid, tid, event_info->end_no, *ptr);
    }

    info("Leave(After): Encoded eid:%d into scratch as:%d\n", eid, (*((x_uint32*)bundle->scratch)));

    //may be the event-no should be uniq?
    //bundle->event_no--;

    info("Ttf_LeaveState_info: Exit: tid:%d event_type:%d pid:%d event_id:%d\n", tid, event_type, process_id, event_id);
    
    return 0;
}


////////////////////////////////////////////////////////////////////////////////
extern "C" int Ttf_LogEvent_info(int event_type, int process_id, int* thread_id, int* event_id, double start_time, double end_time, ...) {
    int err = 0;

    if((err = Ttf_EnterState_info(event_type, process_id, thread_id, event_id)) != 0) {
       //error...!!!
       err("Ttf_LogEvent_info: Error from Ttf_EnterState_info: %d. Returning.\n", err);
       return err;
    }

    if((err = Ttf_LeaveState_info(event_type, process_id, thread_id, *event_id)) != 0) {
       //error...!!!
       err("Ttf_LogEvent_info: Error from Ttf_LeaveState_info: %d. Returning.\n", err);
       return err;
    }

    return 0;
}


//The Utils/Helpers of the API 
//----------------------------

////////////////////////////////////////////////////////////////////////////////
static Ttf_thread_bundle* get_t_bundle(int tid) {
    if(g_t_bundles[tid] == 0) {
        g_t_bundles[tid] = new Ttf_thread_bundle();
    }
    return g_t_bundles[tid];
}


////////////////////////////////////////////////////////////////////////////////
static int init_master_bundle() {
    //setup the mutex
    pthread_mutexattr_init(&ttf_MasterBundleMutexAttr);
    pthread_mutex_init(&ttf_MasterBundleMutex, &ttf_MasterBundleMutexAttr);

    //allocate and inialize the bundle
    if(g_master_bundle != NULL) {
        err("WARN: init_master_bundle: g_master_bundle(%p) != NULL\n", g_master_bundle);
    }
    g_master_bundle = new Ttf_thread_bundle();
    if(!g_master_bundle) {
        err("init_master_bundle: new g_master_bundle() returned NULL.\n");
        return -1;
    }
    g_master_bundle->events = new Ttf_closed_event_def[TP_MAX_EVENTS];
    if(!g_master_bundle->events) {
        err("init_master_bundle: new g_master_bundle->events returned NULL.\n");
        return -1;
    }

    PINT_dummy_main = TAU_Log_get_event_number(g_master_bundle);
    return 0;
}


////////////////////////////////////////////////////////////////////////////////
static Ttf_thread_bundle* get_master_bundle() {
    //lock mutex and return master bundle
    pthread_mutex_lock(&ttf_MasterBundleMutex);
    return g_master_bundle;
}


////////////////////////////////////////////////////////////////////////////////
static void put_master_bundle(Ttf_thread_bundle* master_bundle) {
    //unlock mutex and noop
    pthread_mutex_unlock(&ttf_MasterBundleMutex);
}


////////////////////////////////////////////////////////////////////////////////
int tau_thread_init(int tid, int max_elements) {
    
    Ttf_thread_bundle *bundle = NULL;

    info("tau_thread_init: tid:%d max_elements:%d\n", tid, max_elements);

    bundle = get_t_bundle(tid);

    char trcname[TP_MAX_STR*3];
    char edfname[TP_MAX_STR*3];

    if(bundle->taufile != NULL) {
      info("tau_thread_init: tid:%d Exit: Already inited. returning.\n", tid);
      return 0;
    }

    snprintf(trcname, (TP_MAX_STR*3), "%s/%s.%d.%d.0.trc", g_traceloc, g_filepfx, g_pid, tid);
    snprintf(edfname, (TP_MAX_STR*3), "%s/%s.%d.%d.edf", g_traceloc, g_filepfx, g_pid, tid);

    bundle->taufile = Ttf_OpenFileForOutput_wsize(trcname,edfname, max_elements);

    bundle->tau_no = 0;

    Ttf_DefThread(bundle->taufile, g_pid, tid, "GENTHREAD");
    
    PINT_dummy_main = TAU_Log_get_event_number(bundle);
    Ttf_DefStateGroup(bundle->taufile, "TAU_DEFAULT", 1);
    Ttf_DefState(bundle->taufile, PINT_dummy_main, "main", 1);
    Ttf_EnterState(bundle->taufile, (x_uint64) Tau_get_timestamp(tid), g_pid, tid, PINT_dummy_main);
    Ttf_FlushTrace(bundle->taufile);

    info("tau_thread_init: tid:%d Exit.\n", tid);

    return 0;
}


////////////////////////////////////////////////////////////////////////////////
//Assumes any required locking has been performed by caller (no locks taken/released here)
static int refresh_bundle(Ttf_thread_bundle *gbl_bundle, Ttf_thread_bundle *lc_bundle) {
    int no_copied = 0;

    if(lc_bundle->events == NULL) {
        lc_bundle->events = new Ttf_closed_event_def[TP_MAX_EVENTS];
        if(!lc_bundle->events) {
            err("refresh_bundle: new lc_bundle->events returned NULL.\n");
            return -1;
        }
    }

    while(lc_bundle->tau_no < gbl_bundle->tau_no) {
        int lc_no = TAU_Log_get_event_number(lc_bundle); 
        TAU_Log_get_event_number(lc_bundle);
        TAU_Log_get_event_number(lc_bundle);

        lc_bundle->events[lc_no] = gbl_bundle->events[lc_no];
        Ttf_closed_event_def* this_ev = &(lc_bundle->events[lc_no]);
    
        Ttf_DefState(lc_bundle->taufile, this_ev->index, this_ev->name, 1);

        char start_name[256];
        if(this_ev->fmt_start.no_patns > 0) {
            snprintf(start_name, 256, "_xxSTART_%s_FMT_%s%s", this_ev->name, "%d", this_ev->fmt_start.v_parsed_fmt);
        } else {
            snprintf(start_name, 256, "_xxSTART_%s_FMT_%s", this_ev->name, "%d");
        }
        Ttf_DefUserEvent(lc_bundle->taufile, this_ev->start_no, start_name, 0);

        char end_name[256];
        if(this_ev->fmt_end.no_patns > 0) {
            snprintf(end_name, 256, "_xxSTOP_%s_FMT_%s%s", this_ev->name, "%d", this_ev->fmt_end.v_parsed_fmt);
        } else {
            snprintf(end_name, 256, "_xxSTOP_%s_FMT_%s", this_ev->name, "%d");
        }
        Ttf_DefUserEvent(lc_bundle->taufile, this_ev->end_no, end_name, 0);

        no_copied++;
    }//while
    
    return no_copied;
}


//notes
// we could wrap the thread_func as in here....
//rc = tau_pthread_group_create(thread_pointer_array[k], &thread_attr, (void *) run_tau_version, 
//            (void*)threaddata[k], "trialgroup", number_of_threads_in_group);
//

