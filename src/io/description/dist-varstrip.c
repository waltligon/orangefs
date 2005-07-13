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
    PVFS_varstrip_params* varstripParams = (PVFS_varstrip_params*)params;
    //parse parameter
    Strips *strips;
    unsigned int uiCount;
    uint32_t server_nr = fd->server_nr;
    
    if (strips_Parse(varstripParams->strips, &strips, &uiCount) == -1)
    {
        return -1;
    }
    
    //stripe size
    PVFS_size uiStripeSize = strips[uiCount - 1].offset + 
                             strips[uiCount - 1].size;
    unsigned int uiStripeNr = logical_offset / uiStripeSize;
    //search my Strips
    unsigned int ii;
    for(ii = 0; ii < uiCount; ii++)
    {
        if (strips[ii].servernr == server_nr)
        {
            if ((logical_offset >= (uiStripeNr * uiStripeSize) + 
                                   strips[ii].offset) &&
                (logical_offset <= (uiStripeNr * uiStripeSize) + 
                                   strips[ii].offset + strips[ii].size - 1))
            {
                //get size of all strips in the stripe of this server
                //and get the size of all strips of this server that are
                //before the current Strips
                PVFS_size uiSizeOfAllMystripsInStripe = 0;
                PVFS_size uiSizeOfMystripsBeforeCurrent = 0;
                unsigned int jj;
                for (jj = 0; jj < uiCount; jj++)
                {
                    if (strips[jj].servernr == server_nr)
                    {
                        uiSizeOfAllMystripsInStripe += strips[jj].size;
                        if (jj < ii)
                        {
                            uiSizeOfMystripsBeforeCurrent += strips[jj].size;
                        }
                    }
                }
                
                PVFS_offset uiOffsetInStrips = logical_offset - 
                    (uiStripeNr * uiStripeSize) - strips[ii].offset;
                
                strips_FreeMem(&strips);
                return uiStripeNr * uiSizeOfAllMystripsInStripe + 
                       uiSizeOfMystripsBeforeCurrent + uiOffsetInStrips;
            }
        }
        //try next
    }
    gossip_err("logical_to_physical: todo\n");
    //todo: logical offsets that do not belong to the current server
    //      I dont know if it is really neccessary to implement this.
    //      In practice I have never seen this error string.
    strips_FreeMem(&strips);
    return 0;
}

static PVFS_offset physical_to_logical_offset(void* params,
                                              PINT_request_file_data* fd,
                                              PVFS_offset physical_offset)
{
    PVFS_varstrip_params* varstripParams = (PVFS_varstrip_params*)params;
    //parse parameter
    Strips *strips;
    unsigned int uiCount;
    uint32_t server_nr = fd->server_nr;

    if (strips_Parse(varstripParams->strips, &strips, &uiCount) == -1)
    {
        return -1;
    }
    
    //to what stripe belongs that physical offset?
    //must get size of strips of this server in one stripe first
    PVFS_size uiSizeOfstripsInStripe = 0;
    unsigned int jj;
    for (jj = 0; jj < uiCount; jj++)
    {
        if (strips[jj].servernr == server_nr)
        {
            uiSizeOfstripsInStripe += strips[jj].size;
        }
    }
    unsigned int uiStripeNr = physical_offset / uiSizeOfstripsInStripe;

    //now get the Strips number in the stripe the physical offset belongs to
    PVFS_offset uiPhysicalOffsetInStripe = physical_offset - 
                                           uiStripeNr * uiSizeOfstripsInStripe;
    PVFS_offset uiOffsetInStripe = 0;
    unsigned int ii;
    for(ii = 0; ii < uiCount; ii++)
    {
        if (strips[ii].servernr == server_nr)
        {
            if (uiPhysicalOffsetInStripe < uiOffsetInStripe + strips[ii].size)
            {
                //found!
                PVFS_offset logical_offset = uiStripeNr * 
                    (strips[uiCount - 1].offset + strips[uiCount - 1].size) + 
                    strips[ii].offset + 
                    (uiPhysicalOffsetInStripe - uiOffsetInStripe);
                strips_FreeMem(&strips);
                return logical_offset;
            }
            else
            {
                uiOffsetInStripe += strips[ii].size;
            }
        }
        //try next
    }
    //error!
    gossip_err("ERROR in varstrip distribution in function physical_to_logical: no fitting strip found!\n");
    strips_FreeMem(&strips);
    return -1;
}

static PVFS_offset next_mapped_offset(void* params,
                                      PINT_request_file_data* fd,
                                      PVFS_offset logical_offset)
{
    PVFS_varstrip_params* varstripParams = (PVFS_varstrip_params*)params;
    //parse parameter
    Strips *strips;
    unsigned int uiCount;
    uint32_t server_nr = fd->server_nr;
    
    if (strips_Parse(varstripParams->strips, &strips, &uiCount) == -1)
    {
        return -1;
    }

    //get number of stripes
    PVFS_size uiStripeSize = strips[uiCount - 1].offset +
                             strips[uiCount - 1].size;
    unsigned int uiStripeNr = logical_offset / uiStripeSize;

    //get offset in stripe
    PVFS_offset uiOffsetInStripe = logical_offset - 
                                   (uiStripeNr * uiStripeSize);
    //get Strips number
    unsigned int ii = 0;
    for(ii = 0; ii < uiCount; ii++)
    {
        if ((uiOffsetInStripe >= strips[ii].offset) &&
            (uiOffsetInStripe <= strips[ii].offset + strips[ii].size - 1))
        {
            //found!
            break;
        }
    }
    if (strips[ii].servernr == server_nr)
    {
        //logical offset is part of my Strips
        strips_FreeMem(&strips);
        return logical_offset;
    }
    else
    {
        //search my next Strips
        unsigned int jj;
        for (jj = ii + 1; jj < uiCount; jj++)
        {
            if (strips[jj].servernr == server_nr)
            {
                //found!
                PVFS_offset uiNextMappedOffset = uiStripeNr * uiStripeSize + 
                                                 strips[jj].offset;
                strips_FreeMem(&strips);
                return uiNextMappedOffset;
            }
        }
        //not found!
        //-> next stripe
        uiStripeNr++;
        for (jj = 0; jj < uiCount; jj++)
        {
            if (strips[jj].servernr == server_nr)
            {
                PVFS_offset uiNextMappedOffset = uiStripeNr * uiStripeSize + 
                                                 strips[jj].offset;
                strips_FreeMem(&strips);
                return uiNextMappedOffset;
            }
        }
    }
    //huh? this is an error
    strips_FreeMem(&strips);
    gossip_err("ERROR in varstrip distribution in next_mapped_offset: Did not find my next offset\n");
    return -1;
}

static PVFS_size contiguous_length(void* params,
                                   PINT_request_file_data* fd,
                                   PVFS_offset physical_offset)
{
    //convert to a logical offset
    PVFS_offset logical_offset = physical_to_logical_offset(
        params, fd, physical_offset);
    
    PVFS_varstrip_params* varstripParams = (PVFS_varstrip_params*)params;
    //parse parameter
    Strips *strips;
    unsigned int uiCount;

    if (strips_Parse(varstripParams->strips, &strips, &uiCount) == -1)
    {
        return -1;
    }
    
    //get number of stripes
    PVFS_size uiStripeSize = strips[uiCount - 1].offset +
                             strips[uiCount - 1].size;
    unsigned int uiStripeNr = logical_offset / uiStripeSize;
    //get offset in stripe
    PVFS_offset uiOffsetInStripe = logical_offset -
                                   (uiStripeNr * uiStripeSize);
    //get Strips
    unsigned int ii;
    for(ii = 0; ii < uiCount; ii++)
    {
        if ((uiOffsetInStripe >= strips[ii].offset) &&
            (uiOffsetInStripe <= strips[ii].offset + strips[ii].size - 1))
        {
            //found!
            PVFS_size uiContiguousLength = strips[ii].offset + 
                strips[ii].size - uiOffsetInStripe;// - 1;
            strips_FreeMem(&strips);
            
            return uiContiguousLength;
        }
    }
    //error!
    gossip_err("ERROR in varstrip distribution in function contiguous_length: no fitting strip found\n");
    strips_FreeMem(&strips);
    return 0;
}

static PVFS_size logical_file_size(void* params,
                                   uint32_t server_ct,
                                   PVFS_size *psizes)
{
    if (!psizes)
        return -1;
    PVFS_size uiFileSize = 0;
    uint32_t ii;
    for (ii = 0; ii < server_ct; ii++)
    {
        uiFileSize += psizes[ii];
    }
    return uiFileSize;
}

static int get_num_dfiles(void* params,
                          uint32_t num_servers_requested,
                          uint32_t num_dfiles_requested)
{
    PVFS_varstrip_params* varstripParams = (PVFS_varstrip_params*)params;
    //parse parameter
    Strips *strips;
    unsigned int uiCount;
    if (strips_Parse(varstripParams->strips, &strips, &uiCount) == -1)
    {
        //error
        return -1;
    }
    //get highest requested data file number
    unsigned int iHighestDataFileNumber = 0;
    unsigned int ii;
    for (ii = 0; ii < uiCount; ii++)
    {
        if (strips[ii].servernr > iHighestDataFileNumber)
        {
            iHighestDataFileNumber = strips[ii].servernr;
        }
    }
    //are all data file numbers available in string?
    for (ii = 0; ii < iHighestDataFileNumber; ii++)
    {
        int bFound = 0;
        unsigned int jj;
        for (jj = 0; jj < uiCount; jj++)
        {
            if (strips[jj].servernr == ii)
            {
                bFound = 1;
                break;
            }
        }
        if (!bFound)
        {
            gossip_err("ERROR in varstrip distribution: The strip partitioning string must contain all data file numbers from 0 to the highest one specified!\n");
            return -1;
        }
    }
    strips_FreeMem(&strips);
    iHighestDataFileNumber++;
    if (iHighestDataFileNumber > num_servers_requested)
    {
        gossip_err("ERROR in varstrip distribution: There are more data files specified in strip partitioning string than servers available!\n");
        return -1;
    }
    return iHighestDataFileNumber;
}

//its like the default one but assures that last character is \0
//we need null terminated strings!
static int set_param(const char* dist_name, void* params,
                    const char* param_name, void* value)
{
    PVFS_varstrip_params* varstripParams = (PVFS_varstrip_params*)params;
    if (strcmp(param_name, "strips") == 0)
    {
        if (strlen((char *)value) == 0)
        {
            gossip_err("ERROR: Parameter 'strips' empty!\n");
        }
        else
        {
            if (strlen((char *)value) > PVFS_DIST_VARSTRIP_MAX_STRIPS_STRING_LENGTH)
            {
                gossip_err("ERROR: Parameter 'strips' too long!\n");
            }
            else
            {
                strcpy(varstripParams->strips, (char *)value);
            }
        }
    }
    return 0;
}


static void encode_params(char **pptr, void* params)
{
    PVFS_varstrip_params* varstripParams = (PVFS_varstrip_params*)params;
    encode_string(pptr, &varstripParams->strips);
}

static void decode_params(char **pptr, void* params)
{
    PVFS_varstrip_params* varstripParams = (PVFS_varstrip_params*)params;
    decode_here_string(pptr, varstripParams->strips);
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
