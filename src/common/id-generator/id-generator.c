/*
 * copyright (c) 2000 Clemson University, all rights reserved.
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
 * Contacts:  Phil Carns  pcarns@parl.clemson.edu
 */

/*
 * April 2001
 *
 * This will hopefully eventually be a library of mechanisms for doing
 * fast registration and lookups of data structures.  Right now it only
 * has function calls that will turn data structure pointers into opaque
 * 64 bit handles.  Should *not* be used in a user application, because
 * it is rather easy to break.
 */ 

#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <id-generator.h>

/* id_gen_fast_register()
 * 
 * registers a piece of data (usually a pointer of some sort) and
 * returns an opaque id for it.  
 *
 * returns 0 on success, -errno on failure
 */
int id_gen_fast_register(id_gen_t* new_id, void* item)
{

	if(!item)
	{
		return(-EINVAL);
	}

#if __WORDSIZE == 32
	
	*new_id = 0;

	*new_id += (int32_t)item;

#elif __WORDSIZE == 64

	*new_id = (int64_t)item;

#else

	ERROR; Unsupported Architecture...

	return(-ENOSYS);

#endif

	return(0);
}

/* id_gen_fast_lookup()
 * 
 * Returns the piece of data registered with an id.  It does no error
 * checking!  It does not make sure that that the id was actually
 * registered before proceeding.
 *
 * returns pointer to data on success, NULL on failure
 */
void* id_gen_fast_lookup(id_gen_t id)
{

	int32_t little_int = 0;

	if(!id)
	{
		return(NULL);
	}

#if __WORDSIZE == 32

	little_int += id;

	return((void*)little_int);

#elif __WORDSIZE == 64
	
	return((void*)id);

#else

	ERROR; Unsupported Architecture...

#endif

	return(NULL);
}


