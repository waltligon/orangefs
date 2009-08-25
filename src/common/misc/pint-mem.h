/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_MEM_H
#define __PINT_MEM_H

extern void* PINT_mem_aligned_alloc(size_t size, size_t alignment);
extern void PINT_mem_aligned_free(void *ptr);

#endif /* __PINT_MEM_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */


