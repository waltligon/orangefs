/*****************************************************
 * File    : fmt_fsm.cpp   
 * Version : $Id: fmt_fsm.c,v 1.2 2008-11-20 01:16:52 slang Exp $
 ****************************************************/

/* Author: Aroon Nataraj */

#include "fmt_fsm.h"
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

int ff_format::ff_pattern::bfprint(FILE* fp, char* src, int src_sz, int *nowrote) {
	*nowrote = v_size;

	switch(type) {
		case FF_U:
			switch(v_long) {
				case 0:
					fprintf(fp, "%u ", *((unsigned int*)src));
					break;
				case 1:
					fprintf(fp, "%lu ", *((unsigned long int*)src));
					break;
				case 2:
					fprintf(fp, "%llu ", *((unsigned long long int*)src));
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
					fprintf(fp, "%d ", *((int*)src));
					break;
				case 1:
					fprintf(fp, "%ld ", *((long int*)src));
					break;
				case 2:
					fprintf(fp, "%lld ", *((long long int*)src));
					break;
				default:
					//badness
					*nowrote = 0;
					return -1;
			}

			break;

		case FF_F:
			switch(v_long) {
				case 0:
					fprintf(fp, "%f ", *((float*)src));
					break;
				case 1:
					fprintf(fp, "%lf ", *((double*)src));
					break;
				case 2:
					fprintf(fp, "%lf ", *((long double*)src));
					break;
				default:
					//badness
					*nowrote = 0;
					return -1;
			}

			break;

		case FF_C:
			fprintf(fp, "%c ", *((char*)src));
			break;

		default:
			*nowrote  = 0;
			return -1;

	}

	return 0;
}

int ff_format::ff_pattern::suck(va_list ap, char* space, int *nostored) {

	*nostored = v_size;

	switch(type) {
		case FF_U:
			switch(v_long) {
				case 0:
					*((unsigned int*)space) = va_arg(ap, unsigned int);
					break;
				case 1:
					*((unsigned long int*)space) = va_arg(ap, unsigned long int);
					break;
				case 2:
					*((unsigned long long int*)space) = va_arg(ap, unsigned long long int);
					break;
				default:
					//badness
					*nostored = 0;
					return -1;
			}
			break;

		case FF_D:
			switch(v_long) {
				case 0:
					*(( int*)space) = va_arg(ap,  int);
					break;
				case 1:
					*(( long int*)space) = va_arg(ap,  long int);
					break;
				case 2:
					*(( long long int*)space) = va_arg(ap,  long long int);
					break;
				default:
					//badness
					*nostored = 0;
					return -1;
			}

			break;

		case FF_F:
			switch(v_long) {
				case 0:
				{
					//compiler says....
					//warning: float is promoted to double when passed through ...
					//warning: (so you should pass double not a float to a va_args)
					//so we do as the compiler gods tell us to
					double d_tmp = va_arg(ap,  double);
					*(( float*)space) = (float)d_tmp;
				}
					break;
				case 1:
					*(( double*)space) = va_arg(ap,  double);
					break;
				case 2:
					*(( long double*)space) = va_arg(ap,  long double);
					break;
				default:
					//badness
					*nostored = 0;
					return -1;
			}

			break;

		case FF_C:
			{
				//compiler says....
				//warning: char is promoted to int when passed through ...
				//warning: (so you should pass int not a char to a va_args)
				//so we do as the compiler gods tell us to
				int i_tmp = va_arg(ap,  int);
				*((unsigned char*)space) = (unsigned char)i_tmp;
			}
			break;

		default:
			*nostored  = 0;
			return -1;

	}

	return 0;
}

int ff_format::ff_pattern::parse(char* fmt, int *noread) {

	int pos = 0, start_pos = 0;
	char *cur = fmt;
	
	init();

	while(1) {
		switch(next_state) {
		case FF_INIT:
			if(eop) {
				strncpy(v_patn, fmt + start_pos, pos - start_pos);
				v_patn[pos] = '\0';
				*noread=pos;
				return 0;
			}

			if(err) {
				err = 0;
				init();
				if(cur[pos] == ' ' || cur[pos] == '\t' || cur[pos] == '\n' || cur[pos] == '\r' || cur[pos] == '\0') {
					//whitespace ends this (broken)pattern
					*noread=pos;
					return -1;
					
				}
				break;
			}

			if(cur[pos] == '\0') {
				//term
				*noread=pos;
				return -1;
			}

			if(cur[pos++] == '%') {
				start_pos = pos-1;
				next_state = FF_PATN;
				break;
			}
			break;

		case FF_PATN:
			if(cur[pos] == 'l') {
				next_state = FF_L;
				pos++;
				break;
			} else if(cur[pos] == 'u') {
				next_state = FF_U;
				pos++;
				break;
			} else if(cur[pos] == 'd') {
				next_state = FF_D;
				pos++;
				break;
			} else if(cur[pos] == 'c') {
				next_state = FF_C;
				pos++;
				break;
			} else if(cur[pos] == 'f') {
				next_state = FF_F;
				pos++;
				break;
			} else {
				//some badness.. unrecognized pattern
				err = 1;
				next_state = FF_INIT;
				break;
			}
			break;

		case FF_U:
			//terminal...? yes , I think so
			v_unsigned += 1;
			if(v_unsigned > 1) {
				//badness
				err = 1;
				next_state = FF_INIT;
				break;
			}
			switch(v_long) {
				case 0:
					v_size = sizeof(unsigned int);
					break;
				case 1:
					v_size = sizeof(unsigned long int);
					break;
				case 2:
					v_size = sizeof(unsigned long long int);
					break;
				default:
					//badness
					break;
			}
			
			type = FF_U;

			eop = 1;
			next_state = FF_INIT;
			break;

		case FF_L:
			v_long += 1;
			if(v_long > 2) {
				//badness
				err = 1;
				next_state = FF_INIT;
				break;
			}

			//pos++; //dont incr here - already doing in FF_PATN
			next_state = FF_PATN;
			break;

		case FF_D:
			switch(v_long) {
				case 0:
					v_size = sizeof(int);
					break;
				case 1:
					v_size = sizeof(long int);
					break;
				case 2:
					v_size = sizeof(long long int);
					break;
				default:
					//badness
					break;
			}

			type = FF_D;

			//teminal state
			eop = 1;
			next_state = FF_INIT;
			break;

		case FF_F:
			switch(v_long) {
				case 0:
					v_size = sizeof(float);
					break;
				case 1:
					v_size = sizeof(double);
					break;
				case 2:
					v_size = sizeof(long double);
					break;
				default:
					//badness
					break;
			}

			type = FF_F;

			//teminal state
			eop = 1;
			next_state = FF_INIT;
			break;

			
			break;

		case FF_C:
			if((v_long > 0) || (v_unsigned > 0)) {
				err = 1;
				next_state = FF_INIT;
				break;
			}

			type = FF_C;

			v_size = sizeof(unsigned char);
			eop = 1;
			next_state = FF_INIT;
			break;

		default:
			//not good
			break;
		}//switch

	}//while
	
	return -1;
}

int ff_format::suck(va_list ap, char* space, int* upto) {
	int i, totread = 0, noread, err = 0;
	for(i = 0; i< no_patns; i++) {
		noread = 0;
		err = patns[i].suck(ap, space+totread, &noread);
		if(err != 0) {
			//badness - we got to get out ...
			if(upto) *upto = totread;
			return -1;
		} else {
			totread += noread;
		}
		
	}

	if(upto) *upto = totread;

	return 0;
}

int ff_format::parse() {
	int totread = 0, noread = 0, err = 0;

	while(v_fmt[totread] != '\0') {
		noread = 0;
		err = patns[no_patns].parse(v_fmt+totread, &noread);
		if(err != 0) {
			//badness
		} else {
			strncat(v_parsed_fmt, patns[no_patns].v_patn, 255);
			v_tot_size += patns[no_patns].v_size;
			no_patns++;
		}
		totread += noread;
	}
	
	return no_patns;
}


int ff_format::bfprint(FILE* fp, char* src, int src_sz, int* upto) {
	int i, totwrote = 0, nowrote = 0, err = 0;
	for(i = 0; (i< no_patns) && (totwrote <= src_sz); i++) {
		nowrote = 0;
		err = patns[i].bfprint(fp, src+totwrote, (src_sz-totwrote), &nowrote);
		if(err != 0) {
			//badness - we got to get out ...
			if(upto) *upto = totwrote;
			return -1;
		} else {
			totwrote += nowrote;
		}
	}

	if(upto) *upto = totwrote;

	return 0;
}


