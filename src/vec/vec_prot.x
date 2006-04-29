/*
 * (C) 2006 The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "vec_config.h"

/* Opaque handle used by the version vector daemon */
typedef opaque  vec_handle_t[VEC_HANDLE_LENGTH];

typedef int64_t vec_offset_t;
typedef int64_t vec_size_t;
typedef int32_t vec_stripe_size_t;
typedef int32_t vec_server_count_t;

/* This type is for one version vector */
typedef uint32_t		  vec_vectors_t<VEC_MAX_SERVERS>;
/* This type is for a set of version vectors */
typedef vec_vectors_t vec_svectors_t<VEC_MAX_SVECTORS>;

enum vec_mode_t {
	VEC_READ_MODE  = 0,
	VEC_WRITE_MODE = 1
};

enum vstatus_t {
	VEC_NO_ERROR   = 0,
	VEC_ERROR      = 1
};

/* Add more error codes if need be.
 * These are simply errno values prefixed by VEC_
 */
enum vec_error_t  {
	VEC_EPERM = 1,	/* Operation not permitted */
	VEC_ENOENT = 2,	/* No such file or directory */
	VEC_ESRCH = 3,	/* No such process */
	VEC_EINTR = 4,	/* Interrupted system call */
	VEC_EIO = 5,	/* I/O error */
	VEC_ENXIO = 6,	/* No such device or address */
	VEC_E2BIG =	 7,	/* Arg list too long */
	VEC_ENOEXEC = 8,	/* Exec format error */
	VEC_EBADF = 9,	/* Bad file number */
	VEC_ECHILD = 10,	/* No child processes */
	VEC_EAGAIN = 11,	/* Try again */
	VEC_ENOMEM = 12,	/* Out of memory */
	VEC_EACCES = 13,	/* Permission denied */
	VEC_EFAULT = 14,	/* Bad address */
	VEC_ENOTBLK = 15,	/* Block device required */
	VEC_EBUSY = 16,	/* Device or resource busy */
	VEC_EEXIST = 17,	/* File exists */
	VEC_EXDEV = 18,	/* Cross-device link */
	VEC_ENODEV = 19,	/* No such device */
	VEC_ENOTDIR = 20,	/* Not a directory */
	VEC_EISDIR = 21,	/* Is a directory */
	VEC_EINVAL = 22,	/* Invalid argument */
	VEC_ENFILE = 23,	/* File table overflow */
	VEC_EMFILE = 24,	/* Too many open files */
	VEC_ENOTTY = 25,	/* Not a typewriter */
	VEC_ETXTBSY = 26,	/* Text file busy */
	VEC_EFBIG = 27,	/* File too large */
	VEC_ENOSPC = 28,	/* No space left on device */
	VEC_ESPIPE = 29,	/* Illegal seek */
	VEC_EROFS = 30,	/* Read-only file system */
	VEC_EMLINK = 31,	/* Too many links */
	VEC_EPIPE = 32,	/* Broken pipe */
	VEC_EDOM = 33,	/* Math argument out of domain of func */
	VEC_ERANGE = 34,	/* Math result not representable */
	VEC_EDEADLK = 35,	/* Resource deadlock would occur */
	VEC_ENAMETOOLONG = 36,	/* File name too long */
	VEC_ENOLCK = 37,	/* No record locks available */
	VEC_ENOSYS = 38,	/* Function not implemented */
	VEC_ENOTEMPTY = 39,	/* Directory not empty */
	VEC_ELOOP = 40,	/* Too many symbolic links encountered */
	VEC_ENOMSG = 42,	/* No message of desired type */
	VEC_EIDRM = 43,	/* Identifier removed */
	VEC_ECHRNG = 44,	/* Channel number out of range */
	VEC_EL2NSYNC = 45,	/* Level 2 not synchronized */
	VEC_EL3HLT	=	46,	/* Level 3 halted */
	VEC_EL3RST	=	47,	/* Level 3 reset */
	VEC_ELNRNG = 48,	/* Link number out of range */
	VEC_EUNATCH = 49,	/* Protocol driver not attached */
	VEC_ENOCSI = 50,	/* No CSI structure available */
	VEC_EL2HLT	=	51,	/* Level 2 halted */
	VEC_EBADE = 52,	/* Invalid exchange */
	VEC_EBADR = 53,	/* Invalid request descriptor */
	VEC_EXFULL = 54,	/* Exchange full */
	VEC_ENOANO = 55,	/* No anode */
	VEC_EBADRQC = 56,	/* Invalid request code */
	VEC_EBADSLT = 57,	/* Invalid slot */
	VEC_EBFONT = 59,	/* Bad font file format */
	VEC_ENOSTR = 60,	/* Device not a stream */
	VEC_ENODATA = 61,	/* No data available */
	VEC_ETIME = 62,	/* Timer expired */
	VEC_ENOSR = 63,	/* Out of streams resources */
	VEC_ENONET = 64,	/* Machine is not on the network */
	VEC_ENOPKG = 65,	/* Package not installed */
	VEC_EREMOTE = 66,	/* Object is remote */
	VEC_ENOLINK = 67,	/* Link has been severed */
	VEC_EADV = 68,	/* Advertise error */
	VEC_ESRMNT = 69,	/* Srmount error */
	VEC_ECOMM = 70,	/* Communication error on send */
	VEC_EPROTO = 71,	/* Protocol error */
	VEC_EMULTIHOP = 72,	/* Multihop attempted */
	VEC_EDOTDOT = 73,	/* RFS specific error */
	VEC_EBADMSG = 74,	/* Not a data message */
	VEC_EOVERFLOW = 75,	/* Value too large for defined data type */
	VEC_ENOTUNIQ = 76,	/* Name not unique on network */
	VEC_EBADFD = 77,	/* File descriptor in bad state */
	VEC_EREMCHG = 78,	/* Remote address changed */
	VEC_ELIBACC = 79,	/* Can not access a needed shared library */
	VEC_ELIBBAD = 80,	/* Accessing a corrupted shared library */
	VEC_ELIBSCN = 81,	/* .lib section in a.out corrupted */
	VEC_ELIBMAX = 82,	/* Attempting to link in too many shared libraries */
	VEC_ELIBEXEC = 83,	/* Cannot exec a shared library directly */
	VEC_EILSEQ = 84,	/* Illegal byte sequence */
	VEC_ERESTART = 85,	/* Interrupted system call should be restarted */
	VEC_ESTRPIPE = 86,	/* Streams pipe error */
	VEC_EUSERS = 87,	/* Too many users */
	VEC_ENOTSOCK = 88,	/* Socket operation on non-socket */
	VEC_EDESTADDRREQ = 89,	/* Destination address required */
	VEC_EMSGSIZE = 90,	/* Message too long */
	VEC_EPROTOTYPE = 91,	/* Protocol wrong type for socket */
	VEC_ENOPROTOOPT = 92,	/* Protocol not available */
	VEC_EPROTONOSUPPORT = 93,	/* Protocol not supported */
	VEC_ESOCKTNOSUPPORT = 94,	/* Socket type not supported */
	VEC_EOPNOTSUPP = 95,	/* Operation not supported on transport endpoint */
	VEC_EPFNOSUPPORT = 96,	/* Protocol family not supported */
	VEC_EAFNOSUPPORT = 97,	/* Address family not supported by protocol */
	VEC_EADDRINUSE = 98,	/* Address already in use */
	VEC_EADDRNOTAVAIL = 99,	/* Cannot assign requested address */
	VEC_ENETDOWN = 100,	/* Network is down */
	VEC_ENETUNREACH = 101,	/* Network is unreachable */
	VEC_ENETRESET = 102,	/* Network dropped connection because of reset */
	VEC_ECONNABORTED = 103,	/* Software caused connection abort */
	VEC_ECONNRESET = 104,	/* Connection reset by peer */
	VEC_ENOBUFS = 105,	/* No buffer space available */
	VEC_EISCONN = 106,	/* Transport endpoint is already connected */
	VEC_ENOTCONN = 107,	/* Transport endpoint is not connected */
	VEC_ESHUTDOWN = 108,	/* Cannot send after transport endpoint shutdown */
	VEC_ETOOMANYREFS = 109,	/* Too many references: cannot splice */
	VEC_ETIMEDOUT = 110,	/* Connection timed out */
	VEC_ECONNREFUSED = 111,	/* Connection refused */
	VEC_EHOSTDOWN = 112,	/* Host is down */
	VEC_EHOSTUNREACH = 113,	/* No route to host */
	VEC_EALREADY = 114,	/* Operation already in progress */
	VEC_EINPROGRESS = 115,	/* Operation now in progress */
	VEC_ESTALE = 116,	/* Stale NFS file handle */
	VEC_EUCLEAN = 117,	/* Structure needs cleaning */
	VEC_ENOTNAM = 118,	/* Not a XENIX named type file */
	VEC_ENAVAIL = 119,	/* No XENIX semaphores available */
	VEC_EISNAM = 120,	/* Is a named type file */
	VEC_EREMOTEIO = 121,	/* Remote I/O error */
	VEC_EDQUOT = 122,	/* Quota exceeded */

	VEC_ENOMEDIUM = 123,	/* No medium found */
	VEC_EMEDIUMTYPE = 124,	/* Wrong medium type */
	VEC_ECANCELED = 125,	/* Operation Cancelled */
	VEC_ENOKEY = 126,	/* Required key not available */
	VEC_EKEYEXPIRED = 127,	/* Key has expired */
	VEC_EKEYREVOKED = 128,	/* Key has been revoked */
	VEC_EKEYREJECTED = 129	/* Key was rejected by service */
};

struct vec_status_t {
	vstatus_t  	  	vs_status;
	vec_error_t   	vs_error;
};

/* Get request */
struct vec_get_req {
	vec_handle_t	vr_handle;
	vec_mode_t   	vr_mode;
	vec_offset_t 	vr_offset;
	vec_size_t   	vr_size;
    vec_stripe_size_t vr_stripe_size;
    vec_server_count_t vr_nservers;
    /* indicate how many vectors you want */
    int            vr_max_vectors;
	/* optionally indicate if you want greater than a specific vector */
	vec_vectors_t  vr_vector;
};

/* Get response */
struct vec_get_resp {
	vec_status_t 	vr_status;
	/* Set of vectors */
	vec_svectors_t vr_vectors;
};

/* Put request */
struct vec_put_req {
	vec_handle_t   vr_handle;
	vec_mode_t     vr_mode;
	vec_offset_t   vr_offset;
	vec_size_t     vr_size;
    vec_stripe_size_t vr_stripe_size;
    vec_server_count_t vr_nservers;
};

/* Put response */
struct vec_put_resp {
	vec_status_t   vr_status;
	/* get a unique vector to sort out ordering amongst IO servers */
	vec_vectors_t  vr_vector;
};

program VEC_MGR {
	version vec_v1 {
		vec_get_resp   VEC_GET(vec_get_req)     = 1;
		vec_put_resp   VEC_PUT(vec_put_req)     = 2;
	} = 1;
} = 0x20000201; /* Some unique number... that's all */

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */
