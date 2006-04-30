/*
 * (C) 2006 The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "dlm_config.h"

/* Opaque handle used by the locking daemon */
typedef opaque  dlm_handle_t[DLM_HANDLE_LENGTH];

typedef int64_t dlm_offset_t;
typedef int64_t dlm_size_t;

typedef dlm_offset_t dlm_offsets_t<DLM_NUM_VECTORS>;
typedef dlm_size_t   dlm_sizes_t<DLM_NUM_VECTORS>;

enum dlm_mode_t {
	DLM_READ_MODE    = 0,
	DLM_WRITE_MODE   = 1,
	DLM_UPGRADE_MODE = 2
};

typedef int64_t dlm_token_t;

enum status_t {
	DLM_NO_ERROR   = 0,
	DLM_ERROR      = 1
};

/* Add more error codes if need be.
 * These are simply errno values prefixed by DLM_
 */
enum dlm_error_t  {
	DLM_EPERM = 1,	/* Operation not permitted */
	DLM_ENOENT = 2,	/* No such file or directory */
	DLM_ESRCH = 3,	/* No such process */
	DLM_EINTR = 4,	/* Interrupted system call */
	DLM_EIO = 5,	/* I/O error */
	DLM_ENXIO = 6,	/* No such device or address */
	DLM_E2BIG =	 7,	/* Arg list too long */
	DLM_ENOEXEC = 8,	/* Exec format error */
	DLM_EBADF = 9,	/* Bad file number */
	DLM_ECHILD = 10,	/* No child processes */
	DLM_EAGAIN = 11,	/* Try again */
	DLM_ENOMEM = 12,	/* Out of memory */
	DLM_EACCES = 13,	/* Permission denied */
	DLM_EFAULT = 14,	/* Bad address */
	DLM_ENOTBLK = 15,	/* Block device required */
	DLM_EBUSY = 16,	/* Device or resource busy */
	DLM_EEXIST = 17,	/* File exists */
	DLM_EXDEV = 18,	/* Cross-device link */
	DLM_ENODEV = 19,	/* No such device */
	DLM_ENOTDIR = 20,	/* Not a directory */
	DLM_EISDIR = 21,	/* Is a directory */
	DLM_EINVAL = 22,	/* Invalid argument */
	DLM_ENFILE = 23,	/* File table overflow */
	DLM_EMFILE = 24,	/* Too many open files */
	DLM_ENOTTY = 25,	/* Not a typewriter */
	DLM_ETXTBSY = 26,	/* Text file busy */
	DLM_EFBIG = 27,	/* File too large */
	DLM_ENOSPC = 28,	/* No space left on device */
	DLM_ESPIPE = 29,	/* Illegal seek */
	DLM_EROFS = 30,	/* Read-only file system */
	DLM_EMLINK = 31,	/* Too many links */
	DLM_EPIPE = 32,	/* Broken pipe */
	DLM_EDOM = 33,	/* Math argument out of domain of func */
	DLM_ERANGE = 34,	/* Math result not representable */
	DLM_EDEADLK = 35,	/* Resource deadlock would occur */
	DLM_ENAMETOOLONG = 36,	/* File name too long */
	DLM_ENOLCK = 37,	/* No record locks available */
	DLM_ENOSYS = 38,	/* Function not implemented */
	DLM_ENOTEMPTY = 39,	/* Directory not empty */
	DLM_ELOOP = 40,	/* Too many symbolic links encountered */
	DLM_ENOMSG = 42,	/* No message of desired type */
	DLM_EIDRM = 43,	/* Identifier removed */
	DLM_ECHRNG = 44,	/* Channel number out of range */
	DLM_EL2NSYNC = 45,	/* Level 2 not synchronized */
	DLM_EL3HLT	=	46,	/* Level 3 halted */
	DLM_EL3RST	=	47,	/* Level 3 reset */
	DLM_ELNRNG = 48,	/* Link number out of range */
	DLM_EUNATCH = 49,	/* Protocol driver not attached */
	DLM_ENOCSI = 50,	/* No CSI structure available */
	DLM_EL2HLT	=	51,	/* Level 2 halted */
	DLM_EBADE = 52,	/* Invalid exchange */
	DLM_EBADR = 53,	/* Invalid request descriptor */
	DLM_EXFULL = 54,	/* Exchange full */
	DLM_ENOANO = 55,	/* No anode */
	DLM_EBADRQC = 56,	/* Invalid request code */
	DLM_EBADSLT = 57,	/* Invalid slot */
	DLM_EBFONT = 59,	/* Bad font file format */
	DLM_ENOSTR = 60,	/* Device not a stream */
	DLM_ENODATA = 61,	/* No data available */
	DLM_ETIME = 62,	/* Timer expired */
	DLM_ENOSR = 63,	/* Out of streams resources */
	DLM_ENONET = 64,	/* Machine is not on the network */
	DLM_ENOPKG = 65,	/* Package not installed */
	DLM_EREMOTE = 66,	/* Object is remote */
	DLM_ENOLINK = 67,	/* Link has been severed */
	DLM_EADV = 68,	/* Advertise error */
	DLM_ESRMNT = 69,	/* Srmount error */
	DLM_ECOMM = 70,	/* Communication error on send */
	DLM_EPROTO = 71,	/* Protocol error */
	DLM_EMULTIHOP = 72,	/* Multihop attempted */
	DLM_EDOTDOT = 73,	/* RFS specific error */
	DLM_EBADMSG = 74,	/* Not a data message */
	DLM_EOVERFLOW = 75,	/* Value too large for defined data type */
	DLM_ENOTUNIQ = 76,	/* Name not unique on network */
	DLM_EBADFD = 77,	/* File descriptor in bad state */
	DLM_EREMCHG = 78,	/* Remote address changed */
	DLM_ELIBACC = 79,	/* Can not access a needed shared library */
	DLM_ELIBBAD = 80,	/* Accessing a corrupted shared library */
	DLM_ELIBSCN = 81,	/* .lib section in a.out corrupted */
	DLM_ELIBMAX = 82,	/* Attempting to link in too many shared libraries */
	DLM_ELIBEXEC = 83,	/* Cannot exec a shared library directly */
	DLM_EILSEQ = 84,	/* Illegal byte sequence */
	DLM_ERESTART = 85,	/* Interrupted system call should be restarted */
	DLM_ESTRPIPE = 86,	/* Streams pipe error */
	DLM_EUSERS = 87,	/* Too many users */
	DLM_ENOTSOCK = 88,	/* Socket operation on non-socket */
	DLM_EDESTADDRREQ = 89,	/* Destination address required */
	DLM_EMSGSIZE = 90,	/* Message too long */
	DLM_EPROTOTYPE = 91,	/* Protocol wrong type for socket */
	DLM_ENOPROTOOPT = 92,	/* Protocol not available */
	DLM_EPROTONOSUPPORT = 93,	/* Protocol not supported */
	DLM_ESOCKTNOSUPPORT = 94,	/* Socket type not supported */
	DLM_EOPNOTSUPP = 95,	/* Operation not supported on transport endpoint */
	DLM_EPFNOSUPPORT = 96,	/* Protocol family not supported */
	DLM_EAFNOSUPPORT = 97,	/* Address family not supported by protocol */
	DLM_EADDRINUSE = 98,	/* Address already in use */
	DLM_EADDRNOTAVAIL = 99,	/* Cannot assign requested address */
	DLM_ENETDOWN = 100,	/* Network is down */
	DLM_ENETUNREACH = 101,	/* Network is unreachable */
	DLM_ENETRESET = 102,	/* Network dropped connection because of reset */
	DLM_ECONNABORTED = 103,	/* Software caused connection abort */
	DLM_ECONNRESET = 104,	/* Connection reset by peer */
	DLM_ENOBUFS = 105,	/* No buffer space available */
	DLM_EISCONN = 106,	/* Transport endpoint is already connected */
	DLM_ENOTCONN = 107,	/* Transport endpoint is not connected */
	DLM_ESHUTDOWN = 108,	/* Cannot send after transport endpoint shutdown */
	DLM_ETOOMANYREFS = 109,	/* Too many references: cannot splice */
	DLM_ETIMEDOUT = 110,	/* Connection timed out */
	DLM_ECONNREFUSED = 111,	/* Connection refused */
	DLM_EHOSTDOWN = 112,	/* Host is down */
	DLM_EHOSTUNREACH = 113,	/* No route to host */
	DLM_EALREADY = 114,	/* Operation already in progress */
	DLM_EINPROGRESS = 115,	/* Operation now in progress */
	DLM_ESTALE = 116,	/* Stale NFS file handle */
	DLM_EUCLEAN = 117,	/* Structure needs cleaning */
	DLM_ENOTNAM = 118,	/* Not a XENIX named type file */
	DLM_ENAVAIL = 119,	/* No XENIX semaphores available */
	DLM_EISNAM = 120,	/* Is a named type file */
	DLM_EREMOTEIO = 121,	/* Remote I/O error */
	DLM_EDQUOT = 122,	/* Quota exceeded */

	DLM_ENOMEDIUM = 123,	/* No medium found */
	DLM_EMEDIUMTYPE = 124,	/* Wrong medium type */
	DLM_ECANCELED = 125,	/* Operation Cancelled */
	DLM_ENOKEY = 126,	/* Required key not available */
	DLM_EKEYEXPIRED = 127,	/* Key has expired */
	DLM_EKEYREVOKED = 128,	/* Key has been revoked */
	DLM_EKEYREJECTED = 129	/* Key was rejected by service */
};

struct dlm_status_t {
	status_t  	  	ds_status;
	dlm_error_t   	ds_error;
};

/* Lock request */
struct dlm_lock_req {
	dlm_handle_t	dr_handle;
	dlm_mode_t   	dr_mode;
	dlm_offset_t 	dr_offset;
	dlm_size_t   	dr_size;
};

/* Lock response */
struct dlm_lock_resp {
	dlm_status_t 	dr_status;
	dlm_token_t		dr_token;
};

/* Vectored Lock request */
struct dlm_lockv_req {
	dlm_handle_t   dr_handle;
	dlm_mode_t     dr_mode;
	dlm_offsets_t  dr_offsets;
	dlm_sizes_t    dr_sizes;
};

/* Vectored Lock response */
struct dlm_lockv_resp {
	dlm_status_t 	dr_status;
	dlm_token_t		dr_token;
};

/* Unlock request */
struct dlm_unlock_req {
	dlm_token_t   	ur_token;
};

/* Unlock response */
struct dlm_unlock_resp {
	dlm_status_t   ur_status;
};

program DLM_MGR {
	version dlm_v1 {
		dlm_lock_resp   DLM_LOCK(dlm_lock_req)     = 1;
		dlm_lockv_resp  DLM_LOCKV(dlm_lockv_req)   = 2;
		dlm_unlock_resp DLM_UNLOCK(dlm_unlock_req) = 3;
	} = 1;
} = 0x20000101; /* Some unique program number which is not used internally anyway */
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
