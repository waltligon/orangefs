/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#ifndef __PINT_MEM_H
#define __PINT_MEM_H

/* struct that describes the underlying memory region that we pick our
 * aligned region from
 */
struct PINT_mem_desc
{
    void* addr;
    int size;
};

/* PINT_mem_aligned_alloc()
 *
 * allocates a memory region of the specified size and returns a 
 * pointer to the region.  The address of the memory will be evenly
 * divisible by alignment.
 *
 * returns pointer to memory on success, NULL on failure
 */
static inline void* PINT_mem_aligned_alloc(int size, unsigned long alignment)
{
    char* true_ptr = NULL;
    char* returned_ptr = NULL;
    struct PINT_mem_desc* desc = NULL;
    int full_size = size + alignment + sizeof(struct PINT_mem_desc);
    unsigned long alignment_mask = (~(alignment-1));

    true_ptr = (char*)malloc(full_size);
    if(!true_ptr)
    {
	return(NULL);
    }

    /* we need to find the first aligned address within the malloc'd
     * area that leaves room for a descriptor structure
     */
    returned_ptr = 
	(char*)(((unsigned long)(true_ptr)
	+ alignment + sizeof(struct PINT_mem_desc) - 1)&alignment_mask);

    /* index backwards and fill in a descriptor that tells us what
     * region really needs to be free'd when the time comes
     */
    desc = (struct PINT_mem_desc*)(returned_ptr - sizeof(struct
	PINT_mem_desc));
    desc->addr = true_ptr;
    desc->size = full_size;
    
    return(returned_ptr);
}

/* PINT_mem_aligned_free()
 *
 * frees memory region previously allocated with
 * PINT_mem_aligned_alloc()
 *
 * no return value
 */
static inline void PINT_mem_aligned_free(void* ptr)
{
    struct PINT_mem_desc* desc = NULL;

    /* backup a little off of the pointer and find the descriptor of the 
     * underlying memory region 
     */
    desc = (struct PINT_mem_desc*)((char*)ptr - sizeof(struct PINT_mem_desc));

    free(desc->addr);

    return;
}
#endif /* __PINT_MEM_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */


