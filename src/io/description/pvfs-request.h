
/*
 * (C) 2002 Clemson University and The University of Chicago.
 *
 * See COPYING in top-level directory.
 */

#ifndef __PVFS_REQUEST_H
#define __PVFS_REQUEST_H

#include <pvfs2-types.h>

typedef struct PINT_Request *PVFS_Request;

int PVFS_Request_contiguous(PVFS_count32 count, PVFS_Request oldreq,
		      PVFS_Request *newreq);

int PVFS_Request_vector(PVFS_count32 count, PVFS_count32 blocklength,
		PVFS_size stride, PVFS_Request oldreq, PVFS_Request *newreq);

int PVFS_Request_hvector(PVFS_count32 count, PVFS_count32 blocklength,
		PVFS_size stride, PVFS_Request oldreq, PVFS_Request *newreq);

int PVFS_Request_indexed(PVFS_count32 count, PVFS_count32 *blocklengths,
		      PVFS_size *displacements, PVFS_Request oldreq, PVFS_Request *newreq);

int PVFS_Request_hindexed(PVFS_count32 count, PVFS_count32 *blocklengths,
		PVFS_size *displacements, PVFS_Request oldreq, PVFS_Request *newreq);

int PVFS_Request_struct(PVFS_count32 count, PVFS_count32 *blocklengths,
		PVFS_size *displacements, PVFS_Request *oldreqs, PVFS_Request *newreq);

int PVFS_Address(void* location, PVFS_offset *address);

int PVFS_Request_extent(PVFS_Request request, PVFS_size *extent);

int PVFS_Request_size(PVFS_Request request, PVFS_size *size);

int PVFS_Request_lb(PVFS_Request request, PVFS_size *displacement);

int PVFS_Request_ub(PVFS_Request request, PVFS_size *displacement);

int PVFS_Request_commit(PVFS_Request *reqp);

/* pre-defined request types */
extern PVFS_Request PVFS_CHAR;
extern PVFS_Request PVFS_SHORT;
extern PVFS_Request PVFS_INT;
extern PVFS_Request PVFS_LONG;
extern PVFS_Request PVFS_UNSIGNED_CHAR;
extern PVFS_Request PVFS_UNSIGNED_SHORT;
extern PVFS_Request PVFS_UNSIGNED_INT;
extern PVFS_Request PVFS_UNSIGNED_LONG;
extern PVFS_Request PVFS_FLOAT;
extern PVFS_Request PVFS_DOUBLE;
extern PVFS_Request PVFS_LONG_DOUBLE;
extern PVFS_Request PVFS_BTYE;
extern PVFS_Request PVFS_PACKED;

#endif /* __PVFS_REQUEST_H */
