/*
 * (C) Kyle Schochenmaier 2007
 * Test API for 2-dimensional dist for pvfs2 functionality
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define __PINT_REQPROTO_ENCODE_FUNCS_C
#include "pvfs2-encode-stubs.h"
#include "pint-distribution.h"
#include "pvfs2-types.h"
#include "pvfs2-dist-twod-stripe.h"
#include "pvfs2-util.h"
#include "gossip.h"
#include "pint-dist-utils.h"

#include <assert.h>

#define DEBUG 0
#define ASSERT 1

/*
 * If server_ct and num_groups dont play nicely, we will have one (the last) 
 * set of servers that will contain anywhere between <n+1...n*2-1> servers.
 * Since we base the dist on the number of servers, we will have to provide
 * more space on the last set than the other sets to keep load balanced.
 */


static PVFS_offset logical_to_physical_offset ( void* params,
						PINT_request_file_data* fd,
						PVFS_offset logical_offset)
{
    if(logical_offset==0)
	return 0;
    
    PVFS_offset ret_offset = 0;	    /* ret value */
 
    uint32_t server_nr = fd->server_nr;
    uint32_t server_ct = fd->server_ct;
    uint32_t group_nr = 0;	    /* group server_nr is in */
    uint32_t servers_in_group = 0;  /* # servers in group_nr */
    uint32_t gserver_nr = 0;	    /* server_nr relative to group */

    /* param related variables */
    PVFS_twod_stripe_params* dparam = (PVFS_twod_stripe_params*)params;
    uint32_t factor = dparam->group_strip_factor;
    uint32_t num_groups = dparam->num_groups;
    PVFS_size strip_size = dparam->strip_size;

    /* size of all groups that are of equal size: all groups
     * except when server_ct doesnt divide evenly into num_groups */
    uint32_t small_group_size = server_ct / num_groups;
    
    /* global variables, which describe the entire dist reg. of groups */
    PVFS_size global_stripe_size = server_ct * strip_size * factor;
    PVFS_size global_stripes = 0;
    PVFS_size group_stripes = 0;
    PVFS_size group_strips = 0;
    PVFS_offset last_stripe_offset = 0;	/* offset into a stripe */
    PVFS_offset group_offset = 0;   /* offset into groups */
    PVFS_offset local_offset = 0;   /* offset inside a group */
    
    /* for future use, determine the layout of the system */
    /* group that server_nr is in: */
    if(server_nr >= (num_groups-1)*(small_group_size))
	group_nr = num_groups-1;    /* in last group */
    else
	group_nr = server_nr/small_group_size;  /* generic size */

    if(group_nr == num_groups-1)
	servers_in_group = server_ct - (num_groups-1)*(small_group_size);
    else
	servers_in_group = small_group_size;

    /* calculate the number of full stripes across all groups */
    global_stripes = logical_offset / global_stripe_size;

    /* calculate offset into the last global stripe (first not full stripe) */
    last_stripe_offset = logical_offset - global_stripes * global_stripe_size;

    /* get all of the blocks already accounted for via global stripes */
    ret_offset += global_stripes * factor * strip_size;

    /* find out if logical offset falls inside or before this group_nr */

    /* is the offset at least in this group?
     * if so, we need to add more to the ret_offset */
    group_offset = last_stripe_offset -
	 group_nr * factor * strip_size * small_group_size;

    local_offset = group_offset - 
	( servers_in_group * factor * strip_size );

    gserver_nr = server_nr - group_nr * small_group_size; 

    /* is the offset in this group? */
    if(group_offset > 0)
    {
	if(local_offset > 0)	
	{   /* no, l_off is in a group > group_nr, get as close as possible */
	    ret_offset += factor * strip_size;
	    return ret_offset;
	}
	else
	{   /* l_off is in this group, calculate the offset now */
	    /* find out if l_off ends on this server */
	    group_stripes = group_offset / (servers_in_group * strip_size );
	    /* add the stripes to the offset calc */
	    ret_offset += group_stripes * strip_size;
	    /* get rid of full group stripes */
	    group_offset -= group_stripes * strip_size * servers_in_group;
	    group_strips = group_offset / strip_size;
	    /* if the # of strips >= server_nr+1, this server strip is full */
	    if(group_strips >= gserver_nr+1)
		ret_offset+=strip_size;
	    else
		if(group_strips == gserver_nr)	/* not on this server */
		    ret_offset += group_offset % strip_size;
	}
    }
        /* else, l_off is in a server in group < group_nr */
	/* return the end of the last global stripe then */

#if DEBUG
    fprintf(stdout,"%s: server_nr: %d l_off: %d p_off: %d\n",__func__,fd->server_nr,logical_offset,ret_offset);
#endif
    return ret_offset;
}

/* 
 * For any block (block_nr) in a given group (group_nr) on any
 * server (server_nr), the following will always remain true:
 * 1.  All blocks in the same group and same strip with server<server_nr
 *     will always be full.
 * 2.  All blocks in all groups<group_nr in the same global_stripe (gstripe_nr)
 *     are full.
 * 3.  All blocks in all global_stripes < gstripe_nr are full.
 */
static PVFS_offset physical_to_logical_offset (void* params,
                                               PINT_request_file_data* fd,
                                               PVFS_offset physical_offset)
{
    PVFS_offset ret_offset = 0;
    PVFS_twod_stripe_params* dparam = (PVFS_twod_stripe_params*)params;
    uint32_t server_nr = fd->server_nr;
    uint32_t server_ct = fd->server_ct;
    uint32_t group_nr = 0;
    PVFS_size strip_size = dparam->strip_size;
    PVFS_size strips = physical_offset / strip_size;
    uint32_t servers_in_group = 0;
    /* determine layout of system */
    uint32_t small_group_size = server_ct / dparam->num_groups;
    uint32_t gserver_nr = 0;
    uint32_t factor = dparam->group_strip_factor;
    PVFS_size full_group_strips = strips % factor;
    PVFS_size global_stripes = 0;
    uint32_t num_groups = dparam->num_groups;

    /* if we are a server in the last group, make sure things are happy */
    if(server_nr >= (num_groups-1)*(small_group_size))
	group_nr = num_groups-1;
    else
	group_nr = server_nr/small_group_size;

    /* if we're in the last group, make sure we have the correct size! */
    if(group_nr == num_groups-1)
	servers_in_group = server_ct - (num_groups-1)*(small_group_size);
    else
	servers_in_group = small_group_size;

    /* find the server_nr relative to the group it is in */
    gserver_nr = server_nr - group_nr * small_group_size;
    
    /* 
     * if(#strips >= factor) we have at least one full stripe
     * else nothing more is filled
     */
    if(strips>factor)
    {
	global_stripes = strips/factor;
	ret_offset+=global_stripes * factor * server_ct * strip_size;
    }
    else
	if(strips==factor)  /* we're perfectly aligned with a strip */
	    ret_offset+=strips * server_ct * strip_size;

    /* Add all the servers in groups lower than current group */
    ret_offset += group_nr * small_group_size * factor * strip_size;

    /* Add all group strips */
    ret_offset += full_group_strips * servers_in_group * strip_size;

    /* Add all servers in group in this strip */
    ret_offset += gserver_nr * strip_size;

    /* Add the final portion of the physical strip */
    ret_offset += physical_offset % strip_size;

#if DEBUG
    fprintf(stdout,"%s: server_nr: %d p_off: %d l_off: %d\n",__func__,fd->server_nr,physical_offset, ret_offset);
#endif
    return ret_offset;

}






static PVFS_size logical_file_size(void* params,
                                   uint32_t server_ct,
                                   PVFS_size *psizes)
{
    PVFS_size cur_max = 0;
    int i = 0;
    /* generic fd request so we can cheat and use p_to_l */
#if 0
    PINT_request_file_data fd = {
				    0,	    /* file size */
				    i,	    /* server_nr */
				    server_ct, 
				    NULL,   /* dist, we dont use anyways */
				    0 };    /* extend_flag */

    for(i=0;i<server_ct;i++)
    {
	fd.server_nr = i;
	if(psizes[i])
	{
	    tmp_max = physical_to_logical_offset(params,&fd,(PVFS_offset)psizes[i]);
	    if(tmp_max > cur_max)
		cur_max = tmp_max;
	}
    }

#endif

    if(!psizes)
	return -1;
    for(i=0;i<server_ct;i++)
    {
	if(psizes[i])
	    cur_max+=psizes[i];
    }


#if DEBUG
    fprintf(stdout,"%s: server_ct: %d log_size: %d\n",__func__,server_ct,cur_max);
#endif
	    
    return cur_max;
}


/* Given a logical offset, return the next offset which is directly
 * mapped to a physical address on this server.
 *
 * NOTE:
 * For all server's that are in a group before server_nr, we will
 * find the physical address of the last strip, add one, and then
 * return the physical_to_logical() mapping to it.
 */

static PVFS_offset next_mapped_offset (void* params,
                                       PINT_request_file_data* fd,
                                       PVFS_offset logical_offset)
{
#if DEBUG
    fprintf(stdout,"%s: fsize: %d l_off: %d server_nr: %d server_ct: %d\n\t",__func__,fd->fsize,logical_offset,fd->server_nr,fd->server_ct);
#endif

    if(logical_offset == 0)
    {
	PVFS_offset ret2 = physical_to_logical_offset(params,fd,0);
#if DEBUG
    fprintf(stdout,"\t%s: n_m_o: %d\n",__func__,ret2);
#endif
	return ret2;

    }
    PVFS_twod_stripe_params* dparam = (PVFS_twod_stripe_params*)params;
    uint32_t server_nr = fd->server_nr;
    uint32_t server_ct = fd->server_ct;
    PVFS_size strip_size = dparam->strip_size;
    uint32_t num_groups = dparam->num_groups;
    uint32_t small_group_size = server_ct / num_groups;
    uint32_t factor = dparam->group_strip_factor;
    uint32_t group_nr = 0;
    uint32_t gserver_nr = 0;
    PVFS_size global_stripe_size = server_ct * factor * strip_size;
    uint32_t global_stripes = logical_offset / global_stripe_size;
    PVFS_size small_group_stripe = small_group_size * factor * strip_size;
    PVFS_size cur_group_size = 0;

    uint32_t servers_in_group = 0;
    PVFS_offset l_off = logical_offset - global_stripe_size * global_stripes;
    PVFS_offset p_off = 0;
    PVFS_offset g_off = 0;
    uint32_t g_strips = 0;
    
    PVFS_offset result = 0;
    /* we'll find the server where teh logical offset should be, then
     * use that to compute the address offsets for everyone else.
     */
    uint32_t total_stripes = 0;
    total_stripes += global_stripes * factor;

    /* if we are a server in the last group, make sure things are happy */
    if(server_nr >= (num_groups-1)*(small_group_size))
	group_nr = num_groups-1;
    else
	group_nr = server_nr/small_group_size;
    if(group_nr == num_groups-1)
	servers_in_group = server_ct - (num_groups-1)*(small_group_size);
    else
	servers_in_group = small_group_size;
    gserver_nr = server_nr - group_nr * small_group_size;

    cur_group_size = servers_in_group * factor * strip_size;

    /* Find the server where the l_off should be: */
    
    /* if the logical offset ends on a group > group_nr, 
     * we are guaranteed to have filled this stripe, add a stripe
     * and calculate the logical offset based on the physical off
     */
    /* is server_nr in a group < l_off's group? */
    if(l_off >= (group_nr)*small_group_stripe+cur_group_size)
    {
	total_stripes += factor;
	p_off += total_stripes * strip_size;
	return physical_to_logical_offset(params,fd,p_off);
	result=physical_to_logical_offset(params,fd,p_off);
//	result_t = logical_to_physical_offset(params,fd,logical_offset);
//	result = physical_to_logical_offset(params,fd,result_t);/*+1*/
    }
    else    /* check to see if we're already passed the l_off */
    {
	if((l_off < (group_nr)*small_group_stripe+cur_group_size) &&
		(l_off > group_nr*small_group_stripe))
	{
	    /* logical_offset ends on this group, find the server! */
	    g_off = l_off - group_nr*small_group_stripe;
	    g_strips = g_off / (servers_in_group * strip_size);
	    total_stripes += g_strips;
   	    g_off -= g_strips * strip_size * servers_in_group;	    
	    /* does logical_offset land on a server after server_nr? */
	    if(g_off < gserver_nr*strip_size)
	    {
		p_off += (total_stripes) * strip_size;
		result = physical_to_logical_offset(params,fd,p_off);
//		result_t = logical_to_physical_offset(params,fd,logical_offset);
//		result = physical_to_logical_offset(params,fd,result_t);/*+1*/
//		return physical_to_logical_offset(params,fd,p_off);
	    }
	    else    /* is l_off on a larger# server? */
		if( g_off >= (gserver_nr+1) * strip_size)
		{
//		    result_t = logical_to_physical_offset(params,fd,logical_offset);
//		    result = physical_to_logical_offset(params,fd,result_t); /*+1*/

		    p_off += (total_stripes+1) * strip_size;
		    result = physical_to_logical_offset(params,fd,p_off);
		    return physical_to_logical_offset(params,fd,p_off);
		}
		else
		{   /* We're on the correct server! */
		    result = logical_offset;
//		    return logical_offset;
		}
	}
	else
	{   
	    /* We're beyond the logical offset, dont add anything */
//	    result_t = logical_to_physical_offset(params,fd,logical_offset);
//	    result = physical_to_logical_offset(params,fd,result_t);

	    p_off += total_stripes * strip_size;
	    result = physical_to_logical_offset(params,fd,p_off);
	    return physical_to_logical_offset(params,fd,p_off);
	}
    }

#if DEBUG
    fprintf(stdout,"\t%s: n_m_o: %d\n",__func__,result);
#endif
    return result;
}



/*
 * Given a logical offset, find the logical offset greater than or equal
 * to the logical offset that maps to a physical offset on the current
 * PVFS server.
 * Returns a logical offset.
 */
/*
 * Does L_off map directly to a physical offset on this server?
 *  Yes:    return l_off
 *  No:
 *	After all full_stripes, is l_off before this server?
 *	 Yes: return last full strips' offset
 *	 No:  return the next strip's offset
 *		if l_off is in this group stripe, otherwise
 *		add a full global stripe
 */

#if 0
PVFS_offset next_mapped_offset2 (void* params,
                                       PINT_request_file_data* fd,
                                       PVFS_offset logical_offset)
{
    PVFS_offset server_offset = 0;
    PVFS_twod_stripe_params* dparam = (PVFS_twod_stripe_params*)params;
    uint32_t server_nr = fd->server_nr;
    uint32_t server_ct = fd->server_ct;
    PVFS_size strip_size = dparam->strip_size;
    uint32_t num_groups = dparam->num_groups;
    uint32_t small_group_size = server_ct / num_groups;
    uint32_t factor = dparam->group_strip_factor;

    uint32_t group_nr = 0;
    uint32_t gserver_nr = 0;
    PVFS_size global_stripe_size = server_ct * factor * strip_size;
    uint32_t global_stripes = logical_offset / global_stripe_size;
    PVFS_size small_group_stripe = small_group_size * factor * strip_size;
    
    uint32_t group_stripes = 0;
    PVFS_offset g_off = 0;
    uint32_t servers_in_group = 0;

    /* if we are a server in the last group, make sure things are happy */
    if(server_nr >= (num_groups-1)*(small_group_size))
	group_nr = num_groups-1;
    else
	group_nr = server_nr/small_group_size;

    if(group_nr == num_groups-1)
	servers_in_group = server_ct - (num_groups-1)*(small_group_size);
    else
	servers_in_group = small_group_size;

    gserver_nr = server_nr - group_nr * small_group_size;

    server_offset += global_stripes*global_stripe_size;
    server_offset += group_nr * small_group_stripe;

    /* Make l_off be the offset into the last full stripe < logical_offset */
    PVFS_offset l_off = logical_offset - global_stripes * global_stripe_size;    
    /* check to see if the l_off is after the group server_nr is in*/
    if(l_off > ((group_nr+1) * small_group_stripe ))
/* return the logical_offset from the beg of next full global stripe */
    {
	server_offset += global_stripe_size;
	/* since l_off is in a group < group_nr, it cant be the last group*/
	server_offset -= (small_group_size - gserver_nr) * strip_size;
	return server_offset;
    }
    else    /* is l_off is in group before group_nr */
    {	    /* watch out for group_nr == last group */
	if(l_off < (group_nr) * small_group_stripe) 
	{
	    server_offset += gserver_nr * strip_size;
	    return server_offset;
	}
	else	    /* l_off is in this group */
	{
	    g_off = l_off - group_nr*small_group_stripe;
	    /* Get rid of all the group-wide stripes */
	    group_stripes = g_off / (strip_size * servers_in_group);
	    server_offset += group_stripes * strip_size * servers_in_group;

	    /* We have an offset into the last stripe < l_off */
	    g_off -= group_stripes*strip_size * servers_in_group;   

	    /* Now g_off should be <= servers_in_group * strip_size */
#if ASSERT
	    assert(g_off <= servers_in_group*strip_size);
#endif
	    /* Determine what server g_off lands on */
	    if(g_off >= (gserver_nr+1) * strip_size)/* g_off on a > server*/
	    {
		/* Add another group strip if it fits */
		if(group_stripes == factor-1)	/* it wont fit, add global */
		{
		    /* Add a global stripe to the offset */
    		    server_offset += global_stripe_size;
		    server_offset += gserver_nr * strip_size;
		    return server_offset;
		}
		else
		{
		    server_offset += servers_in_group * strip_size;
		    return server_offset; 
		}
	    }
	    else 
	    {
		if(g_off / strip_size == gserver_nr)	/* on this server */
		{
		    return logical_offset;
		}
		else
		{	/* server_nr > server w/ g_off */
		    if(g_off < gserver_nr * strip_size)
		    {
			server_offset += gserver_nr * strip_size;
			return server_offset;
		    }
		}
	    }
	}
    }
    fprintf(stderr,"We shouldnt be here!!!!\nl_o: %d\tserver: %d\n",logical_offset,server_nr);

    return server_offset;
}
#endif

/*
 * Beginning in a given physical location, return the number of contiguous
 * bytes in the physical bytes stream on the current PVFS server that map
 * to contiguous bytes in the logical byte sequence.  Returns a length in
 * bytes.
 */
static PVFS_size contiguous_length(void* params,
                                   PINT_request_file_data* fd,
                                   PVFS_offset physical_offset)
{
    PVFS_twod_stripe_params* dparam = (PVFS_twod_stripe_params*)params;
    PVFS_size result = 0;
    result = (dparam->strip_size - (physical_offset % dparam->strip_size));

#if DEBUG
    fprintf(stdout,"%s: p_off: %d length: %d\n",__func__,physical_offset,result);
    if(result > 10)
	fprintf(stdout,"%s: CONTIG_LENGTH IS WRONG!\n",__func__);
#endif
    return result;
    //    return (dparam->strip_size - (physical_offset % dparam->strip_size));
}


static int set_param(const char* dist_name, void* params,
                    const char* param_name, void* value)
{
    PVFS_twod_stripe_params* dparam = (PVFS_twod_stripe_params*)params;
    if(strcmp(param_name, "strip_size")==0)
    {
	if(*(PVFS_size*)value <=0)
	    gossip_err("ERROR: strip_size param <= 0!\n");
	else
	{
#if DEBUG
	    fprintf(stdout,"%s: strip_size: %d\n",__func__,*(PVFS_size*)value);
#endif
	    memcpy(&(dparam->strip_size),value,sizeof(PVFS_size));
//	    dparam->strip_size = *(PVFS_size*)value;
	}
    }
    else
    {
	if(strcmp(param_name, "num_groups")==0)
	{
	    if(*(uint32_t*)value <= 0)
		gossip_err("ERROR: num_groups param <= 0!\n");
	    else
	    {
#if DEBUG
	    fprintf(stdout,"%s: num_groups: %d\n",__func__,*(uint32_t*)value);
#endif
		dparam->num_groups = *(uint32_t*)value;
	    }
	}
	else
	{
	    if(strcmp(param_name, "group_strip_factor")==0)
	    {
		if(*(uint32_t*)value <= 0)
		    gossip_err("ERROR: group_strip_factor param <= 0!\n");
		else
		{
#if DEBUG
	    fprintf(stdout,"%s: group_strip_factor: %d\n",__func__,*(uint32_t*)value);
#endif
		
		    dparam->group_strip_factor = *(uint32_t*)value;
		}
	    }
	}
    }
    return 0;
}



static void encode_params(char **pptr, void* params)
{
    PVFS_twod_stripe_params *dparam =(PVFS_twod_stripe_params*)params;
    encode_uint32_t(pptr,&dparam->num_groups);
    encode_PVFS_size(pptr, &dparam->strip_size);
    encode_int32_t(pptr,&dparam->group_strip_factor);
}


static void decode_params(char **pptr, void* params)
{
    PVFS_twod_stripe_params* dparam = (PVFS_twod_stripe_params*)params;
    decode_uint32_t(pptr, &dparam->num_groups);
    decode_PVFS_size(pptr, &dparam->strip_size);
    decode_uint32_t(pptr, &dparam->group_strip_factor);
}



static void registration_init(void* params)
{
    PINT_dist_register_param(PVFS_DIST_TWOD_STRIPE_NAME, "num_groups",
			    PVFS_twod_stripe_params, num_groups);
    PINT_dist_register_param(PVFS_DIST_TWOD_STRIPE_NAME, "strip_size",
			    PVFS_twod_stripe_params, strip_size);
    PINT_dist_register_param(PVFS_DIST_TWOD_STRIPE_NAME, "group_strip_factor",
			    PVFS_twod_stripe_params, group_strip_factor);
}

/* default twod_stripe_params */
static PVFS_twod_stripe_params twod_stripe_params = {
    PVFS_DIST_TWOD_STRIPE_DEFAULT_GROUPS,   /* num_groups */
    PVFS_DIST_TWOD_STRIPE_DEFAULT_STRIP_SIZE,
    PVFS_DIST_TWOD_STRIPE_DEFAULT_FACTOR
};

    

static PINT_dist_methods twod_stripe_methods = {
    logical_to_physical_offset,
    physical_to_logical_offset,
    next_mapped_offset,
    contiguous_length,
    logical_file_size,
    PINT_dist_default_get_num_dfiles,
    set_param,
    encode_params,
    decode_params,
    registration_init
};


PINT_dist twod_stripe_dist = {
    .dist_name = PVFS_DIST_TWOD_STRIPE_NAME,
    .name_size = roundup8(PVFS_DIST_TWOD_STRIPE_NAME_SIZE), /* name size */
    .param_size = roundup8(sizeof(PVFS_twod_stripe_params)), /* param size */
    .params = &twod_stripe_params,
    .methods = &twod_stripe_methods
};

