
#ifndef __CLIENTLIBRARY_H
#define __CLIENTLIBRARY_H

#include <stdint.h>
#include <time.h>


typedef uint32_t OrangeFS_fsid;
typedef uint32_t OrangeFS_uid;
typedef uint32_t OrangeFS_gid;
typedef uint32_t OrangeFS_permissions;
typedef uint64_t OrangeFS_time;
typedef uint64_t OrangeFS_size;
typedef uint64_t OrangeFS_flags;
typedef uint64_t OrangeFS_ds_position;
typedef void* OrangeFS_handle;
typedef unsigned char *OrangeFS_signature;

#define ORANGEFS_DEFAULT_CREDENTIAL_TIMEOUT (3600)   /* 1 hour */
#define ORANGEFS_NAME_MAX 256
#define ORANGEFS_PATH_MAX 4096
#define MAX_LINKS 256

/* permission bits */
#define OrangeFS_O_EXECUTE (1 << 0)
#define OrangeFS_O_WRITE   (1 << 1)
#define OrangeFS_O_READ    (1 << 2)
#define OrangeFS_G_EXECUTE (1 << 3)
#define OrangeFS_G_WRITE   (1 << 4)
#define OrangeFS_G_READ    (1 << 5)
#define OrangeFS_U_EXECUTE (1 << 6)
#define OrangeFS_U_WRITE   (1 << 7)
#define OrangeFS_U_READ    (1 << 8)
/* no OrangeFS_U_VTX (sticky bit) */
#define OrangeFS_G_SGID    (1 << 10)
#define OrangeFS_U_SUID    (1 << 11)

/* valid permission mask */
#define OrangeFS_PERM_VALID \
(OrangeFS_O_EXECUTE | OrangeFS_O_WRITE | OrangeFS_O_READ | OrangeFS_G_EXECUTE | \
 OrangeFS_G_WRITE | OrangeFS_G_READ | OrangeFS_U_EXECUTE | OrangeFS_U_WRITE | \
 OrangeFS_U_READ | OrangeFS_G_SGID | OrangeFS_U_SUID)

#define OrangeFS_USER_ALL  (OrangeFS_U_EXECUTE|OrangeFS_U_WRITE|OrangeFS_U_READ)
#define OrangeFS_GROUP_ALL (OrangeFS_G_EXECUTE|OrangeFS_G_WRITE|OrangeFS_G_READ)
#define OrangeFS_OTHER_ALL (OrangeFS_O_EXECUTE|OrangeFS_O_WRITE|OrangeFS_O_READ)

#define OrangeFS_ALL_EXECUTE (OrangeFS_U_EXECUTE|OrangeFS_G_EXECUTE|OrangeFS_O_EXECUTE)
#define OrangeFS_ALL_WRITE   (OrangeFS_U_WRITE|OrangeFS_G_WRITE|OrangeFS_O_WRITE)
#define OrangeFS_ALL_READ    (OrangeFS_U_READ|OrangeFS_G_READ|OrangeFS_O_READ)

/** PVFS I/O operation types, used in both system and server interfaces.
 */
enum ORANGEFS_io_type
{
    ORANGEFS_IO_READ  = 1,
    ORANGEFS_IO_WRITE = 2
};

/** Object and attribute types. */
/* If this enum is modified the server parameters related to the precreate pool
 * batch and low threshold sizes may need to be modified  to reflect this 
 * change. Also, the OrangeFS_DS_TYPE_COUNT #define below must be updated */
typedef enum
{
    OrangeFS_TYPE_NONE =              0,
    OrangeFS_TYPE_METAFILE =    (1 << 0),
    OrangeFS_TYPE_DATAFILE =    (1 << 1),
    OrangeFS_TYPE_DIRECTORY =   (1 << 2),
    OrangeFS_TYPE_SYMLINK =     (1 << 3),
    OrangeFS_TYPE_DIRDATA =     (1 << 4),
    OrangeFS_TYPE_INTERNAL =    (1 << 5)   /* for the server's private use */
} OrangeFS_ds_type;

#define decode_OrangeFS_ds_type decode_enum
#define encode_OrangeFS_ds_type encode_enum
#define OrangeFS_DS_TYPE_COUNT      7      /* total number of DS types defined in
                                        * the OrangeFS_ds_type enum */

typedef struct
{
	OrangeFS_uid		userid;			/* user id */
    uint32_t			num_groups;		/* length of group_array */
    OrangeFS_gid*		group_array;   /* groups for which the user is a member */
    char*				issuer;		/* alias of the issuing server */
    OrangeFS_time		timeout;		/* seconds after epoch to time out */
    uint32_t			sig_size;		/* length of the signature in bytes */
    OrangeFS_signature	signature;		/* digital signature */
}OrangeFS_credential;

/** object reference (uniquely refers to a single file, directory, or
    symlink).
*/
typedef struct
{
    OrangeFS_handle handle;
    OrangeFS_fsid fs_id;
    int32_t    __pad1;
} OrangeFS_object_ref;

enum OrangeFS_flowproto_type
{
    DUMP_OFFSETS = 1,
    BMI_CACHE = 2,
    MULTIQUEUE = 3
};
#define FLOWPROTO_DEFAULT MULTIQUEUE

/* supported wire encoding types */
enum ORANGEFS_encoding_type
{
    DIRECT = 1,
    LE_BFIELD = 2,
    XDR = 3
};

/** Holds results of a lookup operation (reference to object). */
/*  if error_path is passed in NULL then nothing returned on error */
/*  otherwise up to error_path_size chars of unresolved path */
/*  segments are passed out in null terminated string */
typedef struct
{
    OrangeFS_object_ref ref;
    char *error_path;       /* on error, the unresolved path segments */
    int error_path_size;    /* size of the buffer provided by the user */
}OrangeFS_sysresp_lookup;

/* these values must correspond to the defined encoding types above */
#define ENCODING_INVALID_MIN                    0
#define ENCODING_INVALID_MAX                    4
#define ENCODING_SUPPORTED_MIN ENCODING_LE_BFIELD
#define ENCODING_SUPPORTED_MAX ENCODING_LE_BFIELD
#define ENCODING_IS_VALID(enc_type)      \
((enc_type > ENCODING_INVALID_MIN) &&    \
 (enc_type < ENCODING_INVALID_MAX))
#define ENCODING_IS_SUPPORTED(enc_type)  \
((enc_type >= ENCODING_SUPPORTED_MIN) && \
 (enc_type <= ENCODING_SUPPORTED_MAX))
#define ORANGEFS_ENCODING_DEFAULT ENCODING_LE_BFIELD

/* figure out the size of a pointer */
#if defined(__WORDSIZE)
  #define ORANGEFS_SIZEOF_VOIDP __WORDSIZE
#elif defined(BITS_PER_LONG)
  #define ORANGEFS_SIZEOF_VOIDP BITS_PER_LONG
#elif defined(INTPTR_MIN)
  #if   INTPTR_MIN == INT32_MIN
    #define ORANGEFS_SIZEOF_VOIDP 32
  #elif INTPTR_MIN == INT64_MIN
    #define ORANGEFS_SIZEOF_VOIDP 64
  #endif
#elif defined(_WIN64)
  #define ORANGEFS_SIZEOF_VOIDP 64
#elif defined(WIN32)
  #define ORANGEFS_SIZEOF_VOIDP 32
#else
  #error "Unhandled size of void pointer"
#endif

/* we need to align some variables in 32bit case to match alignment
 * in 64bit case
 */
#if ORANGEFS_SIZEOF_VOIDP == 32
#define ORANGEFS_ALIGN_VAR(_type, _name) \
    _type _name; \
    int32_t __pad##_name
#else
#define ORANGEFS_ALIGN_VAR(_type, _name) _type _name
#endif

typedef struct
{
    char **orangefs_config_servers;	/**< addresses of servers with config info */
    int32_t num_orangefs_config_servers; /**< changed to int32_t so that size of structure does not change */
    char *first_orangefs_config_server; /**< first of the entries above that works */
    char *orangefs_fs_name;		/**< name of PVFS2 file system */
    enum OrangeFS_flowproto_type flowproto;	/**< flow protocol */
    enum OrangeFS_encoding_type encoding;   /**< wire data encoding */
    OrangeFS_fsid fs_id; /**< fs id, filled in by system interface when it looks up the fs */
    /* int32_t for portable, fixed size structure */
    int32_t default_num_dfiles; /**< Default number of dfiles mount option value */
    int32_t integrity_check; /**< Check to determine whether the mount process must perform the integrity checks on the config files */
    /* the following fields are included for convenience;
     * useful if the file system is "mounted" */
    char *mnt_dir;		/**< local mount path */
    char *mnt_opts;		/**< full option list */
}OrangeFS_mntent;

/** Describes attributes for a file, directory, or symlink. */
typedef struct 
{
    OrangeFS_uid owner;
    OrangeFS_gid group;
    ORANGEFS_ALIGN_VAR(OrangeFS_permissions, perms);
    OrangeFS_time atime;
    OrangeFS_time mtime;
    OrangeFS_time ctime;
    OrangeFS_size size;
    ORANGEFS_ALIGN_VAR(char *, link_target);/**< NOTE: caller must free if valid */
    ORANGEFS_ALIGN_VAR(int32_t, dfile_count); /* Changed to int32_t so that size of structure does not change */
    ORANGEFS_ALIGN_VAR(uint32_t, mirror_copies_count);
    ORANGEFS_ALIGN_VAR(char*, dist_name);   /**< NOTE: caller must free if valid */
    ORANGEFS_ALIGN_VAR(char*, dist_params); /**< NOTE: caller must free if valid */
    OrangeFS_size dirent_count;
    OrangeFS_ds_type objtype;
    OrangeFS_flags flags;
    uint32_t mask;
    OrangeFS_size blksize;
}OrangeFS_attr;

int _declspec(dllexport) orangefs_init(const char *fs_uri, const char *mount_point, OrangeFS_fsid *fsid, char *error_msg, size_t error_msg_len, const char *tabfile);
int _declspec(dllexport) orangefs_load_tabfile(const char *path, OrangeFS_mntent **mntents, char *error_msg, size_t error_msg_len);
int _declspec(dllexport) orangefs_lookup(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, char *path, OrangeFS_handle *handle);
int _declspec(dllexport) orangefs_lookup_follow_links(OrangeFS_fsid fs_id, OrangeFS_credential *cred, char *fs_path, OrangeFS_sysresp_lookup *resp, OrangeFS_attr *attr);
int _declspec(dllexport) orangefs_get_symlink_attr(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, char *fs_path, char *target, OrangeFS_object_ref *ref, OrangeFS_attr *attr);
int _declspec(dllexport) orangefs_create(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, char *path, unsigned perms, OrangeFS_handle *handle);
int _declspec(dllexport) orangefs_create_h(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, OrangeFS_handle parent_handle, char *name, unsigned perms, OrangeFS_handle *handle);
int _declspec(dllexport) orangefs_remove(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, char *path);
int _declspec(dllexport) orangefs_remove_h(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, OrangeFS_handle handle, char *name);
int _declspec(dllexport) orangefs_rename(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, char *old_path, char *new_path);
int _declspec(dllexport) orangefs_getattr(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, char *path, OrangeFS_attr *attr);
int _declspec(dllexport) orangefs_setattr(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, char *fs_path, OrangeFS_attr *attr);
int _declspec(dllexport) orangefs_mkdir(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, char *fs_path, OrangeFS_handle *handle, unsigned perms);
int _declspec(dllexport) orangefs_io(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, enum ORANGEFS_io_type io_type, char *fs_path, void *buffer, size_t buffer_len, uint64_t offset, OrangeFS_size *op_len);
int _declspec(dllexport) orangefs_flush(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, char *fs_path);
int _declspec(dllexport) orangefs_find_files(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, char *fs_path, OrangeFS_ds_position *token, int32_t incount, int32_t *outcount, char **filename_array, OrangeFS_attr *attr_array);
int _declspec(dllexport) orangefs_get_diskfreespace(OrangeFS_fsid *fs_id, OrangeFS_credential *cred, OrangeFS_size *free_bytes, OrangeFS_size *total_bytes);
int _declspec(dllexport) orangefs_finalize();
int _declspec(dllexport) orangefs_credential_init(OrangeFS_credential *cred);
int _declspec(dllexport) orangefs_credential_set_user(OrangeFS_credential *cred, OrangeFS_uid uid);
void _declspec(dllexport) orangefs_credential_add_group(OrangeFS_credential *cred, OrangeFS_gid gid);
void _declspec(dllexport) orangefs_cleanup_credentials(OrangeFS_credential *cred);
int _declspec(dllexport) orangefs_credential_in_group(OrangeFS_credential *cred, OrangeFS_gid group);
void _declspec(dllexport) orangefs_credential_set_timeout(OrangeFS_credential *cred, OrangeFS_time timeout);

#endif

