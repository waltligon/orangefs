/* Modified May 2002 by Phil Carns for use with BMI GM method */

/*************************************************************************
 * Myricom MPICH-GM ch_gm backend                                        *
 * Copyright (c) 2001 by Myricom, Inc.                                   *
 * All rights reserved.                                                  *
 *************************************************************************/

#include <unistd.h>
#include <unistd.h>
#include <sys/mman.h>

#include <bmi-gm-regcache.h>

#if !defined(__linux__) && !defined(__APPLE__)
/* loic: at least on 64bits archs, it is important that sbrk
   has the right prototype sbrk altough quite UNIX universal
   is a non-official function, so might not be in headers
   if this definition conflicts with yours, remove the OS
   from the def condition
 */
void *sbrk();
#endif


void *bmi_gm_sbrk(int inc)
{
	if (inc < 0)
	{
		long oldp = (long)sbrk(0);
		bmi_gm_clear_interval((unsigned long)(oldp+inc), -inc);
	}
	return sbrk(inc);
}


int bmi_gm_munmap(void *start, size_t length)
{
  bmi_gm_clear_interval((unsigned long)start, length);
  return munmap(start, length);
}
