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
 * A small library for generating and looking up fully opaque id's for
 * an arbitrary piece of data.
 */ 

#ifndef __ID_GENERATOR_H
#define __ID_GENERATOR_H 

#include <inttypes.h>

typedef int64_t id_gen_t;

/********************************************************************
 * Visible interface
 */

int id_gen_fast_register(id_gen_t* new_id, void* item);
void* id_gen_fast_lookup(id_gen_t id);

#endif /* __ID_GENERATOR_H */

