/*
 * copyright (c) 2001 Clemson University, all rights reserved.
 *
 * Written by Phil Carns.
 *
 * This program is free software; you can redistribute it and/or
 * modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Contact:  Phil Carns  pcarns@parl.clemson.edu
 */


/* this is a header for a customizable set of locking primitives.  They can
 * be turned on or off through compile time defines.  Application
 * programmers should use the following interface for portability rather
 * than directly calling specific lock implementations:
 *
 * int gen_mutex_lock(gen_mutex_t* mut);
 * int gen_mutex_unlock(gen_mutex_t* mut);
 * int gen_mutex_trylock(gen_mutex_t* mut);
 * gen_mutex_t* gen_mutex_build(void);
 * int gen_mutex_destroy(gen_mutex_t* mut); 
 *
 * See the examples directory for details.
 */

#ifndef __GEN_LOCKS_H
#define __GEN_LOCKS_H

#include <stdlib.h>

/* we will make posix locks the default unless turned off for now. */
/* this is especially important for development in which case the locks
 * should really be enabled in order to verify proper operation 
 */
#if !defined(__GEN_NULL_LOCKING__) && !defined(__GEN_POSIX_LOCKING__) 
	#define __GEN_POSIX_LOCKING__
#endif

#ifdef __GEN_POSIX_LOCKING__
	#include <pthread.h>

	/* function prototypes for specific locking implementations*/
	int gen_posix_mutex_lock(pthread_mutex_t* mut);
	int gen_posix_mutex_unlock(pthread_mutex_t* mut);
	int gen_posix_mutex_trylock(pthread_mutex_t* mut);
	pthread_mutex_t* gen_posix_mutex_build(void);
	int gen_posix_mutex_destroy(pthread_mutex_t* mut);
	int gen_posix_mutex_init(pthread_mutex_t* mut);

	typedef pthread_mutex_t gen_mutex_t;
	#define GEN_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER;
	#define gen_mutex_lock(m) gen_posix_mutex_lock(m)
	#define gen_mutex_unlock(m) gen_posix_mutex_unlock(m)
	#define gen_mutex_trylock(m) gen_posix_mutex_trylock(m)
	#define gen_mutex_build() gen_posix_mutex_build()
	#define gen_mutex_destroy(m) gen_posix_mutex_destroy(m)
	#define gen_mutex_init(m) gen_posix_mutex_init(m)
#endif /* __GEN_POSIX_LOCKING__ */


#ifdef __GEN_NULL_LOCKING__
	/* this stuff messes around just enough to prevent warnings */
	typedef int gen_mutex_t;
	#define GEN_MUTEX_INITIALIZER 0
        static inline int gen_mutex_lock(gen_mutex_t *mutex_p) { return 0; }
        static inline int gen_mutex_unlock(gen_mutex_t *mutex_p) { return 0; }
        static inline int gen_mutex_trylock(gen_mutex_t *mutex_p) { return 0; }
        static inline gen_mutex_t *gen_mutex_build(void) { return (int*) malloc(sizeof(int)); }
	#define gen_mutex_init(m) do{}while(0)
	#define gen_mutex_destroy(m) free(m)
#endif /* __GEN_NULL_LOCKING__ */

#endif /* __GEN_LOCKS_H */
