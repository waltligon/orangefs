#ifndef __PINT_SEGPOOL_H__
#define __PINT_SEGPOOL_H__

#include "pvfs2-types.h"
#include "pvfs2-request.h"
#include "pint-distribution.h"

/* The segment pool interface is used for pipelining, where
 * multiple calls to get segments from the same request are made.
 *
 * The basic use of the interface is as follows.  Given a memory and file request,
 * PINT_segpool_handle_init is called to initialize the segment pool handle.
 * This call is made once for a given memory and file request.
 *
 * Component elements (each pipelining unit) then calls PINT_segpool_register
 * to register its intent to take segments from the pool.  This is needed to
 * get a unique unit id that can be passed to PINT_segpool_take_segments to
 * identify which unit is taking segments.
 *
 * Different pipelining elements then call PINT_segpool_take_segments as necessary
 * using the offset and size arrays returned.  The offset and size arrays returned
 * are valid until the same pipelining unit calls PINT_segpool_take_segments again,
 * or PINT_segpool_destroy is called.
 *
 * Finally a call to PINT_segpool_destroy is called to cleanup.
 */

enum PINT_segpool_type
{
    PINT_SP_SERVER_READ = 1,
    PINT_SP_SERVER_WRITE,
    PINT_SP_CLIENT_READ,
    PINT_SP_CLIENT_WRITE
};

typedef struct PINT_segpool_handle *PINT_segpool_handle_t;
typedef PVFS_id_gen_t PINT_segpool_unit_id;

int PINT_segpool_init(PVFS_Request mem_request,
                      PVFS_Request file_request,
                      PVFS_size file_size,
                      PVFS_offset request_offset,
                      PVFS_size aggregate_size,
                      uint32_t server_number,
                      uint32_t server_count,
                      PINT_dist *dist,
                      enum PINT_segpool_type type,
                      PINT_segpool_handle_t *h);

int PINT_segpool_destroy(PINT_segpool_handle_t h);

int PINT_segpool_register(PINT_segpool_handle_t h,
                          PINT_segpool_unit_id *id);

int PINT_segpool_take_segments(PINT_segpool_handle_t h,
                               PINT_segpool_unit_id id,
                               PVFS_size *bytes,
                               int *count,
                               PVFS_offset **offsets,
                               PVFS_size **sizes);

int segpool_done(PINT_segpool_handle_t h);

#endif
