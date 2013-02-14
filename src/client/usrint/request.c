/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup usrint
 *
 *  PVFS2 user interface routines - routines to convert requests for pvfs
 */
#define USRINT_SOURCE 1
#include "usrint.h"
#include <sys/types.h>

int pvfs_check_vector(const struct iovec *iov,
                      int count,
                      PVFS_Request *req,
                      void **buf);

/**
 * converts a posix iovec into a PVFS Request
 */
int pvfs_convert_iovec (const struct iovec *vector,
                        int count,
                        PVFS_Request *req,
                        void **buf)
{
    /* for now just check for vectors and covert the rest */
    /* to a basic indexed struct */
    return pvfs_check_vector(vector, count, req, buf);
}

int pvfs_check_vector(const struct iovec *iov,
                      int count,
                      PVFS_Request *req,
                      void **buf)
{
    int i;
    int vstart;
    int vlen;
    int bsz;
    PVFS_size stride;
    int32_t *bsz_array;
    PVFS_size *disp_array;
    PVFS_Request *req_array;
    int rblk;

    /* set up request arrays */
    bsz_array = (int32_t *)malloc(count * sizeof(int32_t));
    if (!bsz_array)
    {
        return -1;
    }
    memset(bsz_array, 0, count * sizeof(int32_t));
    disp_array = (PVFS_size *)malloc(count * sizeof(PVFS_size));
    if (!disp_array)
    {
        free(bsz_array);
        return -1;
    }
    memset(disp_array, 0, count * sizeof(PVFS_size));
    req_array = (PVFS_Request *)malloc(count * sizeof(PVFS_Request));
    if (!disp_array)
    {
        free(disp_array);
        free(bsz_array);
        return -1;
    }
    memset(req_array, 0, count * sizeof(PVFS_Request));
    /* for now we assume that addresses in the iovec are ascending */
    /* not that otherwise won't work, but we're not sure */
    /* the first address will be assumed to be the base address of */
    /* the whole request.  the displacement of each vector is relative */
    /* to that address */
    if (count > 0)
    {
        *buf = iov[0].iov_base;
    }
    rblk = 0;
    /* start at beginning of iovec */
    i = 0;
    while(i < count)
    {
        /* starting a new vector at position i */
        vstart = i;
        vlen = 1;
        bsz = iov[i].iov_len;
        stride = 0;
        /* vector blocks must be of equal size */
        while(++i < count && iov[i].iov_len == bsz)
        {
            if(vlen == 1)
            {
                /* two blocks of equal size are a vector of two */
                stride = (u_char *)iov[i].iov_base -
                         (u_char *)iov[i - 1].iov_base;
                if (stride < bsz)
                {
                    /* overlapping blocks and negative strides are problems */
                    break;
                }
                vlen++;
            }
            else if (((u_char *)iov[i].iov_base -
                      (u_char *)iov[i - 1].iov_base) == stride)
            {
                /* to add more blocks, stride must match */
                vlen++;
            }
            else
            {
                /* doesn't match - end of vector */
                break;
            }
        }
        if (vlen == 1)
        {
            /* trivial conversion */
            bsz_array[rblk] = iov[vstart].iov_len;
            disp_array[rblk] = (PVFS_size)((u_char *)iov[vstart].iov_base -
                                                          (u_char *)*buf);
            req_array[rblk] = PVFS_BYTE;
            rblk++;
        }
        else
        {
            /* found a vector */
            bsz_array[rblk] = 1;
            disp_array[rblk] = (PVFS_size)((u_char *)iov[vstart].iov_base -
                                                          (u_char *)*buf);
            PVFS_Request_vector(vlen, bsz, stride, PVFS_BYTE, &req_array[rblk]);
            rblk++;
        }
    }
    /* now build full request */
    PVFS_Request_struct(rblk, bsz_array, disp_array, req_array, req);
    PVFS_Request_commit(req);
    free(bsz_array);
    free(disp_array);
    while (rblk--)
    {
        if (req_array[rblk] != PVFS_BYTE)
        {
            PVFS_Request_free(&req_array[rblk]);
        }
    }
    free(req_array);
    /* req is not freed, the caller is expected to do that */
    return 0;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

