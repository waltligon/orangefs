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

#include<gen-locks.h>
#include<stdio.h>

/* gen_locks can be declared and initialized at the same time */
static gen_mutex_t test_mutex = GEN_MUTEX_INITIALIZER;
static int foo_function(void);


int main(int argc, char **argv)	{

	gen_mutex_lock(&test_mutex);
		/* Access critical region here */
		printf("Critical region 1.\n");
	gen_mutex_unlock(&test_mutex);

	foo_function();

	return(0);
}


/* this function shows how mutex's can be dynamically allocated and
 * destroyed */
static int foo_function(void){

	gen_mutex_t* foo_mutex;

	foo_mutex = gen_mutex_build();
	if(!foo_mutex){
		return(-1);
	}

	gen_mutex_lock(foo_mutex);
		/* Access critical region */
		printf("Critical region 2.\n");
	gen_mutex_unlock(foo_mutex);

	gen_mutex_destroy(foo_mutex);

	return(0);
}
