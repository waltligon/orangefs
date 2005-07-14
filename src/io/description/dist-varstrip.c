/*
 * (C) 2005 Tobias Eberle <tobias.eberle@gmx.de>
 *
 * See COPYING in top-level directory.
 */       

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define __PINT_REQPROTO_ENCODE_FUNCS_C
#include "pint-distribution.h"
#include "pint-dist-utils.h"
#include "pvfs2-dist-varstrip.h"
#include "dist-varstrip-parser.h"

#include "pvfs2-debug.h"
#include "gossip.h"

static PVFS_offset logical_to_physical_offset(void* params,
                                              PINT_request_file_data* fd,
                                              PVFS_offset logical_offset)
{
    PVFS_varstrip_params* varstrip_params = (PVFS_varstrip_params*)params;
    /* parse parameter */
    PINT_dist_strips *strips;
    uint32_t ui_count, ii, ui_stripe_nr;
    PVFS_size ui_stripe_size;
    int32_t server_nr = fd->server_nr;
    
    if (PINT_dist_strips_parse(
            varstrip_params->strips, &strips, &ui_count) == -1)
    {
        return -1;
    }
    
    /* stripe size */
    ui_stripe_size = strips[ui_count - 1].offset + 
        strips[ui_count - 1].size;
    ui_stripe_nr = logical_offset / ui_stripe_size;
    /* search my strips */
    for(ii = 0; ii < ui_count; ii++)
    {
        if (strips[ii].server_nr == server_nr)
        {
            if ((logical_offset >= (ui_stripe_nr * ui_stripe_size) + 
                                   strips[ii].offset) &&
                (logical_offset <= (ui_stripe_nr * ui_stripe_size) + 
                                   strips[ii].offset + strips[ii].size - 1))
            {
                unsigned int jj;
                PVFS_offset ui_offset_in_strips;
                
                /* get size of all strips in the stripe of this server */
                /* and get the size of all strips of this server that are */
                /* before the current Strips */
                PVFS_size ui_size_of_all_my_strips_in_stripe = 0;
                PVFS_size ui_size_of_my_strips_before_current = 0;
                for (jj = 0; jj < ui_count; jj++)
                {
                    if (strips[jj].server_nr == server_nr)
                    {
                        ui_size_of_all_my_strips_in_stripe += strips[jj].size;
                        if (jj < ii)
                        {
                            ui_size_of_my_strips_before_current += 
                                strips[jj].size;
                        }
                    }
                }
                
                ui_offset_in_strips = logical_offset - 
                    (ui_stripe_nr * ui_stripe_size) - strips[ii].offset;
                
                PINT_dist_strips_free_mem(&strips);
                return ui_stripe_nr * ui_size_of_all_my_strips_in_stripe + 
                       ui_size_of_my_strips_before_current + 
                       ui_offset_in_strips;
            }
        }
        /* try next */
    }
    gossip_err("logical_to_physical: todo\n");
    /*
     * todo: logical offsets that do not belong to the current server
     *       I dont know if it is really neccessary to implement this.
     *       In practice I have never seen this error string.
     */
    PINT_dist_strips_free_mem(&strips);
    return 0;
}

static PVFS_offset physical_to_logical_offset(void* params,
                                              PINT_request_file_data* fd,
                                              PVFS_offset physical_offset)
{
    /* parse parameter */
    PINT_dist_strips *strips;
    unsigned int ui_count, jj, ui_stripe_nr, ii;
    uint32_t server_nr = fd->server_nr;
    PVFS_size ui_size_of_strips_in_stripe = 0;
    PVFS_offset ui_physical_offset_in_stripe;
    PVFS_offset ui_offset_in_stripe = 0;

    PVFS_varstrip_params* varstrip_params = (PVFS_varstrip_params*)params;

    if (PINT_dist_strips_parse(
            varstrip_params->strips, &strips, &ui_count) == -1)
    {
        return -1;
    }
    
    /* to what stripe belongs that physical offset? */
    /* must get size of strips of this server in one stripe first */
    for (jj = 0; jj < ui_count; jj++)
    {
        if (strips[jj].server_nr == server_nr)
        {
            ui_size_of_strips_in_stripe += strips[jj].size;
        }
    }
    ui_stripe_nr = physical_offset / ui_size_of_strips_in_stripe;

    /* now get the Strips number in the 
     * stripe the physical offset belongs to */
    ui_physical_offset_in_stripe = 
        physical_offset - ui_stripe_nr * ui_size_of_strips_in_stripe;
    for(ii = 0; ii < ui_count; ii++)
    {
        if (strips[ii].server_nr == server_nr)
        {
            if (ui_physical_offset_in_stripe < 
                ui_offset_in_stripe + strips[ii].size)
            {
                /* found! */
                PVFS_offset logical_offset = ui_stripe_nr * 
                    (strips[ui_count - 1].offset + strips[ui_count - 1].size) + 
                    strips[ii].offset + 
                    (ui_physical_offset_in_stripe - ui_offset_in_stripe);
                PINT_dist_strips_free_mem(&strips);
                return logical_offset;
            }
            else
            {
                ui_offset_in_stripe += strips[ii].size;
            }
        }
        /* try next */
    }
    /* error! */
    gossip_err("ERROR in varstrip distribution in "
               "function physical_to_logical: no fitting strip found!\n");
    PINT_dist_strips_free_mem(&strips);
    return -1;
}

static PVFS_offset next_mapped_offset(void* params,
                                      PINT_request_file_data* fd,
                                      PVFS_offset logical_offset)
{
    PVFS_varstrip_params* varstrip_params = (PVFS_varstrip_params*)params;
    /* parse parameter */
    PINT_dist_strips *strips;
    unsigned int ui_count, ii, ui_stripe_nr;
    uint32_t server_nr = fd->server_nr;
    PVFS_size ui_stripe_size;
    PVFS_offset ui_offset_in_stripe;

    if (PINT_dist_strips_parse(
            varstrip_params->strips, &strips, &ui_count) == -1)
    {
        return -1;
    }

    /* get number of stripes */
    ui_stripe_size = 
        strips[ui_count - 1].offset +
        strips[ui_count - 1].size;
    ui_stripe_nr = logical_offset / ui_stripe_size;

    /* get offset in stripe */
    ui_offset_in_stripe = 
        logical_offset - (ui_stripe_nr * ui_stripe_size);

    /* get Strips number */
    ii = 0;
    for(ii = 0; ii < ui_count; ii++)
    {
        if ((ui_offset_in_stripe >= strips[ii].offset) &&
            (ui_offset_in_stripe <= strips[ii].offset + strips[ii].size - 1))
        {
            /* found! */
            break;
        }
    }
    if (strips[ii].server_nr == server_nr)
    {
        /* logical offset is part of my Strips */
        PINT_dist_strips_free_mem(&strips);
        return logical_offset;
    }
    else
    {
        /* search my next Strips */
        unsigned int jj;
        for (jj = ii + 1; jj < ui_count; jj++)
        {
            if (strips[jj].server_nr == server_nr)
            {
                /* found! */
                PVFS_offset ui_next_mapped_offset = 
                    ui_stripe_nr * ui_stripe_size + strips[jj].offset;
                PINT_dist_strips_free_mem(&strips);
                return ui_next_mapped_offset;
            }
        }
        /* not found! */
        /* -> next stripe */
        ui_stripe_nr++;
        for (jj = 0; jj < ui_count; jj++)
        {
            if (strips[jj].server_nr == server_nr)
            {
                PVFS_offset ui_next_mapped_offset = 
                    ui_stripe_nr * ui_stripe_size + strips[jj].offset;
                PINT_dist_strips_free_mem(&strips);
                return ui_next_mapped_offset;
            }
        }
    }
    /* huh? this is an error */
    PINT_dist_strips_free_mem(&strips);
    gossip_err("ERROR in varstrip distribution in "
               "next_mapped_offset: Did not find my next offset\n");
    return -1;
}

static PVFS_size contiguous_length(void* params,
                                   PINT_request_file_data* fd,
                                   PVFS_offset physical_offset)
{
    PVFS_varstrip_params* varstrip_params;
    /* convert to a logical offset */
     
    /* parse parameter */
    PINT_dist_strips *strips;
    PVFS_offset ui_offset_in_stripe, logical_offset;
    unsigned int ui_count, ii, ui_stripe_nr, ui_stripe_size;

    logical_offset = physical_to_logical_offset(
        params, fd, physical_offset);
    
    varstrip_params = (PVFS_varstrip_params*)params;

    if (PINT_dist_strips_parse(
            varstrip_params->strips, &strips, &ui_count) == -1)
    {
        return -1;
    }
    
    /* get number of stripes */
    ui_stripe_size = strips[ui_count - 1].offset +
        strips[ui_count - 1].size;
    ui_stripe_nr = logical_offset / ui_stripe_size;
    /* get offset in stripe */
    ui_count = logical_offset - (ui_stripe_nr * ui_stripe_size);
    /* get Strips */
    for(ii = 0; ii < ui_count; ii++)
    {
        if ((ui_offset_in_stripe >= strips[ii].offset) &&
            (ui_offset_in_stripe <= strips[ii].offset + strips[ii].size - 1))
        {
            /* found! */
            PVFS_size ui_contiguous_length = strips[ii].offset + 
                strips[ii].size - ui_offset_in_stripe;
            PINT_dist_strips_free_mem(&strips);
            
            return ui_contiguous_length;
        }
    }
    /* error! */
    gossip_err("ERROR in varstrip distribution in "
               "function contiguous_length: no fitting strip found\n");
    PINT_dist_strips_free_mem(&strips);
    return 0;
}

static PVFS_size logical_file_size(void* params,
                                   uint32_t server_ct,
                                   PVFS_size *psizes)
{
    PVFS_size ui_file_size = 0;
    uint32_t ii;
    
    if (!psizes)
        return -1;
    for (ii = 0; ii < server_ct; ii++)
    {
        ui_file_size += psizes[ii];
    }
    return ui_file_size;
}

static int get_num_dfiles(void* params,
                          uint32_t num_servers_requested,
                          uint32_t num_dfiles_requested)
{
    PVFS_varstrip_params* varstrip_params = (PVFS_varstrip_params*)params;
    /* parse parameter */
    PINT_dist_strips *strips;
    unsigned int i_highest_data_file_number = 0;
    unsigned int ii, ui_count;

    if (PINT_dist_strips_parse(
            varstrip_params->strips, &strips, &ui_count) == -1)
    {
        /* error */
        return -1;
    }
    /* get highest requested data file number */
    for (ii = 0; ii < ui_count; ii++)
    {
        if (strips[ii].server_nr > i_highest_data_file_number)
        {
            i_highest_data_file_number = strips[ii].server_nr;
        }
    }
    /* are all data file numbers available in string? */
    for (ii = 0; ii < i_highest_data_file_number; ii++)
    {
        int b_found = 0;
        unsigned int jj;
        for (jj = 0; jj < ui_count; jj++)
        {
            if (strips[jj].server_nr == ii)
            {
                b_found = 1;
                break;
            }
        }
        if (!b_found)
        {
            gossip_err("ERROR in varstrip distribution: The strip "
                       "partitioning string must contain all data "
                       "file numbers from 0 to the highest one specified!\n");
            return -1;
        }
    }
    PINT_dist_strips_free_mem(&strips);
    i_highest_data_file_number++;
    if (i_highest_data_file_number > num_servers_requested)
    {
        gossip_err("ERROR in varstrip distribution: There are more "
                   "data files specified in strip partitioning string "
                   "than servers available!\n");
        return -1;
    }
    return i_highest_data_file_number;
}

/* its like the default one but assures that last character is \0
 * we need null terminated strings!
 */
static int set_param(const char* dist_name, void* params,
                    const char* param_name, void* value)
{
    PVFS_varstrip_params* varstrip_params = (PVFS_varstrip_params*)params;
    if (strcmp(param_name, "strips") == 0)
    {
        if (strlen((char *)value) == 0)
        {
            gossip_err("ERROR: Parameter 'strips' empty!\n");
        }
        else
        {
            if (strlen((char *)value) > 
                PVFS_DIST_VARSTRIP_MAX_STRIPS_STRING_LENGTH)
            {
                gossip_err("ERROR: Parameter 'strips' too long!\n");
            }
            else
            {
                strcpy(varstrip_params->strips, (char *)value);
            }
        }
    }
    return 0;
}


static void encode_params(char **pptr, void* params)
{
    PVFS_varstrip_params* varstrip_params = (PVFS_varstrip_params*)params;
    encode_string(pptr, &varstrip_params->strips);
}

static void decode_params(char **pptr, void* params)
{
    PVFS_varstrip_params* varstrip_params = (PVFS_varstrip_params*)params;
    decode_here_string(pptr, varstrip_params->strips);
}

static void registration_init(void* params)
{
    PINT_dist_register_param(PVFS_DIST_VARSTRIP_NAME, "strips",
            PVFS_varstrip_params, strips);
}


static PVFS_varstrip_params varstrip_params = { "\0" };

static PINT_dist_methods varstrip_methods = {
    logical_to_physical_offset,
    physical_to_logical_offset,
    next_mapped_offset,
    contiguous_length,
    logical_file_size,
    get_num_dfiles,
    set_param,
    encode_params,
    decode_params,
    registration_init
};

PINT_dist varstrip_dist = {
    PVFS_DIST_VARSTRIP_NAME,
    roundup8(PVFS_DIST_VARSTRIP_NAME_SIZE), /* name size */
    roundup8(sizeof(PVFS_varstrip_params)), /* param size */
    &varstrip_params,
    &varstrip_methods
};

/*
 * Local variables:
 *  mode: c
 *  c-indent-level: 4
 *  c-varstrip-offset: 4
 * End:
 *
 * vim: ft=c ts=8 sts=4 sw=4 expandtab
 */
