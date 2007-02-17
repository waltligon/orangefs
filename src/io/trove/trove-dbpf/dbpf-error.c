/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>
#include <db.h>

#include "trove.h"
#include "trove-internal.h"
#include "gossip.h"
#include "dbpf.h"

PVFS_error dbpf_db_error_to_trove_error(int db_error_value)
{
    /* values greater than zero are errno values */
    if (db_error_value > 0)
    {
        return trove_errno_to_trove_error(db_error_value);
    }

    switch (db_error_value)
    {
        case 0:
            return 0;
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
	    gossip_err("Error: DB_RUNRECOVERY encountered.\n");
	    return TROVE_EIO;
#ifdef HAVE_DB_BUFFER_SMALL
        case DB_BUFFER_SMALL:
            /* all Berkeley DB gets should be allocating user memory,
             * so if we get BUFFER_SMALL it must be programming error
             */
            return TROVE_EINVAL;
#endif
    }
    return DBPF_ERROR_UNKNOWN; /* return some identifiable value */
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
