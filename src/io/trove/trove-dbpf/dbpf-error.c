/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <db.h>

#include "trove.h"
#include "trove-internal.h"

PVFS_error dbpf_db_error_to_trove_error(int db_error_value)
{
    /* values greater than zero are errno values */
    if (db_error_value > 0) return trove_errno_to_trove_error(db_error_value);
    else if (db_error_value == 0) return 0; /* no mapping */

    switch (db_error_value) {
	case DB_NOTFOUND:
	case DB_KEYEMPTY:
	    return TROVE_ENOENT;
	case DB_KEYEXIST:
	    return TROVE_EEXIST;
	case DB_LOCK_DEADLOCK:
	    return TROVE_EDEADLK;
	case DB_LOCK_NOTGRANTED:
	    return TROVE_ENOLCK;
	case DB_RUNRECOVERY:
	    return TROVE_EIO;
    }
    return 4243; /* return some identifiable number */
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
