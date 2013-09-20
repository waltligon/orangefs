/*****************************************************
 * File    : fmt_fsm.h   
 * Version : $Id: fmt_fsm.h,v 1.2 2008-11-20 01:16:52 slang Exp $
 ****************************************************/

/* Author: Aroon Nataraj */

#ifndef _FMT_FSM_H
#define _FMT_FSM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdarg.h>

//per-format info (i.e. composed of patterns)
struct ff_format {

	//the states
	enum {FF_INIT=0xbeef, FF_PATN, FF_L, FF_D, FF_F, FF_C, FF_U, FF_S, FF_MAX}; //FF_S unused now

	//the struct to hold per-pattern info
	struct ff_pattern {
		char v_patn[8]; //the pattern itself
		int v_long; //flag that specifies how many longs have been encountered
		int v_size; //the sizeof stuff
		int v_unsigned; //is this unsigned? //shouldnt really matter until reader
		int next_state;
		int last_state;
		int this_state;
		int eop; // end-of-pattern flag
		int err; //signals error condition
		int type;

		void init(const ff_pattern& thecopy) {
			strncpy(v_patn, thecopy.v_patn, 8);
			v_long = thecopy.v_long;
			v_size = thecopy.v_size;
			v_unsigned = thecopy.v_unsigned;
			next_state = thecopy.next_state;
			last_state = thecopy.last_state;
			this_state = thecopy.this_state;
			eop = thecopy.eop;
			err = thecopy.err;
			type = thecopy.type;
		}

		void init() {
			strncpy(v_patn, "\0", 8);
			v_long = 0;
			v_size = 0;
			v_unsigned = 0;
			next_state = last_state = this_state = FF_INIT;
			eop = err = 0;
		}

		ff_pattern() {
			init();
		}

		ff_pattern(const ff_pattern& thecopy) {
			init(thecopy);
		}


		int parse(char* fmt, int *noread);

		int suck(va_list ap, char* space, int *nostored);

		int bfprint(FILE* fp, char* src, int src_sz, int *nowrote);

		template < class integralType > 
		int promoteIntegral(char* src, int src_sz, int *nowrote, integralType* retVal);
	};

	char v_fmt[256]; //the format itself
	char v_parsed_fmt[256]; //the format parsed from v_fmt
	ff_pattern patns[16]; //16 patterns
	int no_patns; // the no in above array
	int v_tot_size; //tot size in bytes req to read whole fmt
	int inited;

	ff_format() {
		init("");
	}

	ff_format(char* _fmt) {
		init(_fmt);
	}

	ff_format(const ff_format& o) {
		init(o);
	}

	void init(const ff_format& thecopy) {
		strncpy(v_fmt, thecopy.v_fmt, 256);
		strncpy(v_parsed_fmt, thecopy.v_parsed_fmt, 256);
		no_patns = thecopy.no_patns;
		v_tot_size = thecopy.v_tot_size;
		inited = thecopy.inited;
		int i = 0;
		for(i=0; i< no_patns; i++) {
			patns[i].init(thecopy.patns[i]);		
		}
	}

	void init(char* _fmt) {
		v_fmt[0] = '\0';
		v_parsed_fmt[0] = '\0';
		v_tot_size = no_patns = 0;

		if((!_fmt) || (!strcmp("", _fmt))) {
			inited = 0;
			return;
		}

		strncpy(v_fmt, _fmt, 256);

		inited = 1;
	}
	
	int parse();
	int suck(va_list ap, char* space, int* upto);
	int bfprint(FILE* fp, char* src, int src_sz, int* upto);
	template < class integralType > 
	int promoteIntegral(int patn_index, char* src, int src_sz, int *upto, integralType* retVal);
};


template <class integralType>
int ff_format::ff_pattern::promoteIntegral(char* src, int src_sz, int *nowrote, integralType* retVal) {
	*nowrote = v_size;

	switch(type) {
		case FF_U:
			switch(v_long) {
				case 0:
					*retVal = (integralType)*((unsigned int*)src);
					break;
				case 1:
					*retVal = (integralType)*((unsigned long int*)src);
					break;
				case 2:
					*retVal = (integralType)*((unsigned long long int*)src);
					break;
				default:
					//badness
					*nowrote = 0;
					return -1;
			}
			break;

		case FF_D:
			switch(v_long) {
				case 0:
					*retVal = (integralType)*((int*)src);
					break;
				case 1:
					*retVal = (integralType)*((long int*)src);
					break;
				case 2:
					*retVal = (integralType)*((long long int*)src);
					break;
				default:
					//badness
					*nowrote = 0;
					return -1;
			}

			break;

		case FF_F: /* not integralType - then dont do this */
			switch(v_long) {
				case 0:
				case 1:
				case 2:
				default:
					//badness
					*nowrote = 0;
					return -1;
			}

			break;

		case FF_C:
			*retVal = (integralType)*((char*)src);
			break;

		default:
			*nowrote  = 0;
			return -1;

	}

	return 0;
}

template <class integralType>
int ff_format::promoteIntegral(int patn_index, char* src, int src_sz, int *upto, integralType* retVal) {
	int i, totwrote = 0, nowrote = 0, err = 0;
	if(patn_index >= no_patns) {
		return -1;
	}

	nowrote = 0;
	err = patns[patn_index].promoteIntegral(src, src_sz, &nowrote, retVal);
	if(err != 0) {
		//badness - we got to get out ...
		if(upto) *upto = 0;
		return -1;
	} 

	if(upto) *upto = nowrote;

	return 0;
}


#endif //_FMT_FSM_H

/***************************************************************************
 * $RCSfile: fmt_fsm.h,v $   $Author: slang $
 * $Revision: 1.2 $   $Date: 2008-11-20 01:16:52 $
 ***************************************************************************/
