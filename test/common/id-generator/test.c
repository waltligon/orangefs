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

#include <stdio.h>
#include <stdlib.h>
#include <id-generator.h>

/* This code is a pretty pointless example.  It just generates a handle
 * for a structure and then uses it elsewhere.  The concept isn't
 * particularly useful unless you wish to hide the data type that you
 * are actually representing with the handle.
 */

typedef PVFS_id_gen_t example_handle;

struct example_struct{
	int x;
	int y;
	int z;
};

int print_example(example_handle my_handle);

int main(int argc, char **argv)	{

	example_handle my_handle;
	struct example_struct my_example;

	my_example.x = 1;
	my_example.y = 2;
	my_example.z = 3;
	
	id_gen_fast_register(&my_handle, &my_example);
	if(my_handle == 0)
	{
		printf("register failed.\n");
		exit(0);
	}

	print_example(my_handle);

	return(0);
}


int print_example(example_handle my_handle){

	struct example_struct* foo;

	foo = id_gen_fast_lookup(my_handle);
	if(!foo)
	{
		printf("lookup failed.\n");
		exit(0);
	}

	printf("%d, %d, %d.\n", foo->x, foo->y, foo->z);
	
	return(0);
}
