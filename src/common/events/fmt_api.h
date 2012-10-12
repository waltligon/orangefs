
#ifndef _FMT_API_H_
#define _FMT_API_H_

/* Some macros *
 ***************/
#define TP_MAX_THREADS 1024
#define TP_MAX_EVENTS 128
#define TP_MAX_STR 256
#define TAU_Log_get_event_number(THR) ((THR)->tau_no++)
#define TAUPVFS_MAX TP_MAX_THREADS


#include <TAU_tf_writer.h>
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



/* Process-level vars (safe to be globals - since process-level)
 * these names are far too generic to be externally visible - 
 * TODO: give them a prefix *
 ****************************/
extern int g_pid;
extern char g_traceloc[TP_MAX_STR];
extern char g_filepfx[TP_MAX_STR];
extern int g_defbufsz;

#endif

