/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* GM memory registration cache; based on MPICH-GM device code Copyright
 * (c) 2001 by Myricom, Inc.
 */

#include <stdlib.h>
#include <errno.h>

#include "gossip.h"
#include <pvfs-debug.h>

#include <gm.h>

typedef struct _entry
{
    unsigned long addr;
    struct _entry *prev;
    struct _entry *next;
    unsigned int refcount;
}
regcache_entry;

static struct gm_hash *regcache_hash = NULL;
static struct gm_lookaside *regcache_lookaside = NULL;
static regcache_entry *regcache_head = NULL;
static regcache_entry *regcache_tail = NULL;
static struct gm_port *local_port = NULL;

/* TODO: come back and make as many of these static as possible 
 * TODO: add something to track how much memory we are using over time
 * TODO: comment better 
 */

/* bmi_gm_regcache_init()
 *
 * initializes the GM memory registration cache
 *
 * returns 0 on success, -errno on failure
 */
int bmi_gm_regcache_init(struct gm_port *current_port)
{
    /* initialize hash table and lookaside list */
    if ((regcache_hash = gm_create_hash(gm_hash_compare_ptrs,
					gm_hash_hash_ptr, 0, 0, 4096,
					0)) == NULL)
    {
	return (-ENOMEM);
    }

    if ((regcache_lookaside = gm_create_lookaside(sizeof(regcache_entry),
						  4096)) == NULL)
    {
	gm_destroy_hash(regcache_hash);
	return (-ENOMEM);
    }

    local_port = current_port;

    return (0);
}

/* bmi_gm_regcache_finalize()
 *
 * shuts down the registration cache and releases any memory and
 * resources
 *
 * no return value
 */
void bmi_gm_regcache_finalize(void)
{
    /* TODO: probably should deregister all regions first */

    if (regcache_hash)
    {
	gm_destroy_hash(regcache_hash);
    }
    if (regcache_lookaside)
    {
	gm_destroy_lookaside(regcache_lookaside);
    }

    local_port = NULL;

    return;
}

/* bmi_gm_regcache_deregister()
 *
 * deregisters a set of pages that were previously registered
 *
 * no return value
 */
void bmi_gm_regcache_deregister(void *addr,
				unsigned int pages)
{
    if (pages > 0)
    {
	gm_deregister_memory(local_port, addr, GM_PAGE_LEN * pages);
	gossip_ldebug(BMI_DEBUG_GM_MEM,
		      "Regcache: deregistering %d pages at %p.\n", (int) pages,
		      addr);
    }
}

/* bmi_gm_regcache_garbage_collector()
 *
 * garbage collection for the regcache
 *
 * no return value
 */
void bmi_gm_regcache_garbage_collector(unsigned int required)
{
    regcache_entry *entry_ptr, *next_entry;
    unsigned int count = 0;
    unsigned long batch_addr = 0;
    unsigned int batch_pages = 0;

    gossip_ldebug(BMI_DEBUG_GM_MEM,
		  "Regcache: garbage collector start: required: %d.\n",
		  required);

    entry_ptr = regcache_head;
    while ((count < required) && (entry_ptr != NULL))
    {
	if (entry_ptr->refcount == 0)
	{
	    gm_hash_remove(regcache_hash, (void *) entry_ptr->addr);
	    if (batch_addr == 0)
	    {
		batch_addr = entry_ptr->addr;
		batch_pages++;
	    }
	    else
	    {
		if (entry_ptr->addr == batch_addr + batch_pages * GM_PAGE_LEN)
		{
		    batch_pages++;
		}
		else
		{
		    bmi_gm_regcache_deregister((void *) batch_addr,
					       batch_pages);
		    batch_addr = entry_ptr->addr;
		    batch_pages = 1;
		}
	    }

	    count++;
	    next_entry = entry_ptr->next;

	    if (regcache_head == entry_ptr)
	    {
		regcache_head = next_entry;
	    }
	    else
	    {
		entry_ptr->prev->next = next_entry;
	    }

	    if (regcache_tail == entry_ptr)
	    {
		regcache_tail = entry_ptr->prev;
	    }
	    else
	    {
		entry_ptr->next->prev = entry_ptr->prev;
	    }

	    gm_lookaside_free(entry_ptr);
	    entry_ptr = next_entry;
	}
	else
	{
	    entry_ptr = entry_ptr->next;
	}
    }

    if (batch_addr)
    {
	bmi_gm_regcache_deregister((void *) batch_addr, batch_pages);
    }
    gossip_ldebug(BMI_DEBUG_GM_MEM,
		  "Regcache: garbage collector stop: required: %d, count: %d.\n",
		  required, (int) count);

    return;
}


/* bmi_gm_regcache_register()
 *
 * registers a region of memory and adds it to the regcache
 *
 * returns 0 on success, -errno on failure
 */
int bmi_gm_regcache_register(void *addr,
			     unsigned int pages)
{
    unsigned int i;
    regcache_entry *entry_ptr;
    gm_status_t status;

    gossip_ldebug(BMI_DEBUG_GM_MEM,
		  "Regcache: register addr: %p, len: %d.\n",
		  addr, (int) (pages * GM_PAGE_LEN));

    if (gm_register_memory(local_port, addr, GM_PAGE_LEN * pages) != GM_SUCCESS)
    {
	gossip_ldebug(BMI_DEBUG_GM_MEM, "Regcache: using garbage collector.\n");

	bmi_gm_regcache_garbage_collector(4096);
	if (gm_register_memory(local_port, addr, GM_PAGE_LEN * pages)
	    != GM_SUCCESS)
	{
	    gossip_ldebug(BMI_DEBUG_GM_MEM,
			  "Regcache: registor_memory failed; addr: %p, length: %d.\n",
			  addr, (int) (GM_PAGE_LEN * pages));
	    return (-ENOMEM);
	}
    }

    for (i = 0; i < pages; i++)
    {
	entry_ptr = (regcache_entry *) gm_lookaside_alloc(regcache_lookaside);
	if (!entry_ptr)
	{
	    /* TODO cleanup properly */
	    return (-ENOMEM);
	}

	if (regcache_head == NULL)
	{
	    regcache_head = entry_ptr;
	}
	else
	{
	    regcache_tail->next = entry_ptr;
	}

	entry_ptr->prev = regcache_tail;
	entry_ptr->next = NULL;
	regcache_tail = entry_ptr;
	entry_ptr->refcount = 1;
	entry_ptr->addr = (unsigned long) addr + i * GM_PAGE_LEN;

	status = gm_hash_insert(regcache_hash, (void *) (entry_ptr->addr),
				(void *) (entry_ptr));
	if (status != GM_SUCCESS)
	{
	    /* TODO: cleanup properly */
	    return (-ENOMEM);
	}
    }

    return (0);
}


/* bmi_gm_use_interval()
 *
 * registers a region of memory (if it is not already) and adds it to
 * the regcache
 *
 * returns length of region that it was actually able to register
 * (assume error or out of memory if < length)
 */
unsigned long bmi_gm_use_interval(unsigned long start,
				  unsigned int length)
{
    unsigned long addr, end, batch_addr;
    unsigned int batch_pages;
    regcache_entry *entry_ptr;

    gossip_ldebug(BMI_DEBUG_GM_MEM,
		  "Regcache: use_interval: start: %lu, length: %u.\n", start,
		  length);

    if (length == 0)
    {
	return (0);
    }

    addr = start & ~(GM_PAGE_LEN - 1);
    end = start + length;
    batch_addr = 0;
    batch_pages = 0;

    while (addr < end)
    {
	entry_ptr =
	    (regcache_entry *) gm_hash_find(regcache_hash, (void *) addr);
	if (entry_ptr == NULL)
	{
	    if (batch_addr == 0)
	    {
		batch_addr = addr;
	    }
	    batch_pages++;
	}
	else
	{
	    entry_ptr->refcount++;
	    if (batch_addr != 0)
	    {
		gossip_ldebug(BMI_DEBUG_GM_MEM,
			      "Regcache: use_interval batch, batch_addr: %lu, batch_pages: %u.\n",
			      batch_addr, batch_pages);
		if (bmi_gm_regcache_register((void *) batch_addr, batch_pages)
		    != 0)
		{
		    entry_ptr->refcount--;

		    if (batch_addr > start)
		    {
			return (batch_addr - start);
		    }
		    else
		    {
			return 0;
		    }
		}

		batch_addr = 0;
		batch_pages = 0;

		/* move the entry to the end of the list (LRU policy) */
		if (entry_ptr != regcache_tail)
		{
		    if (entry_ptr == regcache_head)
		    {
			entry_ptr->next->prev = NULL;
			regcache_head = entry_ptr->next;
		    }
		    else
		    {
			entry_ptr->prev->next = entry_ptr->next;
			entry_ptr->next->prev = entry_ptr->prev;
		    }

		    entry_ptr->next = NULL;
		    entry_ptr->prev = regcache_tail;
		    regcache_tail->next = entry_ptr;
		    regcache_tail = entry_ptr;
		}
	    }
	}
	addr += GM_PAGE_LEN;
    }

    if (batch_addr != 0)
    {
	if (bmi_gm_regcache_register((void *) batch_addr, batch_pages) != 0)
	{
	    if (batch_addr > start)
	    {
		return (batch_addr - start);
	    }
	    else
	    {
		return 0;
	    }
	}
    }

    return length;
}


/* bmi_gm_unuse_interval()
 *
 * indicates that a memory region is no longer needed.  the regcache may
 * choose to deregister it if no one else is using it
 *
 * no return value
 */
void bmi_gm_unuse_interval(unsigned long start,
			   unsigned int length)
{
    unsigned long addr, end;
    regcache_entry *entry_ptr;

    gossip_ldebug(BMI_DEBUG_GM_MEM,
		  "Regcache: unuse_interval, start: %lu, length: %u.\n",
		  start, length);

    if (length == 0)
    {
	return;
    }

    addr = start & ~(GM_PAGE_LEN - 1);
    end = start + length;

    while (addr < end)
    {
	entry_ptr =
	    (regcache_entry *) gm_hash_find(regcache_hash, (void *) addr);
	entry_ptr->refcount--;
	addr += GM_PAGE_LEN;
    }

    return;
}


/* bmi_gm_clear_interval()
 *
 * Immediately deregisters an interval that has been pinned by GM
 *
 * no return value
 */
void bmi_gm_clear_interval(unsigned long start,
			   unsigned int length)
{
    unsigned long addr, end, batch_addr;
    unsigned int batch_pages;
    regcache_entry *entry_ptr;

    if (regcache_hash != NULL)
    {
	addr = start & ~(GM_PAGE_LEN - 1);
	end = start + length;
	batch_addr = 0;
	batch_pages = 0;

	while (addr < end)
	{
	    entry_ptr = (regcache_entry *) gm_hash_find(regcache_hash,
							(void *) addr);
	    if (entry_ptr != NULL)
	    {
		gm_hash_remove(regcache_hash, (void *) addr);

		if (batch_addr == 0)
		    batch_addr = addr;
		batch_pages++;

		if (regcache_head == entry_ptr)
		    regcache_head = entry_ptr->next;
		else
		    entry_ptr->prev->next = entry_ptr->next;

		if (regcache_tail == entry_ptr)
		    regcache_tail = entry_ptr->prev;
		else
		    entry_ptr->next->prev = entry_ptr->prev;

		gm_lookaside_free(entry_ptr);
	    }
	    else
	    {
		if (batch_addr != 0)
		{
		    bmi_gm_regcache_deregister((void *) batch_addr,
					       batch_pages);
		    batch_addr = 0;
		    batch_pages = 0;
		}
	    }
	    addr += GM_PAGE_LEN;
	}

	if (batch_addr != 0)
	    bmi_gm_regcache_deregister((void *) batch_addr, batch_pages);
    }
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sw=4 noexpandtab
 */
