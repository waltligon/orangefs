/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include <errno.h>

#include "trove.h"
#include "trove-internal.h"

struct errno_mapping {
    int errno_value;
    PVFS_error trove_value;
};

static struct errno_mapping trove_error_map[] =
{
    { EPERM, TROVE_EPERM },
    { ENOENT, TROVE_ENOENT },
    { EINTR, TROVE_EINTR },
    { EIO, TROVE_EIO },
    { ENXIO, TROVE_ENXIO },
    { EBADF, TROVE_EBADF },
    { EAGAIN, TROVE_EAGAIN },
    { ENOMEM, TROVE_ENOMEM },
    { EFAULT          , TROVE_EFAULT          },
    { EBUSY           , TROVE_EBUSY           },
    { EEXIST          , TROVE_EEXIST          },
    { ENODEV          , TROVE_ENODEV          },
    { ENOTDIR         , TROVE_ENOTDIR         },
    { EISDIR          , TROVE_EISDIR          },
    { EINVAL          , TROVE_EINVAL          },
    { EMFILE          , TROVE_EMFILE          },
    { EFBIG           , TROVE_EFBIG           },
    { ENOSPC          , TROVE_ENOSPC          },
    { EROFS           , TROVE_EROFS           },
    { EMLINK          , TROVE_EMLINK          },
    { EPIPE           , TROVE_EPIPE           },
    { EDEADLK         , TROVE_EDEADLK         },
    { ENAMETOOLONG    , TROVE_ENAMETOOLONG    },
    { ENOLCK          , TROVE_ENOLCK          },
    { ENOSYS          , TROVE_ENOSYS          },
    { ENOTEMPTY       , TROVE_ENOTEMPTY       },
    { ELOOP           , TROVE_ELOOP           },
    { EWOULDBLOCK     , TROVE_EWOULDBLOCK     },
    { ENOMSG          , TROVE_ENOMSG          },
    { EUNATCH         , TROVE_EUNATCH         },
    { EBADR           , TROVE_EBADR           },
    { EDEADLOCK       , TROVE_EDEADLOCK       },
    { ENODATA         , TROVE_ENODATA         },
    { ETIME           , TROVE_ETIME           },
    { ENONET          , TROVE_ENONET          },
    { EREMOTE         , TROVE_EREMOTE         },
    { ECOMM           , TROVE_ECOMM           },
    { EPROTO          , TROVE_EPROTO          },
    { EBADMSG         , TROVE_EBADMSG         },
    { EOVERFLOW       , TROVE_EOVERFLOW       },
    { ERESTART        , TROVE_ERESTART        },
    { EMSGSIZE        , TROVE_EMSGSIZE        },
    { EPROTOTYPE      , TROVE_EPROTOTYPE      },
    { ENOPROTOOPT     , TROVE_ENOPROTOOPT     },
    { EPROTONOSUPPORT , TROVE_EPROTONOSUPPORT },
    { EOPNOTSUPP      , TROVE_EOPNOTSUPP      },
    { EADDRINUSE      , TROVE_EADDRINUSE      },
    { EADDRNOTAVAIL   , TROVE_EADDRNOTAVAIL   },
    { ENETDOWN        , TROVE_ENETDOWN        },
    { ENETUNREACH     , TROVE_ENETUNREACH     },
    { ENETRESET       , TROVE_ENETRESET       },
    { ENOBUFS         , TROVE_ENOBUFS         },
    { ETIMEDOUT       , TROVE_ETIMEDOUT       },
    { ECONNREFUSED    , TROVE_ECONNREFUSED    },
    { EHOSTDOWN       , TROVE_EHOSTDOWN       },
    { EHOSTUNREACH    , TROVE_EHOSTUNREACH    },
    { EALREADY        , TROVE_EALREADY        },
    { 0               , 0 }
};

PVFS_error trove_errno_to_trove_error(int errno_value)
{
    int i = 0;

    /* don't try to map invalid values */
    if (errno_value <= 0) return errno_value;

    while (trove_error_map[i].errno_value != 0) {
	if (trove_error_map[i].errno_value == errno_value) {
	    return trove_error_map[i].trove_value;
	}
	i++;
    }
    return 4242; /* just return some identifiable number */
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
