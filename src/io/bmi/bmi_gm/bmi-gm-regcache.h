/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* GM memory registration cache; based on MPICH-GM device code */

#include<gossip.h>

#include<gm.h>

#ifndef __BMI_GM_REGCACHE_H
#define __BMI_GM_REGCACHE_H

int bmi_gm_regcache_init(struct gm_port* current_port);
void bmi_gm_regcache_finalize(void);
void bmi_gm_regcache_deregister(void* addr, unsigned int pages);
void bmi_gm_regcache_garbage_collector(unsigned int required);
int bmi_gm_regcache_register(void * addr, unsigned int pages);
unsigned long bmi_gm_use_interval(unsigned long start, unsigned int
	length);
void bmi_gm_unuse_interval(unsigned long start, unsigned int length);
void bmi_gm_clear_interval(unsigned long start, unsigned int length);

#endif /* __BMI_GM_REGCACHE_H */
