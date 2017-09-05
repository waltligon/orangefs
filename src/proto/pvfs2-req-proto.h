/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
/* NOTE: if you make any changes to the code contained in this file, please
 * update the PVFS2_PROTO_VERSION accordingly
 */

#ifndef __PVFS2_REQ_PROTO_H
#define __PVFS2_REQ_PROTO_H

#include "pvfs2-internal.h"
#include "pvfs2-types.h"
#include "pvfs2-attr.h"
#include "pint-distribution.h"
#include "pvfs2-request.h"
#include "pint-request.h"
#include "pvfs2-mgmt.h"
#include "pint-hint.h"
#include "pint-uid-mgmt.h"
#include "pint-security.h"
#include "security-util.h"

/* update PVFS2_PROTO_MAJOR on wire protocol changes that break backwards
 * compatibility (such as changing the semantics or protocol fields for an
 * existing request type)
 */
#define PVFS2_PROTO_MAJOR 7
/* update PVFS2_PROTO_MINOR on wire protocol changes that preserve backwards
 * compatibility (such as adding a new request type)
 * NOTE: Incrementing this will make clients unable to talk to older servers.
 * Do not change until we have a new version policy.
 */
#define PVFS2_PROTO_MINOR 0

#define PVFS2_PROTO_VERSION ((PVFS2_PROTO_MAJOR*1000)+(PVFS2_PROTO_MINOR))

/* We set the maximum possible size of a small I/O packed message as 64K.
 * This is an upper limit that is used to allocate the request and response
 * encoded buffers, and is independent of the max unexpected message size
 * of the specific BMI module. 
 * All max unexpected message sizes for BMI modules have to be less
 * than this value.
 */
#define PINT_SMALL_IO_MAXSIZE (16*1024)

enum PVFS_server_op
{
    PVFS_SERV_INVALID = 0,
    PVFS_SERV_CREATE = 1,
    PVFS_SERV_REMOVE = 2,
    PVFS_SERV_IO = 3,
    PVFS_SERV_GETATTR = 4,
    PVFS_SERV_SETATTR = 5,
    PVFS_SERV_LOOKUP_PATH = 6,
    PVFS_SERV_CRDIRENT = 7,
    PVFS_SERV_RMDIRENT = 8,
    PVFS_SERV_CHDIRENT = 9,
    PVFS_SERV_TRUNCATE = 10,
    PVFS_SERV_MKDIR = 11,
    PVFS_SERV_READDIR = 12,
    PVFS_SERV_GETCONFIG = 13,
    PVFS_SERV_WRITE_COMPLETION = 14,
    PVFS_SERV_FLUSH = 15,
    PVFS_SERV_MGMT_SETPARAM = 16,
    PVFS_SERV_MGMT_NOOP = 17,
    PVFS_SERV_STATFS = 18,
    PVFS_SERV_PERF_UPDATE = 19,  /* not a real protocol request */
    PVFS_SERV_MGMT_PERF_MON = 20,
    PVFS_SERV_MGMT_ITERATE_HANDLES = 21,
    PVFS_SERV_MGMT_DSPACE_INFO_LIST = 22,
    PVFS_SERV_MGMT_EVENT_MON = 23,
    PVFS_SERV_MGMT_REMOVE_OBJECT = 24,
    PVFS_SERV_MGMT_REMOVE_DIRENT = 25,
    PVFS_SERV_MGMT_GET_DIRDATA_HANDLE = 26,
    PVFS_SERV_JOB_TIMER = 27,    /* not a real protocol request */
    PVFS_SERV_PROTO_ERROR = 28,
    PVFS_SERV_GETEATTR = 29,
    PVFS_SERV_SETEATTR = 30,
    PVFS_SERV_DELEATTR = 31,
    PVFS_SERV_LISTEATTR = 32,
    PVFS_SERV_SMALL_IO = 33,
    PVFS_SERV_LISTATTR = 34,
    PVFS_SERV_BATCH_CREATE = 35,
    PVFS_SERV_BATCH_REMOVE = 36,
    PVFS_SERV_PRECREATE_POOL_REFILLER = 37, /* not a real protocol request */
    PVFS_SERV_UNSTUFF = 38,
    PVFS_SERV_MIRROR = 39,
    PVFS_SERV_IMM_COPIES = 40,
    PVFS_SERV_TREE_REMOVE = 41,
    PVFS_SERV_TREE_GET_FILE_SIZE = 42,
    PVFS_SERV_MGMT_GET_UID = 43,
    PVFS_SERV_TREE_SETATTR = 44,
    PVFS_SERV_MGMT_GET_DIRENT = 45,
    PVFS_SERV_MGMT_SPLIT_DIRENT = 46,
    PVFS_SERV_ATOMICEATTR = 47,
    PVFS_SERV_GET_CONFIG = 48,
    PVFS_SERV_TREE_GETATTR = 49,
    PVFS_SERV_MGMT_GET_USER_CERT = 50,
    PVFS_SERV_MGMT_GET_USER_CERT_KEYREQ = 51,

    /* leave this entry last */
    PVFS_SERV_NUM_OPS
};

/*
 * These ops must always work, even if the server is in admin mode.
 */
#define PVFS_SERV_IS_MGMT_OP(x)          \
    ((x) == PVFS_SERV_MGMT_SETPARAM      \
  || (x) == PVFS_SERV_MGMT_REMOVE_OBJECT \
  || (x) == PVFS_SERV_MGMT_REMOVE_DIRENT)

#define PVFS_REQ_COPY_CAPABILITY(__cap, __req) \
    { int rc = PINT_copy_capability(&(__cap), &((__req).capability)); \
    assert(rc == 0); }

/******************************************************************/
/* This struct ised used to control the way the server passes requests
 * to replicant servers in Version 3
 */

enum PVFS_server_ctrlval
{
    PVFS_REQ_NONE      = 0,
    /* mode values */
    PVFS_REQ_REPLICATE = 10,
    PVFS_REQ_SINGLE    = 11,
    PVFS_REQ_REPLITREE = 12,
    PVFS_REQ_TREE      = 13,
    /* type values */
    PVFS_REQ_PRIMARY   = 20,
    PVFS_REQ_SECONDARY = 21
};

typedef struct PVFS_server_control_s
{
    uint8_t mode; /* replicated write or not */
    uint8_t type; /* primary or secondary */
    uint8_t __pad1;
    uint8_t __pad2;
} PVFS_server_control;

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_server_control(pptr, x)      \
do {                                             \
    encode_uint8_t(pptr, &(x)->mode);            \
    encode_uint8_t(pptr, &(x)->type);            \
    *pptr += 2;                                  \
}  while(0)

#define decode_PVFS_server_control(pptr, x)      \
do {                                             \
    decode_uint8_t(pptr, &(x)->mode);            \
    decode_uint8_t(pptr, &(x)->type);            \
    *pptr += 2;                                  \
} while(0)
#endif

/******************************************************************/
/* these values define limits on the maximum size of variable length
 * parameters used within the request protocol
 */

/* max size of layout information - may include explicit server list */
                                          /* from pvfs2-types.h */
#define PVFS_REQ_LIMIT_LAYOUT             PVFS_SYS_LIMIT_LAYOUT
/* max size of opaque distribution parameters */
#define PVFS_REQ_LIMIT_DIST_BYTES         1024
/* max size of each configuration file transmitted to clients.
 * Note: If you change this value, you should change the $req_limit
 * in pvfs2-genconfig as well. */
#define PVFS_REQ_LIMIT_CONFIG_FILE_BYTES  65536
/* max size of directory entries sent per message when splitting
 * directories. Max message size depends on the network being used. */
#define PVFS_REQ_LIMIT_SPLIT_SIZE_MAX     65536
/* max size of all path strings */
#define PVFS_REQ_LIMIT_PATH_NAME_BYTES    PVFS_PATH_MAX
/* max size of strings representing a single path element */
#define PVFS_REQ_LIMIT_SEGMENT_BYTES      PVFS_SEGMENT_MAX
#define PVFS_REQ_LIMIT_NENTRIES_MAX       (int) (PVFS_REQ_LIMIT_SPLIT_SIZE_MAX / \
(PVFS_REQ_LIMIT_SEGMENT_BYTES + sizeof(PVFS_handle)))
/* max total size of I/O request descriptions */
#define PVFS_REQ_LIMIT_IOREQ_BYTES        8192
/* maximum size of distribution name used for the hints */
#define PVFS_REQ_LIMIT_DIST_NAME          128
/* maxmax count of segments allowed per path lookup (note that this governs 
 * the number of handles and attributes returned in lookup_path responses)
 */
#define PVFS_REQ_LIMIT_PATH_SEGMENT_COUNT   40
/*  count of datafiles associated with a logical file */
#define PVFS_REQ_LIMIT_DFILE_COUNT        1024
#define PVFS_REQ_LIMIT_DFILE_COUNT_IS_VALID(dfile_count) \
((dfile_count > 0) && (dfile_count < PVFS_REQ_LIMIT_DFILE_COUNT))
#define PVFS_REQ_LIMIT_MIRROR_DFILE_COUNT 1024
/* max count of dirent handles associated with a directory */
#define PVFS_REQ_LIMIT_DIRENT_FILE_COUNT 1024
/* max number of handles for which we return attributes */
#define PVFS_REQ_LIMIT_LISTATTR 60
/* max count of directory entries per readdir request */
#define PVFS_REQ_LIMIT_DIRENT_COUNT 512
/* max count of directory entries per readdirplus request */
#define PVFS_REQ_LIMIT_DIRENT_COUNT_READDIRPLUS PVFS_REQ_LIMIT_LISTATTR
/* max number of perf metrics returned by mgmt perf mon op */
#define PVFS_REQ_LIMIT_MGMT_PERF_MON_COUNT 16
/* max number of events returned by mgmt event mon op */
#define PVFS_REQ_LIMIT_MGMT_EVENT_MON_COUNT 2048
/* max number of handles returned by any operation using an array of handles */
#define PVFS_REQ_LIMIT_HANDLES_COUNT PVFS_SYS_LIMIT_HANDLES_COUNT
/* typical numb er of SIDs per handle, there are this number times the
 * number of handles PLUS the handles that can be sent
 * adjust both this ratio and the handles count to get something
 * reasonable - be aware that too many will make requests huge
 */
#define PVFS_REQ_LIMIT_SIDS_RATIO 3
#define PVFS_REQ_LIMIT_SIDS_COUNT (PVFS_REQ_LIMIT_HANDLES_COUNT * \
                                   PVFS_REQ_LIMIT_SIDS_RATIO)
/* max number of handles that can be created at once using batch create */
#define PVFS_REQ_LIMIT_BATCH_CREATE 8192
/* max number of handles returned by mgmt iterate handles op */
#define PVFS_REQ_LIMIT_MGMT_ITERATE_HANDLES_COUNT \
                                   PVFS_REQ_LIMIT_HANDLES_COUNT
/* max number of info list items returned by mgmt dspace info list op */
/* max number of dspace info structs returned by mgmt dpsace info op */
#define PVFS_REQ_LIMIT_MGMT_DSPACE_INFO_LIST_COUNT 1024
/* max number of path elements in a lookup_attr response */
#define PVFS_REQ_LIMIT_MAX_PATH_ELEMENTS  40
/* max number of symlinks to resolve before erroring out */
#define PVFS_REQ_LIMIT_MAX_SYMLINK_RESOLUTION_COUNT 8
/* max number of bytes in the key of a key/value pair including null term */
#define PVFS_REQ_LIMIT_KEY_LEN 128
/* max number of bytes in a value of a key/value/pair */
#define PVFS_REQ_LIMIT_VAL_LEN 4096
/* max number of key/value pairs to set or get in a list operation */
#define PVFS_REQ_LIMIT_KEYVAL_LIST 32
/* max number of bytes in an extended attribute key including null term */
#define PVFS_REQ_LIMIT_EATTR_KEY_LEN    PVFS_MAX_XATTR_NAMELEN
/* max number of bytes in an extended attribute value including null term */
#define PVFS_REQ_LIMIT_EATTR_VAL_LEN    PVFS_MAX_XATTR_VALUELEN
/* max number of keys or key/value pairs to set or get in an operation */
#define PVFS_REQ_LIMIT_EATTR_LIST       PVFS_MAX_XATTR_LISTLEN 
/* max size of security signature (in bytes) */
#define PVFS_REQ_LIMIT_SIGNATURE        PVFS_SYS_LIMIT_SIGNATURE
/* max number of groups in credential array */
#define PVFS_REQ_LIMIT_GROUPS           PVFS_SYS_LIMIT_GROUPS
/* max size of credential/capability issuer (in bytes) */
#define PVFS_REQ_LIMIT_ISSUER           PVFS_SYS_LIMIT_ISSUER
/* max size of a certificate buffer (in bytes) */
#define PVFS_REQ_LIMIT_CERT             PVFS_SYS_LIMIT_CERT
/* max size of a certificate private key (in bytes) */
#define PVFS_REQ_LIMIT_SECURITY_KEY 8192
/* max size of userid/password for cert request (in bytes) */
#define PVFS_REQ_LIMIT_USERID_PWD 256
/* max size of encrypted private key for cert request (in bytes) */
#define PVFS_REQ_LIMIT_ENC_KEY 16384

/* create *********************************************************/
/* - used to create an object.  This creates a metadata handle,
 * a datafile handle, and links the datafile handle to the metadata handle.
 * It also sets the attributes on the metadata. */

/* V3 I don't think we will need either of these any more - WBL */
#if 0
    int32_t num_dfiles_req;
    /* NOTE: leave layout as final field so that we can deal with encoding
     * errors */
    PVFS_sys_layout layout;
#endif
struct PVFS_servreq_create
{
    PVFS_fs_id fs_id;
    PVFS_credential credential;
    PVFS_object_attr attr;
    PVFS_handle handle;            /* metafile */
    int32_t sid_count;             /* number of sids per metadata */
    PVFS_SID *sid_array;           /* sids for metafile - reflexive */
    PVFS_handle *parent;           /* pointer to back pointer handle */
    PVFS_SID *parent_sid_array;    /* sids for back pointer */
    int32_t datafile_count;        /* number of datafiles sent */
    PVFS_handle *datafile_handles; /* array of datafile handles */
    int32_t datafile_sid_count;    /* number of sids per datafile */
    PVFS_SID *datafile_sid_array;  /* sids for datafiles */
};

#if 0
endecode_fields_7a1a_struct(
    PVFS_servreq_create,
    PVFS_fs_id, fs_id,
    skip4,,
    PVFS_credential, credential,
    PVFS_object_attr, attr,
    PVFS_handle, handle,
    PVFS_handle, parent,
    PVFS_SID, parent_sid,
    int32_t, datafile_count,
    PVFS_handle, datafile_handles,
    skip4,,
    int32_t, sid_count, /* this is non-standard this is # per handle */
    PVFS_SID, sid_array);
#endif

/* custom encode/decode macros to deal with OID/SID arrays cleanly */
#if 0

    if ((x)->parent && !PVFS_OID_is_null((x)->parent))                     \
    {                                                                      \
    }                                                                      \
    else                                                                   \
    {                                                                      \
        encode_PVFS_handle((pptr), &PVFS_HANDLE_NULL);                     \
    }                                                                      \

#endif

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
static inline void encode_PVFS_servreq_create(char **pptr,
                                              struct PVFS_servreq_create *x)
{                            
    int i;                                                                 
    encode_PVFS_credential((pptr), &(x)->credential);                      
    encode_PVFS_object_attr((pptr), &(x)->attr);                           
    encode_PVFS_fs_id((pptr), &(x)->fs_id);                                
    encode_uint32_t((pptr), &(x)->sid_count);                              
    encode_uint32_t((pptr), &(x)->datafile_count);                         
    encode_uint32_t((pptr), &(x)->datafile_sid_count);                     
    encode_PVFS_handle((pptr), &(x)->handle);                              
    for (i = 0; i < (x)->sid_count; i++)                                   
    {                                                                      
        encode_PVFS_SID((pptr), &(x)->sid_array[i]);                       
    }                                                                      
    if ((x)->parent && !PVFS_OID_is_null((x)->parent))                     
    {                                                                      
        encode_PVFS_handle((pptr), (x)->parent);                           
        for (i = 0; i < (x)->sid_count; i++)                               
        {                                                                  
            encode_PVFS_SID((pptr), &(x)->parent_sid_array[i]);            
        }                                                                  
    }                                                                      
    else                                                                   
    {                                                                      
        encode_PVFS_handle((pptr), &PVFS_HANDLE_NULL);                     
    }                                                                      
    for (i = 0; i < (x)->datafile_count; i++)                              
    {                                                                      
        encode_PVFS_handle((pptr), &(x)->datafile_handles[i]);             
    }                                                                      
    for (i = 0; i < ((x)->datafile_sid_count * (x)->datafile_count); i++)  
    {                                                                      
        encode_PVFS_SID((pptr), &(x)->datafile_sid_array[i]);              
    }                                                                      
}

static inline void decode_PVFS_servreq_create(char **pptr,
                                              struct PVFS_servreq_create *x)
{                            
    int i;                                                                 
    decode_PVFS_credential((pptr), &(x)->credential);                      
    decode_PVFS_object_attr((pptr), &(x)->attr);                           
    decode_PVFS_fs_id((pptr), &(x)->fs_id);                                
    decode_uint32_t((pptr), &(x)->sid_count);                              
    decode_uint32_t((pptr), &(x)->datafile_count);                         
    decode_uint32_t((pptr), &(x)->datafile_sid_count);                     

    (x)->sid_array = decode_malloc(                                        
                  SASZ((x)->sid_count) +                                   
                  OSASZ(1,(x)->sid_count) +                                
                  OSASZ((x)->datafile_count,(x)->datafile_sid_count));     

    (x)->parent = (PVFS_handle *)((x)->sid_array + (x)->sid_count);        
    (x)->parent_sid_array = (PVFS_SID *)((x)->parent + 1);                 
    (x)->datafile_handles = (PVFS_handle *)((x)->parent_sid_array +        
                                            (x)->sid_count);               

    (x)->datafile_sid_array = (PVFS_SID *)((x)->datafile_handles +         
                                           (x)->datafile_count);           

    decode_PVFS_handle((pptr), &(x)->handle);                              
    for (i = 0; i < (x)->sid_count; i++)                                   
    {                                                                      
        decode_PVFS_SID((pptr), &(x)->sid_array[i]);                       
    }                                                                      
    decode_PVFS_handle((pptr), (x)->parent);                               
    if (!PVFS_OID_is_null((x)->parent))                                    
    {                                                                      
        for (i = 0; i < (x)->sid_count; i++)                               
        {                                                                  
            decode_PVFS_SID((pptr), &(x)->parent_sid_array[i]);            
        }                                                                  
    }                                                                      
    for (i = 0; i < (x)->datafile_count; i++)                              
    {                                                                      
        decode_PVFS_handle((pptr), &(x)->datafile_handles[i]);             
    }                                                                      
    for (i = 0; i < ((x)->datafile_sid_count * (x)->datafile_count); i++)  
    {                                                                      
        decode_PVFS_SID((pptr), &(x)->datafile_sid_array[i]);              
    }
}

static inline void defree_PVFS_servreq_create(struct PVFS_servreq_create *x)
{
    defree_PVFS_credential(&(x)->credential);
    defree_PVFS_object_attr(&(x)->attr);
    decode_free((x)->sid_array);
}
#endif

/* V3 ECQ: Can we delete the layout part of this macro? */
/* V3 ECQ: Is this correct wrt handles and sids? */
#define extra_size_PVFS_servreq_create \
     (extra_size_PVFS_object_attr + \
      extra_size_PVFS_sys_layout + \
      extra_size_PVFS_credential + \
      (PVFS_REQ_LIMIT_HANDLES_COUNT * sizeof(PVFS_handle)) + \
      (PVFS_REQ_LIMIT_SIDS_COUNT * sizeof(PVFS_SID))) 

#define PINT_SERVREQ_CREATE_FILL(__req,                                    \
                                 __cap,                                    \
                                 __cred,                                   \
                                 __attr,                                   \
                                 __fsid,                                   \
                                 __metadata_handle,                        \
                                 __metadata_sid_count,                     \
                                 __metadata_sid_array,                     \
                                 __parent_handle,                          \
                                 __parent_sids,                            \
                                 __datafile_count,                         \
                                 __datafile_handles,                       \
                                 __datafile_sid_count,                     \
                                 __datafile_sid_array,                     \
                                 __hints)                                  \
do {                                                                       \
    int mask;                                                              \
    memset(&(__req), 0, sizeof(__req));                                    \
    (__req).op = PVFS_SERV_CREATE;                                         \
    (__req).ctrl.mode = PVFS_REQ_REPLICATE;                                \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                                  \
    (__req).hints = (__hints);                                             \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));                            \
    (__req).u.create.credential = (__cred);                                \
    (__req).u.create.fs_id = (__fsid);                                     \
    (__req).u.create.handle = *(__metadata_handle);                        \
    (__req).u.create.sid_count = (__metadata_sid_count);                   \
    (__req).u.create.sid_array = (__metadata_sid_array);                   \
    (__req).u.create.parent = (__parent_handle);                           \
    (__req).u.create.parent_sid_array = (__parent_sids);                   \
    (__req).u.create.datafile_count = (__datafile_count);                  \
    (__req).u.create.datafile_handles = (__datafile_handles);              \
    (__req).u.create.datafile_sid_count = (__datafile_sid_count);          \
    (__req).u.create.datafile_sid_array = (__datafile_sid_array);          \
    (__attr).objtype = PVFS_TYPE_METAFILE;                                 \
    mask = (__attr).mask;                                                  \
    (__attr).mask = PVFS_ATTR_COMMON_ALL;                                  \
    (__attr).mask |= PVFS_ATTR_SYS_TYPE;                                   \
    PINT_copy_object_attr(&(__req).u.create.attr, &(__attr));              \
    (__req).u.create.attr.mask |= mask;                                    \
} while (0)

struct PVFS_servresp_create
{
    PVFS_capability capability;
    PVFS_object_attr metafile_attrs;
};
endecode_fields_2_struct(
    PVFS_servresp_create,
    PVFS_capability, capability,
    PVFS_object_attr, metafile_attrs);
#define extra_size_PVFS_servresp_create extra_size_PVFS_capability

/* batch_create *********************************************************/
/* - used to create new multiple metafile and datafile objects */

struct PVFS_servreq_batch_create
{
    PVFS_ds_type object_type;
    PVFS_fs_id fs_id;
    PVFS_handle parent_oid;
    PVFS_SID parent_sid;
    uint32_t handle_count;
    PVFS_handle *handle_array;
    uint32_t sid_count;
    PVFS_SID *sid_array;
};
endecode_fields_4a_struct(
    PVFS_servreq_batch_create,
    PVFS_ds_type, object_type,
    PVFS_handle, parent_oid,
    PVFS_SID, parent_sid,
    PVFS_fs_id, fs_id,
    uint32_t, handle_count,
    PVFS_handle, handle_array);

#define extra_size_PVFS_servreq_batch_create \
    (PVFS_REQ_LIMIT_HANDLES_COUNT * sizeof(PVFS_handle_extent))

#define PINT_SERVREQ_BATCH_CREATE_FILL(__req,               \
                                       __cap,               \
                                       __obj_type,          \
                                       __fsid,              \
                                       __parent_oid,        \
                                       __parent_sid,        \
                                       __handle_count,      \
                                       __handle_array,      \
                                       __hints)             \
do {                                                        \
    memset(&(__req), 0, sizeof(__req));                     \
    (__req).op = PVFS_SERV_BATCH_CREATE;                    \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;                 \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                   \
    (__req).hints = (__hints);                              \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));             \
    (__req).u.batch_create.fs_id = (__fsid);                \
    (__req).u.batch_create.parent_oid = (__parent_oid);     \
    (__req).u.batch_create.parent_sid = (__parent_sid);     \
    (__req).u.batch_create.handle_count = (__handle_count); \
    (__req).u.batch_create.handle_array = (__handle_array); \
} while (0)

struct PVFS_servresp_batch_create
{
    int32_t handle_count;
    PVFS_handle *handle_array;
};
endecode_fields_1a_struct(
    PVFS_servresp_batch_create,
    skip4,,
    int32_t, handle_count,
    PVFS_handle, handle_array);

/* remove *****************************************************/
/* - used to remove an existing metafile or datafile object */

struct PVFS_servreq_remove
{
    PVFS_handle handle;
    PVFS_fs_id  fs_id;
    int32_t sid_count; /* reflexive */
    PVFS_SID *sid_array; /* reflexive */
    PVFS_credential credential;
};
endecode_fields_3a_struct(
    PVFS_servreq_remove,
    PVFS_credential, credential,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    int32_t, sid_count,
    PVFS_SID, sid_array);

#define PINT_SERVREQ_REMOVE_FILL(__req,         \
                                 __cap,         \
                                 __cred,        \
                                 __fsid,        \
                                 __handle,      \
                                 __sid_count,   \
                                 __sid_array,   \
                                 __hints)       \
do {                                            \
    memset(&(__req), 0, sizeof(__req));         \
    (__req).op = PVFS_SERV_REMOVE;              \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;     \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;       \
    (__req).capability = (__cap);               \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req)); \
    (__req).u.remove.credential = (__cred);     \
    (__req).hints = (__hints);                  \
    (__req).u.remove.fs_id = (__fsid);          \
    (__req).u.remove.handle = (__handle);       \
    (__req).u.remove.sid_count = (__sid_count); \
    (__req).u.remove.sid_array = (__sid_array); \
} while (0)

/* batche remove ********************************************/
/* - used to remove an existing metafile or datafile object */

struct PVFS_servreq_batch_remove
{
    PVFS_fs_id  fs_id;
    int32_t handle_count;
    PVFS_handle *handles;
};
endecode_fields_1a_struct(
    PVFS_servreq_batch_remove,
    PVFS_fs_id, fs_id,
    int32_t, handle_count,
    PVFS_handle, handles);
#define extra_size_PVFS_servreq_batch_remove \
                     (PVFS_REQ_LIMIT_HANDLES_COUNT * sizeof(PVFS_handle))

#define PINT_SERVREQ_BATCH_REMOVE_FILL(__req,        \
                                       __cap,        \
                                       __fsid,       \
                                       __count,      \
                                       __handles)    \
do {                                                 \
    memset(&(__req), 0, sizeof(__req));              \
    (__req).op = PVFS_SERV_BATCH_REMOVE;             \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;          \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;            \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));      \
    (__req).u.batch_remove.fs_id = (__fsid);         \
    (__req).u.batch_remove.handle_count = (__count); \
    (__req).u.batch_remove.handles = (__handles);    \
} while (0)

/* mgmt_remove_object */
/* - used to remove an existing object reference */

struct PVFS_servreq_mgmt_remove_object
{
    PVFS_handle handle;
    PVFS_fs_id fs_id;
    int32_t sid_count; /* reflexive */
    PVFS_SID *sid_array; /* reflexive */
};
endecode_fields_2a_struct(
    PVFS_servreq_mgmt_remove_object,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    int32_t, sid_count,
    PVFS_SID, sid_array);

#define PINT_SERVREQ_MGMT_REMOVE_OBJECT_FILL(__req,         \
                                             __cap,         \
                                             __fsid,        \
                                             __handle,      \
                                             __sid_count,   \
                                             __sid_array,   \
                                             __hints)       \
do {                                                        \
    memset(&(__req), 0, sizeof(__req));                     \
    (__req).op = PVFS_SERV_MGMT_REMOVE_OBJECT;              \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;                 \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                   \
    (__req).hints = (__hints);                              \
    (__req).capability = (__cap);                           \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));             \
    (__req).u.mgmt_remove_object.fs_id = (__fsid);          \
    (__req).u.mgmt_remove_object.handle = (__handle);       \
    (__req).u.mgmt_remove_object.sid_count = (__sid_count); \
    (__req).u.mgmt_remove_object.sid_array = (__sid_array); \
} while (0)

/* mgmt_remove_dirent */
/* - used to remove an existing dirent under the specified parent ref */

struct PVFS_servreq_mgmt_remove_dirent
{
    PVFS_handle handle;        /* metadata for split */
    PVFS_handle dirent_handle; /* bucket */
    PVFS_fs_id fs_id;
    int32_t sid_count;         /* reflexive */
    PVFS_SID *sid_array;       /* reflexive */
    char *entry;
};
endecode_fields_4a_struct(
    PVFS_servreq_mgmt_remove_dirent,
    string, entry,
    PVFS_handle, handle,
    PVFS_handle, dirent_handle,
    PVFS_fs_id, fs_id,
    int32_t, sid_count,
    PVFS_SID, sid_array);
#define extra_size_PVFS_servreq_mgmt_remove_dirent \
                            (roundup8(PVFS_REQ_LIMIT_SEGMENT_BYTES + 1) + \
                             PVFS_REQ_LIMIT_SIDS_COUNT * sizeof(PVFS_SID))

#define PINT_SERVREQ_MGMT_REMOVE_DIRENT_FILL(__req,         \
                                             __cap,         \
                                             __fsid,        \
                                             __handle,      \
                                             __dirent_handle,\
                                             __sid_count,   \
                                             __sid_array,   \
                                             __entry,       \
                                             __hints)       \
do {                                                        \
    memset(&(__req), 0, sizeof(__req));                     \
    (__req).op = PVFS_SERV_MGMT_REMOVE_DIRENT;              \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;                 \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                   \
    (__req).capability = (__cap);                           \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));             \
    (__req).hints = (__hints);                              \
    (__req).u.mgmt_remove_dirent.fs_id = (__fsid);          \
    (__req).u.mgmt_remove_dirent.handle = (__handle);       \
    (__req).u.mgmt_remove_dirent.dirent_handle = (__dirent_handle); \
    (__req).u.mgmt_remove_dirent.sid_count = (__sid_count); \
    (__req).u.mgmt_remove_dirent.sid_array = (__sid_array); \
    (__req).u.mgmt_remove_dirent.entry = (__entry);         \
} while (0)

/* tree setattr request *********************************************/

struct PVFS_servreq_tree_setattr
{
    PVFS_fs_id fs_id;
    PVFS_credential credential;
    PVFS_ds_type objtype;
    PVFS_object_attr attr;      /* new attributes */
    uint32_t caller_handle_index;
    uint32_t handle_count;      /* # of servers to send setattr msg */
    PVFS_handle *handle_array;  /* handles indicating where to send msgs */
    uint32_t sid_count;         /* reflexive */
    PVFS_SID *sid_array;        /* reflexive */
};
endecode_fields_4a1a_struct(
    PVFS_servreq_tree_setattr,
    PVFS_credential, credential,
    PVFS_ds_type, objtype,
    PVFS_object_attr, attr,
    uint32_t, caller_handle_index,
    uint32_t, handle_count,
    PVFS_handle, handle_array,
    PVFS_fs_id, fs_id,
    uint32_t, sid_count,
    PVFS_handle, sid_array);
#define extra_size_PVFS_servreq_tree_setattr \
                ((PVFS_REQ_LIMIT_HANDLES_COUNT * sizeof(PVFS_handle)) + \
                 (PVFS_REQ_LIMIT_SIDS_COUNT * sizeof(PVFS_SID)) + \
                 extra_size_PVFS_object_attr)

#define PINT_SERVREQ_TREE_SETATTR_FILL(__req,                            \
                                       __cap,                            \
                                       __cred,                           \
                                       __fsid,                           \
                                       __objtype,                        \
                                       __attr,                           \
                                       __caller_handle_index,            \
                                       __handle_count,                   \
                                       __handle_array,                   \
                                       __sid_count,                      \
                                       __sid_array,                      \
                                       __hints)                          \
do {                                                                     \
    memset(&(__req), 0, sizeof(__req));                                  \
    (__req).op = PVFS_SERV_TREE_SETATTR;                                 \
    (__req).ctrl.mode = PVFS_REQ_REPLITREE;                              \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                                \
    (__req).hints = (__hints);                                           \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));                          \
    (__req).u.tree_setattr.credential = (__cred);                        \
    (__req).u.tree_setattr.fs_id = (__fsid);                             \
    (__req).u.tree_setattr.objtype = (__objtype);                        \
    PINT_copy_object_attr(&(__req).u.tree_setattr.attr, &(__attr));      \
    (__req).u.tree_setattr.caller_handle_index = (__caller_handle_index); \
    (__req).u.tree_setattr.handle_count = (__handle_count);              \
    (__req).u.tree_setattr.handle_array = (__handle_array);              \
    (__req).u.tree_setattr.sid_count = (__sid_count);                    \
    (__req).u.tree_setattr.sid_array = (__sid_array);                    \
} while (0)

struct PVFS_servresp_tree_setattr
{
    uint32_t caller_handle_index;
    uint32_t handle_count;
    int32_t *status;
};
endecode_fields_2a_struct(
    PVFS_servresp_tree_setattr,
    skip4,,
    uint32_t, caller_handle_index,
    uint32_t, handle_count,
    int32_t, status);
#define extra_size_PVFS_servresp_tree_setattr \
                (PVFS_REQ_LIMIT_HANDLES_COUNT * sizeof(int32_t))

/* tree remove request *********************************************/

struct PVFS_servreq_tree_remove
{
    PVFS_fs_id  fs_id;
    PVFS_credential credential;
    uint32_t caller_handle_index;
    uint32_t handle_count;
    PVFS_handle *handle_array;
    uint32_t sid_count;
    PVFS_SID *sid_array;
};
endecode_fields_3a1a_struct(
    PVFS_servreq_tree_remove,
    PVFS_fs_id, fs_id,
    PVFS_credential, credential,
    uint32_t, caller_handle_index,
    uint32_t, handle_count,
    PVFS_handle, handle_array,
    skip4,,
    uint32_t, sid_count,
    PVFS_SID, sid_array);
#define extra_size_PVFS_servreq_tree_remove \
  ((PVFS_REQ_LIMIT_HANDLES_COUNT * sizeof(PVFS_handle)) + \
   (PVFS_REQ_LIMIT_SIDS_COUNT * sizeof(PVFS_SID)))

#define PINT_SERVREQ_TREE_REMOVE_FILL(__req,                             \
                                      __cap,                             \
                                      __cred,                            \
                                      __fsid,                            \
                                      __caller_handle_index,             \
                                      __handle_count,                    \
                                      __handle_array,                    \
                                      __sid_count,                       \
                                      __sid_array,                       \
                                      __hints)                           \
do {                                                                     \
    memset(&(__req), 0, sizeof(__req));                                  \
    (__req).op = PVFS_SERV_TREE_REMOVE;                                  \
    (__req).ctrl.mode = PVFS_REQ_REPLITREE;                              \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                                \
    (__req).hints = (__hints);                                           \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));                          \
    (__req).u.tree_remove.credential = (__cred);                         \
    (__req).u.tree_remove.fs_id = (__fsid);                              \
    (__req).u.tree_remove.caller_handle_index = (__caller_handle_index); \
    (__req).u.tree_remove.handle_count = (__handle_count);               \
    (__req).u.tree_remove.handle_array = (__handle_array);               \
    (__req).u.tree_remove.sid_count = (__sid_count);                     \
    (__req).u.tree_remove.sid_array = (__sid_array);                     \
} while (0)

struct PVFS_servresp_tree_remove
{
    uint32_t caller_handle_index;
    uint32_t handle_count;       /* actually the number of status returned */
    int32_t *status;
};
endecode_fields_2a_struct(
    PVFS_servresp_tree_remove,
    skip4,,
    uint32_t, caller_handle_index,
    uint32_t, handle_count,
    int32_t, status);
#define extra_size_PVFS_servresp_tree_remove \
                      (PVFS_REQ_LIMIT_HANDLES_COUNT * sizeof(int32_t))

/* tree get file size request *******************************************/

struct PVFS_servreq_tree_get_file_size
{
    PVFS_fs_id  fs_id;
    uint32_t caller_handle_index;
    uint32_t retry_msgpair_at_leaf;
    PVFS_credential credential;
    uint32_t num_data_files;
    PVFS_handle *handle_array;
    uint32_t sid_count;
    PVFS_SID *sid_array;
};
endecode_fields_3a1a_struct(
    PVFS_servreq_tree_get_file_size,
    uint32_t, caller_handle_index,
    uint32_t, retry_msgpair_at_leaf,
    PVFS_credential, credential,
    uint32_t, num_data_files,
    PVFS_handle, handle_array,
    PVFS_fs_id, fs_id,
    uint32_t, sid_count,
    PVFS_SID, sid_array);
#define extra_size_PVFS_servreq_tree_get_file_size \
    ((PVFS_REQ_LIMIT_HANDLES_COUNT * sizeof(PVFS_handle)) + \
     (PVFS_REQ_LIMIT_SIDS_COUNT * sizeof(PVFS_SID)) + \
     extra_size_PVFS_credential)

#define PINT_SERVREQ_TREE_GET_FILE_SIZE_FILL(__req,                          \
                                             __cap,                          \
                                             __cred,                         \
                                             __fsid,                         \
                                             __caller_handle_index,          \
                                             __num_data_files,               \
                                             __handle_array,                 \
                                             __sid_count,                    \
                                             __sid_array,                    \
                                             __retry_msgpair_at_leaf,        \
                                             __hints)                        \
do {                                                                         \
    memset(&(__req), 0, sizeof(__req));                                      \
    (__req).op = PVFS_SERV_TREE_GET_FILE_SIZE;                               \
    (__req).ctrl.mode = PVFS_REQ_TREE;                                       \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                                    \
    (__req).hints = (__hints);                                               \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));                   \
    (__req).u.tree_get_file_size.credential = (__cred);                      \
    (__req).u.tree_get_file_size.fs_id = (__fsid);                           \
    (__req).u.tree_get_file_size.caller_handle_index = (__caller_handle_index);\
    (__req).u.tree_get_file_size.num_data_files = (__num_data_files);        \
    (__req).u.tree_get_file_size.handle_array = (__handle_array);            \
    (__req).u.tree_get_file_size.sid_count = (__sid_count);                  \
    (__req).u.tree_get_file_size.sid_array = (__sid_array);                  \
    (__req).u.tree_get_file_size.retry_msgpair_at_leaf = (__retry_msgpair_at_leaf);\
} while (0)

struct PVFS_servresp_tree_get_file_size
{
    uint32_t caller_handle_index;
    uint32_t handle_count;
    PVFS_size  *size;
    PVFS_error *error;
};
endecode_fields_1aa_struct(
    PVFS_servresp_tree_get_file_size,
    uint32_t, caller_handle_index,
    uint32_t, handle_count, /* actually number of sizes and errors returned */
    PVFS_size, size,
    PVFS_error, error);
#define extra_size_PVFS_servresp_tree_get_file_size       \
            ( (PVFS_REQ_LIMIT_HANDLES_COUNT * sizeof(PVFS_error)) + \
              (PVFS_REQ_LIMIT_HANDLES_COUNT * sizeof(PVFS_size)) )

struct PVFS_servreq_tree_getattr
{
    PVFS_fs_id  fs_id;
    uint32_t caller_handle_index;
    uint32_t retry_msgpair_at_leaf;
    PVFS_credential credential;
    uint32_t attrmask;
    uint32_t handle_count;
    PVFS_handle *handle_array;
};
endecode_fields_5a_struct(
    PVFS_servreq_tree_getattr,
    PVFS_fs_id, fs_id,
    uint32_t, caller_handle_index,
    uint32_t, retry_msgpair_at_leaf,
    PVFS_credential, credential,
    uint32_t, attrmask,
    uint32_t, handle_count,
    PVFS_handle, handle_array);
#define extra_size_PVFS_servreq_tree_getattr \
    ((PVFS_REQ_LIMIT_HANDLES_COUNT * sizeof(PVFS_handle)) + \
     extra_size_PVFS_credential)

#define PINT_SERVREQ_TREE_GETATTR_FILL(__req,                   \
                                       __cap,                   \
                                       __cred,                  \
                                       __fsid,                  \
                                       __caller_handle_index,   \
                                       __handle_count,          \
                                       __handle_array,          \
                                       __amask,                 \
                                       __retry_msgpair_at_leaf, \
                                       __hints)                 \
do {                                                            \
    memset(&(__req), 0, sizeof(__req));                         \
    (__req).op = PVFS_SERV_TREE_GETATTR;                        \
    (__req).ctrl.mode = PVFS_REQ_TREE;                          \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                       \
    (__req).hints = (__hints);                                  \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));              \
    (__req).u.tree_getattr.credential = (__cred);               \
    (__req).u.tree_getattr.fs_id = (__fsid);                    \
    (__req).u.tree_getattr.caller_handle_index =                \
                                  (__caller_handle_index);      \
    (__req).u.tree_getattr.handle_count =                       \
                                  (__handle_count);             \
    (__req).u.tree_getattr.handle_array = (__handle_array);     \
    (__req).u.tree_getattr.attrmask = (__amask);                \
    (__req).u.tree_getattr.retry_msgpair_at_leaf =              \
                                  (__retry_msgpair_at_leaf);    \
} while (0)

struct PVFS_servresp_tree_getattr
{
    uint32_t caller_handle_index;
    uint32_t handle_count; /* actuall number of attrs and errors returned */
    PVFS_object_attr *attr;
    PVFS_error *error;
};
endecode_fields_1aa_struct(
    PVFS_servresp_tree_getattr,
    uint32_t, caller_handle_index,
    uint32_t, handle_count,
    PVFS_object_attr, attr,
    PVFS_error, error);
/* this is a big thing. Just use the max io req limit */
#define extra_size_PVFS_servresp_tree_getattr \
              (PVFS_REQ_LIMIT_IOREQ_BYTES)

/*#define extra_size_PVFS_servresp_tree_getattr           \
  ( (PVFS_REQ_LIMIT_HANDLES_COUNT * sizeof(PVFS_error)) + \
    (PVFS_REQ_LIMIT_HANDLES_COUNT * (sizeof(PVFS_object_attr) + \
    extra_size_PVFS_object_attr))) */

/* mgmt_get_dirdata_handle */
/* - used to retrieve the dirdata handle of the specified parent ref */
struct PVFS_servreq_mgmt_get_dirdata_handle
{
    PVFS_handle handle;
    PVFS_fs_id fs_id;
};
endecode_fields_2_struct(
    PVFS_servreq_mgmt_get_dirdata_handle,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id);

#define PINT_SERVREQ_MGMT_GET_DIRDATA_HANDLE_FILL(__req,   \
                                                  __cap,   \
                                                  __fsid,  \
                                                  __handle,\
                                                  __hints) \
do {                                                       \
    memset(&(__req), 0, sizeof(__req));                    \
    (__req).op = PVFS_SERV_MGMT_GET_DIRDATA_HANDLE;        \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;                   \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                  \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));            \
    (__req).hints = (__hints);                             \
    (__req).u.mgmt_get_dirdata_handle.fs_id = (__fsid);    \
    (__req).u.mgmt_get_dirdata_handle.handle = (__handle); \
} while (0)

struct PVFS_servresp_mgmt_get_dirdata_handle
{
    /* TJS: figure out how to change the encode thing with WBL */
    PVFS_handle handle;
    uint32_t sid_count;
    PVFS_SID *sid_array;
};
endecode_fields_1a_struct(
    PVFS_servresp_mgmt_get_dirdata_handle,
    PVFS_handle, handle,
    uint32_t, sid_count,
    PVFS_SID, sid_array);
#define extra_size_PVFS_servresp_mgmt_get_dirdata_handle \
                (PVFS_REQ_LIMIT_SIDS_COUNT * sizeof(PVFS_SID))

/* flush
 * - used to flush an object to disk */
struct PVFS_servreq_flush
{
    PVFS_handle handle;
    PVFS_fs_id fs_id;
    int32_t flags;
    uint32_t sid_count;   /* reflexive */
    PVFS_SID *sid_array; /* reflexive */
};
endecode_fields_3a_struct(
    PVFS_servreq_flush,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    int32_t, flags,
    uint32_t, sid_count,
    PVFS_SID, sid_array
    );
#define extra_size_PVFS_servreq_flush \
                (PVFS_REQ_LIMIT_SIDS_COUNT * sizeof(PVFS_SID))

#define PINT_SERVREQ_FLUSH_FILL(__req,         \
                                __cap,         \
                                __fsid,        \
                                __handle,      \
                                __sid_count,   \
                                __sid_array,   \
                                __hints )      \
do {                                           \
    memset(&(__req), 0, sizeof(__req));        \
    (__req).op = PVFS_SERV_FLUSH;              \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;       \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;      \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));\
    (__req).hints = (__hints);                 \
    (__req).u.flush.fs_id = (__fsid);          \
    (__req).u.flush.handle = (__handle);       \
    (__req).u.flush.sid_count = (__sid_count); \
    (__req).u.flush.sid_array = (__sid_array); \
} while (0)

/* getattr ****************************************************/
/* - retreives attributes based on mask of PVFS_ATTR_XXX values */

struct PVFS_servreq_getattr
{
    PVFS_handle handle;           /* handle of target object */
    PVFS_fs_id fs_id;             /* file system */
    uint32_t attrmask;            /* mask of desired attributes */
    PVFS_credential credential;   /* user credential */
};
endecode_fields_4_struct(
    PVFS_servreq_getattr,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    uint32_t, attrmask,
    PVFS_credential, credential);
#define extra_size_PVFS_servreq_getattr extra_size_PVFS_credential

#define PINT_SERVREQ_GETATTR_FILL(__req,             \
                                  __cap,             \
                                  __cred,            \
				  __fsid,            \
                                  __handle,          \
                                  __amask,           \
                                  __hints)           \
do {                                                 \
    memset(&(__req), 0, sizeof(__req));              \
    (__req).op = PVFS_SERV_GETATTR;                  \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;             \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;            \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));      \
    (__req).u.getattr.credential = (__cred);         \
    (__req).hints = (__hints);                       \
    (__req).u.getattr.fs_id = (__fsid);              \
    (__req).u.getattr.handle = (__handle);           \
    (__req).u.getattr.attrmask = (__amask);          \
} while (0)

struct PVFS_servresp_getattr
{
    PVFS_object_attr attr;
};
endecode_fields_1_struct(
    PVFS_servresp_getattr,
    PVFS_object_attr, attr);
#define extra_size_PVFS_servresp_getattr extra_size_PVFS_object_attr

/* unstuff ****************************************************/
/* - creates the datafile handles for the file.  This allows a stuffed
 * file to migrate to a large one. */

struct PVFS_servreq_unstuff
{
    PVFS_handle handle;         /* handle of target object */
    PVFS_fs_id fs_id;           /* file system */
    uint32_t attrmask;          /* mask of desired attributes */
    PVFS_credential credential; /* credential used to get capability */
    uint32_t sid_count;         /* reflexive */
    PVFS_SID *sid_array;        /* reflexive */
};
endecode_fields_5a_struct(
    PVFS_servreq_unstuff,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    uint32_t, attrmask,
    PVFS_credential, credential,
    skip4,,
    uint32_t, sid_count,
    PVFS_SID, sid_array);
#define extra_size_PVFS_servreq_unstuff extra_size_PVFS_credential

#define PINT_SERVREQ_UNSTUFF_FILL(__req,           \
                                  __cap,           \
                                  __cred,          \
                                  __fsid,          \
                                  __handle,        \
                                  __sid_count,     \
                                  __sid_array,     \
                                  __amask)         \
do {                                               \
    memset(&(__req), 0, sizeof(__req));            \
    (__req).op = PVFS_SERV_UNSTUFF;                \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;        \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;          \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));    \
    (__req).u.unstuff.credential = (__cred);       \
    (__req).u.unstuff.fs_id = (__fsid);            \
    (__req).u.unstuff.handle = (__handle);         \
    (__req).u.unstuff.sid_count = (__sid_count);   \
    (__req).u.unstuff.sid_array = (__sid_array);   \
    (__req).u.unstuff.attrmask = (__amask);        \
} while (0)

struct PVFS_servresp_unstuff
{
    /* return the entire object's attributes, which includes the
     * new datafile handles for the migrated file.
     */
    PVFS_object_attr attr;
};
endecode_fields_1_struct(
    PVFS_servresp_unstuff,
    PVFS_object_attr, attr);
#define extra_size_PVFS_servresp_unstuff extra_size_PVFS_object_attr

/* setattr ****************************************************/
/* - sets attributes specified by mask of PVFS_ATTR_XXX values */

struct PVFS_servreq_setattr
{
    PVFS_handle handle;        /* handle of target object */
    PVFS_object_attr attr;     /* new attributes */
    PVFS_credential credential;
    PVFS_fs_id fs_id;          /* file system */
    uint32_t sid_count;        /* reflexive */
    PVFS_SID *sid_array;       /* reflexive */
};
endecode_fields_4a_struct(
    PVFS_servreq_setattr,
    PVFS_handle, handle,
    PVFS_object_attr, attr,
    PVFS_credential, credential,
    PVFS_fs_id, fs_id,
    uint32_t, sid_count,
    PVFS_SID, sid_array);
#define extra_size_PVFS_servreq_setattr \
             (extra_size_PVFS_object_attr + \
              (PVFS_REQ_LIMIT_SIDS_COUNT * sizeof(PVFS_SID)) + \
              extra_size_PVFS_credential)

#define PINT_SERVREQ_SETATTR_FILL(__req,         \
                                  __cap,         \
                                  __cred,        \
                                  __fsid,        \
                                  __handle,      \
                                  __sid_count,   \
                                  __sid_array,   \
                                  __objtype,     \
                                  __attr,        \
                                  __extra_amask, \
                                  __hints)       \
do {                                             \
    memset(&(__req), 0, sizeof(__req));          \
    (__req).op = PVFS_SERV_SETATTR;              \
    (__req).ctrl.mode = PVFS_REQ_REPLICATE;      \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;        \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));  \
    (__req).u.setattr.credential = (__cred);     \
    (__req).hints = (__hints);                   \
    (__req).u.setattr.fs_id = (__fsid);          \
    (__req).u.setattr.handle = (__handle);       \
    (__req).u.setattr.sid_count = (__sid_count); \
    (__req).u.setattr.sid_array = (__sid_array); \
    (__attr).objtype = (__objtype);              \
    (__attr).mask |= PVFS_ATTR_SYS_TYPE;         \
    PINT_CONVERT_ATTR(&(__req).u.setattr.attr, &(__attr), __extra_amask);\
} while (0)

/* lookup path ************************************************/
/* - looks up as many elements of the specified path as possible */
struct PVFS_servreq_lookup_path
{
    char *path;                  /* path name */
    PVFS_fs_id fs_id;            /* file system */
    PVFS_handle handle; /* handle of path parent */
    /* mask of attribs to return with lookup results */
    uint32_t attrmask;
    PVFS_credential credential;
};
endecode_fields_6_struct(
    PVFS_servreq_lookup_path,
    string, path,
    PVFS_fs_id, fs_id,
    skip4,,
    PVFS_handle, handle,
    uint32_t, attrmask,
    PVFS_credential, credential);
#define extra_size_PVFS_servreq_lookup_path         \
                (roundup8(PVFS_REQ_LIMIT_PATH_NAME_BYTES + 1) + \
                 extra_size_PVFS_credential)

#define PINT_SERVREQ_LOOKUP_PATH_FILL(__req,           \
                                      __cap,           \
                                      __cred,          \
                                      __path,          \
                                      __fsid,          \
                                      __handle,        \
                                      __amask,         \
                                      __hints)         \
do {                                                   \
    memset(&(__req), 0, sizeof(__req));                \
    (__req).op = PVFS_SERV_LOOKUP_PATH;                \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;               \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;              \
    (__req).u.lookup_path.credential = (__cred);       \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));        \
    (__req).hints = (__hints);                         \
    (__req).u.lookup_path.path = (__path);             \
    (__req).u.lookup_path.fs_id = (__fsid);            \
    (__req).u.lookup_path.handle = (__handle);         \
    (__req).u.lookup_path.attrmask = (__amask);        \
} while (0)

/* note that all objects in this set should be directories, except
 * for the last one which may be a file.  All directories should
 * have the same number of SIDs - a file system paramter - the
 * last item, if it is a file may have a variable number
 */
struct PVFS_servresp_lookup_path
{
    /* array of handles for each successfully resolved path segment */
    PVFS_handle *handle_array;            

    /* array of attributes for each path segment (when available) */
    PVFS_object_attr *attr_array;

    /* array of sids for all objects */
    PVFS_SID *sid_array;

    uint32_t handle_count; /* # of handles returned */
    uint32_t attr_count;   /* # of attributes returned */
    uint32_t sid_count;    /* # of sids per object * the number of objects
                            * metasidcnt * handle_count */
};
endecode_fields_1a1a1a_struct(
    PVFS_servresp_lookup_path,
    skip4,,
    uint32_t, handle_count,
    PVFS_handle, handle_array,
    skip4,,
    uint32_t, attr_count,
    PVFS_object_attr, attr_array,
    skip4,,
    uint32_t, sid_count,
    PVFS_SID, sid_array);
/* this is a big thing that could be either a full path,
* or lots of handles, just use the max io req limit */
#define extra_size_PVFS_servresp_lookup_path \
                (PVFS_REQ_LIMIT_IOREQ_BYTES)

/* mkdir *******************************************************/
/* - makes a new directory object */

struct PVFS_servreq_mkdir
{
    PVFS_fs_id fs_id;             /* file system */
    PVFS_credential credential;   /* user credential */
    PVFS_object_attr attr;        /* initial attributes */
    PVFS_handle handle;           /* object being created */
    int32_t sid_count;            /* for object being created */
    PVFS_SID *sid_array;          /* sids for object being created */
    PVFS_handle *parent;          /* pointer to back pointer handle */
    PVFS_SID *parent_sid_array;   /* sids for back pointer */
    int32_t dirdata_count;        /* number of dirdata handles */
    PVFS_handle *dirdata_handles; /* array of dirdata handles */
    int32_t dirdata_sid_count;    /* # of sids per dirdata handle */
    PVFS_SID *dirdata_sid_array;  /* sids for dirdata handles */
    int32_t dist_dir_servers_initial; /* initial # of active dirdata handles */
    int32_t dist_dir_split_size;  /* # of dirents to reach for split to occur */
};

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
static inline void encode_PVFS_servreq_mkdir(char **pptr,
                                             struct PVFS_servreq_mkdir *x)
{
    int i;

    encode_PVFS_credential((pptr), &(x)->credential);
    encode_PVFS_object_attr((pptr), &(x)->attr);
    encode_PVFS_fs_id((pptr), &(x)->fs_id);
    encode_uint32_t((pptr), &(x)->sid_count);
    encode_uint32_t((pptr), &(x)->dirdata_count);                      
    encode_uint32_t((pptr), &(x)->dirdata_sid_count);                  
    encode_PVFS_handle((pptr), &(x)->handle);
    for (i = 0; i < (x)->sid_count; i++)
    {
        encode_PVFS_SID((pptr), &(x)->sid_array[i]);
    }
    if ((x)->parent && !PVFS_OID_is_null((x)->parent))
    {
        encode_PVFS_handle((pptr), (x)->parent);
        for (i = 0; i < (x)->sid_count; i++)
        {
            encode_PVFS_SID((pptr), &(x)->parent_sid_array[i]);
        }
    }
    else
    {
        encode_PVFS_handle((pptr), &PVFS_HANDLE_NULL);
    }
    for (i = 0; i < (x)->dirdata_count; i++)
    {
        encode_PVFS_handle((pptr), &(x)->dirdata_handles[i]);
    }
    for (i = 0; i < ((x)->dirdata_sid_count * (x)->dirdata_count); i++)
    {
        encode_PVFS_SID((pptr), &(x)->dirdata_sid_array[i]);
    }
    encode_int32_t((pptr), &(x)->dist_dir_servers_initial);
    encode_int32_t((pptr), &(x)->dist_dir_split_size);
}

static inline void decode_PVFS_servreq_mkdir(char **pptr,
                                             struct PVFS_servreq_mkdir *x)
{
    int i;
    decode_PVFS_credential((pptr), &(x)->credential);
    decode_PVFS_object_attr((pptr), &(x)->attr);
    decode_PVFS_fs_id((pptr), &(x)->fs_id);
    decode_uint32_t((pptr), &(x)->sid_count);
    decode_uint32_t((pptr), &(x)->dirdata_count);
    decode_uint32_t((pptr), &(x)->dirdata_sid_count);
    (x)->sid_array = decode_malloc(
                  SASZ((x)->sid_count) +
                  OSASZ(1,(x)->sid_count) +
                  OSASZ((x)->dirdata_count,(x)->dirdata_sid_count));

    (x)->parent = (PVFS_handle *)((x)->sid_array + (x)->sid_count);
    (x)->parent_sid_array = (PVFS_SID *)((x)->parent + 1);
    (x)->dirdata_handles = (PVFS_handle *)((x)->parent_sid_array +
                                            (x)->sid_count);

    (x)->dirdata_sid_array = (PVFS_SID *)((x)->dirdata_handles +
                                           (x)->dirdata_count);

    decode_PVFS_handle((pptr), &(x)->handle);
    for (i = 0; i < (x)->sid_count; i++)
    {
        decode_PVFS_SID((pptr), &(x)->sid_array[i]);
    }
    decode_PVFS_handle((pptr), (x)->parent);
    if (!PVFS_OID_is_null((x)->parent))
    {
        for (i = 0; i < (x)->sid_count; i++)
        {
            decode_PVFS_SID((pptr), &(x)->parent_sid_array[i]);
        }
    }
    for (i = 0; i < (x)->dirdata_count; i++)
    {
        decode_PVFS_handle((pptr), &(x)->dirdata_handles[i]);
    }
    for (i = 0; i < ((x)->dirdata_sid_count * (x)->dirdata_count); i++)
    {
        decode_PVFS_SID((pptr), &(x)->dirdata_sid_array[i]);
    }
    decode_int32_t((pptr), &(x)->dist_dir_servers_initial);
    decode_int32_t((pptr), &(x)->dist_dir_split_size);
}

static inline void defree_PVFS_servreq_mkdir(struct PVFS_servreq_mkdir *x)
{
    defree_PVFS_credential(&(x)->credential);
    defree_PVFS_object_attr(&(x)->attr);
    decode_free((x)->sid_array);
}
#endif

/* V3 ECQ: Is this correct wrt handles and sids? */
#define extra_size_PVFS_servreq_mkdir \
     (extra_size_PVFS_object_attr + \
      extra_size_PVFS_credential + \
      (PVFS_REQ_LIMIT_HANDLES_COUNT * sizeof(PVFS_handle)) + \
      (PVFS_REQ_LIMIT_SIDS_COUNT * sizeof(PVFS_SID)))

#define PINT_SERVREQ_MKDIR_FILL(__req,                     \
                                __cap,                     \
                                __cred,                    \
                                __attr,                    \
                                __fs_id,                   \
                                __metadata_handle,         \
                                __metadata_sid_count,      \
                                __metadata_sid_array,      \
                                __parent_handle,           \
                                __parent_sids,             \
                                __dirdata_count,           \
                                __dirdata_handles,         \
                                __dirdata_sid_count,       \
                                __dirdata_sid_array,       \
                                __dist_dir_servers_initial,\
                                __dist_dir_split_size,     \
                                __hints)                   \
do {                                                       \
    memset(&(__req), 0, sizeof(__req));                    \
    (__req).op = PVFS_SERV_MKDIR;                          \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;                   \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                  \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));            \
    (__req).u.mkdir.credential = (__cred);                 \
    (__req).u.mkdir.fs_id = (__fs_id);                     \
    (__req).u.mkdir.handle = (__metadata_handle);          \
    (__req).u.mkdir.sid_count = (__metadata_sid_count);    \
    (__req).u.mkdir.sid_array = (__metadata_sid_array);    \
    (__req).u.mkdir.parent = (__parent_handle);            \
    (__req).u.mkdir.parent_sid_array = (__parent_sids);    \
    (__req).u.mkdir.dirdata_count = (__dirdata_count);     \
    (__req).u.mkdir.dirdata_handles = (__dirdata_handles); \
    (__req).u.mkdir.dirdata_sid_count = (__dirdata_sid_count); \
    (__req).u.mkdir.dirdata_sid_array = (__dirdata_sid_array); \
    (__req).u.mkdir.dist_dir_servers_initial =             \
            (__dist_dir_servers_initial);                  \
    (__req).u.mkdir.dist_dir_split_size =                  \
            (__dist_dir_split_size);                       \
    (__attr).objtype = PVFS_TYPE_DIRECTORY;                \
    (__attr).mask   |= PVFS_ATTR_SYS_TYPE;                 \
    PINT_CONVERT_ATTR(&(__req).u.mkdir.attr, &(__attr), 0);\
} while (0)

struct PVFS_servresp_mkdir
{
    PVFS_capability capability;
};

/* V3: TJS: Need to update struct with array */

endecode_fields_1_struct(
    PVFS_servresp_mkdir,
    PVFS_capability, capability);
#define extra_size_PVFS_servresp_mkdir extra_size_PVFS_capability

/* create dirent ***********************************************/
/* - creates a new entry within an existing directory */

struct PVFS_servreq_crdirent
{
    PVFS_credential credential;
    char *name;                  /* stored with new entry */
    PVFS_handle new_handle;      /* stored with new entry */
    PVFS_handle parent_handle;   /* metadata for split */
    PVFS_handle dirdata_handle;  /* handle of directory bucket */
    PVFS_fs_id fs_id;            /* file system */
    int32_t sid_count;           /* reflexive - of bucket */
    int32_t new_sid_count;       /* # of sids in new_sid_array;could be different than parent. */
    PVFS_SID *new_sid_array;     /* stored with new entry */
    PVFS_SID *parent_sid_array;  /* stored with new entry */
    PVFS_SID *dirdata_sid_array; /* reflexive - of bucket */
};
endecode_fields_6a2a_struct(
    PVFS_servreq_crdirent,
    PVFS_credential, credential,
    string, name,
    PVFS_handle, new_handle,
    PVFS_handle, parent_handle,
    PVFS_handle, dirdata_handle,
    PVFS_fs_id, fs_id,
    int32_t, sid_count,
    int32_t, new_sid_count,
    PVFS_SID, new_sid_array,
    PVFS_SID, parent_sid_array,
    PVFS_SID, dirdata_sid_array);
#define extra_size_PVFS_servreq_crdirent \
                    (roundup8(PVFS_REQ_LIMIT_SEGMENT_BYTES + 1) + \
                     (PVFS_REQ_LIMIT_SIDS_COUNT * 3 * sizeof(PVFS_SID)))

#define PINT_SERVREQ_CRDIRENT_FILL(__req,                         \
                                   __cap,                         \
                                   __cred,                        \
                                   __fs_id,                       \
                                   __dirdata_handle,              \
                                   __sid_count,                   \
                                   __dirdata_sid_array,           \
                                   __name,                        \
                                   __new_handle,                  \
                                   __new_sid_array,               \
                                   __new_sid_count,               \
                                   __parent_handle,               \
                                   __parent_sid_array,            \
                                   __hints)                       \
do {                                                              \
    memset(&(__req), 0, sizeof(__req));                           \
    (__req).op = PVFS_SERV_CRDIRENT;                              \
    (__req).ctrl.mode = PVFS_REQ_REPLICATE;                       \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                         \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));                   \
    (__req).u.crdirent.credential = (__cred);                     \
    (__req).hints = (__hints);                                    \
    (__req).u.crdirent.name = (__name);                           \
    (__req).u.crdirent.new_handle = (__new_handle);               \
    (__req).u.crdirent.parent_handle = (__parent_handle);         \
    (__req).u.crdirent.dirdata_handle = (__dirdata_handle);       \
    (__req).u.crdirent.fs_id = (__fs_id);                         \
    (__req).u.crdirent.sid_count = (__sid_count);                 \
    (__req).u.crdirent.new_sid_array = (__new_sid_array);         \
    (__req).u.crdirent.new_sid_count = (__new_sid_count);         \
    (__req).u.crdirent.dirdata_sid_array = (__dirdata_sid_array); \
    (__req).u.crdirent.parent_sid_array = (__parent_sid_array);   \
} while (0)

/* rmdirent ****************************************************/
/* - removes an existing directory entry */

struct PVFS_servreq_rmdirent
{
    char *entry;               /* name of entry to remove */
    PVFS_handle handle;        /* metadata for split */
    PVFS_handle dirent_handle; /* handle of directory bucket */
    PVFS_fs_id fs_id;          /* file system */
    int32_t sid_count;         /* reflexive - of bucket */
    PVFS_SID *sid_array;       /* reflexive - of bucket */
};
endecode_fields_4a_struct(
    PVFS_servreq_rmdirent,
    string, entry,
    PVFS_handle, handle,
    PVFS_handle, dirent_handle,
    PVFS_fs_id, fs_id,
    int32_t, sid_count,
    PVFS_SID, sid_array);
#define extra_size_PVFS_servreq_rmdirent \
                    (roundup8(PVFS_REQ_LIMIT_SEGMENT_BYTES + 1) + \
                     (PVFS_REQ_LIMIT_SIDS_COUNT * sizeof(PVFS_SID)))

/* V3: The state machine doesn't use handle so we can get
       rid of it. The sid_count and sid_array we need will
       be for the dirent_handle. */
#define PINT_SERVREQ_RMDIRENT_FILL(__req,                \
                                   __cap,                \
                                   __fsid,               \
                                   __handle,             \
                                   __sid_count,          \
                                   __sid_array,          \
                                   __dirent_handle,      \
                                   __entry,              \
                                   __hints)              \
do {                                                     \
    memset(&(__req), 0, sizeof(__req));                  \
    (__req).op = PVFS_SERV_RMDIRENT;                     \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;              \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));          \
    (__req).hints = (__hints);                           \
    (__req).u.rmdirent.fs_id = (__fsid);                 \
    (__req).u.rmdirent.handle = (__handle);              \
    (__req).u.rmdirent.sid_count = (__sid_count);        \
    (__req).u.rmdirent.sid_array = (__sid_array);        \
    (__req).u.rmdirent.dirent_handle = (__dirent_handle);\
    (__req).u.rmdirent.entry = (__entry);                \
} while (0);

struct PVFS_servresp_rmdirent
{
    PVFS_handle entry_handle;   /* handle of removed entry */
    int32_t sid_count;          /* number of sids of removed entry */
    PVFS_SID *sid_array;        /* sids of removed entry */
};
endecode_fields_1a_struct(
    PVFS_servresp_rmdirent,
    PVFS_handle, entry_handle,
    int32_t, sid_count,
    PVFS_SID, sid_array);
#define extra_size_PVFS_servresp_rmdirent \
                     (PVFS_REQ_LIMIT_SIDS_COUNT * sizeof(PVFS_SID))

/* chdirent *******************************************************/
/* - modifies an existing directory entry in a particular dirdata */
/*   by writing a new handle and associated SIDs to that dirent   */
/* This is only used when sys-rename.sm notices that the destination
   already exists and the directory entry should be updated in place
   rather than a new one created. */

struct PVFS_servreq_chdirent
{
    char *entry;                   /* name of entry to change */
    PVFS_handle directory_handle;  /* metadataa for split */
    PVFS_handle new_dirent_handle; /* new handle for dirent */
    PVFS_handle dirdata_handle;    /* handle of bucket */
    PVFS_fs_id fs_id;              /* file system */
    uint32_t sid_count;            /* reflexive - bucket */
    PVFS_SID *sid_array;           /* reflexive - bucket */
    uint32_t new_sid_count;        /* new sid for dirent */
    PVFS_SID *new_sid_array;       /* new sid for dirent */
};
endecode_fields_5a1a_struct(
    PVFS_servreq_chdirent,
    string, entry,
    PVFS_handle, new_dirent_handle,
    PVFS_handle, directory_handle,
    PVFS_handle, dirdata_handle,
    PVFS_fs_id, fs_id,
    uint32_t, sid_count,
    PVFS_SID, sid_array,
    skip4,,
    uint32_t, new_sid_count,
    PVFS_SID, new_sid_array);
#define extra_size_PVFS_servreq_chdirent \
                (roundup8(PVFS_REQ_LIMIT_SEGMENT_BYTES + 1) + \
                 (PVFS_REQ_LIMIT_SIDS_COUNT * 2 * sizeof(PVFS_SID)))

/* V3: The state machine doesn't use handle so we can get
       rid of it, sid_count, and sid_array. */
#define PINT_SERVREQ_CHDIRENT_FILL(__req,                            \
                                   __cap,                            \
                                   __fsid,                           \
                                   __directory_handle,               \
                                   __sid_count,                      \
                                   __sid_array,                      \
                                   __dirdata_handle,                 \
                                   __new_dirent_handle,              \
                                   __new_sid_count,                  \
                                   __new_sid_array,                  \
                                   __entry,                          \
                                   __hints)                          \
do {                                                                 \
    memset(&(__req), 0, sizeof(__req));                              \
    (__req).op = PVFS_SERV_CHDIRENT;                                 \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;                          \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                            \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));                      \
    (__req).hints = (__hints);                                       \
    (__req).u.chdirent.fs_id = (__fsid);                             \
    (__req).u.chdirent.directory_handle = (__directory_handle);      \
    (__req).u.chdirent.sid_count = (__sid_count);                    \
    (__req).u.chdirent.sid_array = (__sid_array);                    \
    (__req).u.chdirent.new_dirent_handle = (__new_dirent_handle);    \
    (__req).u.chdirent.dirdata_handle = (__dirdata_handle);          \
    (__req).u.chdirent.new_sid_count = (__new_sid_count);            \
    (__req).u.chdirent.new_sid_array = (__new_sid_array);            \
    (__req).u.chdirent.entry = (__entry);                            \
} while (0);

struct PVFS_servresp_chdirent
{
    PVFS_handle old_dirent_handle;
    int32_t sid_count;
    PVFS_SID *sid_array;
};
endecode_fields_2a_struct(
    PVFS_servresp_chdirent,
    PVFS_handle, old_dirent_handle,
    skip4,,
    int32_t, sid_count,
    PVFS_SID, sid_array);
#define extra_size_PVFS_servresp_chdirent \
                     (PVFS_REQ_LIMIT_SIDS_COUNT * sizeof(PVFS_SID))

/* readdir *****************************************************/
/* - reads entries from a directory */

struct PVFS_servreq_readdir
{
    PVFS_handle handle;            /* handle of dir metadata */
    PVFS_handle dirent_handle;     /* handle of directory bucket */
    PVFS_fs_id fs_id;              /* file system */
    PVFS_ds_position token;        /* dir offset */
    uint32_t dirent_count;         /* desired # of entries */
};
endecode_fields_5_struct(
    PVFS_servreq_readdir,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    uint32_t, dirent_count,
    skip4,,
    PVFS_ds_position, token);

#define PINT_SERVREQ_READDIR_FILL(__req,                \
                                  __cap,                \
                                  __fsid,               \
                                  __handle,             \
                                  __dirent_handle,      \
                                  __token,              \
                                  __dirent_count,       \
                                  __hints)              \
do {                                                    \
    memset(&(__req), 0, sizeof(__req));                 \
    (__req).op = PVFS_SERV_READDIR;                     \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;                \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;               \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));        \
    (__req).hints = (__hints);                          \
    (__req).u.readdir.fs_id = (__fsid);                 \
    (__req).u.readdir.handle = (__handle);              \
    (__req).u.readdir.dirent_handle = (__dirent_handle);\
    (__req).u.readdir.token = (__token);                \
    (__req).u.readdir.dirent_count = (__dirent_count);  \
} while (0);

struct PVFS_servresp_readdir
{
    PVFS_ds_position token;  /* new dir offset */
    /* array of directory entries */
    PVFS_dirent *dirent_array;
    uint32_t dirent_count;   /* # of entries retrieved */
    PVFS_SID *sid_array;
    uint32_t sid_count;
    uint64_t directory_version;
};
endecode_fields_3a1a_struct(
    PVFS_servresp_readdir,
    PVFS_ds_position, token,
    uint64_t, directory_version,
    skip4,,
    uint32_t, dirent_count,
    PVFS_dirent, dirent_array,
    skip4,,
    uint32_t, sid_count,
    PVFS_SID, sid_array);
#define extra_size_PVFS_servresp_readdir \
           (roundup8(PVFS_REQ_LIMIT_DIRENT_COUNT * (PVFS_NAME_MAX + 1 + 8)) + \
            PVFS_REQ_LIMIT_SIDS_COUNT * sizeof(PVFS_SID))

/* getconfig ***************************************************/
/* - retrieves initial configuration information from server */

#define PINT_SERVREQ_GETCONFIG_FILL(__req, __cap, __hints)  \
do {                                                        \
    memset(&(__req), 0, sizeof(__req));                     \
    (__req).op = PVFS_SERV_GETCONFIG;                       \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;                    \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                   \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));             \
    (__req).hints = (__hints);                              \
} while (0);

struct PVFS_servresp_getconfig
{
    char *fs_config_buf;
    uint32_t fs_config_buf_size;
};
endecode_fields_3_struct(
    PVFS_servresp_getconfig,
    uint32_t, fs_config_buf_size,
    skip4,,
    string, fs_config_buf);
#define extra_size_PVFS_servresp_getconfig \
                    (PVFS_REQ_LIMIT_CONFIG_FILE_BYTES)

/* mirror ******************************************************/
/* - copies a datahandle owned by the local server to a data-  */
/*   handle on a remote server. There could be multiple desti- */
/*   nation data handles. dst_count tells us how many there    */
/*   are.                                                      */
struct PVFS_servreq_mirror
{
    PVFS_handle src_handle;
    PVFS_handle *dst_handle;
    PVFS_fs_id  fs_id;
    PINT_dist   *dist;
    uint32_t    bsize;
    uint32_t    src_server_nr;
    uint32_t    *wcIndex;
    uint32_t     dst_count;
    enum PVFS_flowproto_type flow_type;
    enum PVFS_encoding_type encoding;
};

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_servreq_mirror(pptr,x) do {      \
   int i;                                            \
   encode_PVFS_handle(pptr,&(x)->src_handle);        \
   encode_PVFS_fs_id(pptr,&(x)->fs_id);              \
   encode_PINT_dist(pptr,&(x)->dist);                \
   encode_uint32_t(pptr,&(x)->bsize);                \
   encode_uint32_t(pptr,&(x)->src_server_nr);        \
   encode_uint32_t(pptr,&(x)->dst_count);            \
   encode_enum(pptr,&(x)->flow_type);                \
   encode_enum(pptr,&(x)->encoding);                 \
   for (i=0; i<(x)->dst_count; i++)                  \
   {                                                 \
       encode_PVFS_handle(pptr,&(x)->dst_handle[i]); \
       encode_uint32_t(pptr,&(x)->wcIndex[i]);       \
   }                                                 \
} while (0)

#define decode_PVFS_servreq_mirror(pptr,x) do {          \
   int i;                                                \
   decode_PVFS_handle(pptr,&(x)->src_handle);            \
   decode_PVFS_fs_id(pptr,&(x)->fs_id);                  \
   decode_PINT_dist(pptr,&(x)->dist,NULL);               \
   decode_uint32_t(pptr,&(x)->bsize);                    \
   decode_uint32_t(pptr,&(x)->src_server_nr);            \
   decode_uint32_t(pptr,&(x)->dst_count);                \
   decode_enum(pptr,&(x)->flow_type);                    \
   decode_enum(pptr,&(x)->encoding);                     \
   (x)->dst_handle = decode_malloc((x)->dst_count *      \
                                   sizeof(PVFS_handle)); \
   (x)->wcIndex = decode_malloc((x)->dst_count *         \
                               sizeof(uint32_t));        \
   for (i=0; i<(x)->dst_count; i++)                      \
   {                                                     \
       decode_PVFS_handle(pptr,&(x)->dst_handle[i]);     \
       decode_uint32_t(pptr,&(x)->wcIndex[i]);           \
   }                                                     \
} while (0)
#endif

#define extra_size_PVFS_servreq_mirror                      \
   ( (sizeof(PVFS_handle) * PVFS_REQ_LIMIT_HANDLES_COUNT) + \
     (sizeof(uint32_t) * PVFS_REQ_LIMIT_HANDLES_COUNT) )

/*Response to mirror request.  Identifies the number of bytes written and the */
/*status of that write for each source-destination handle pair. (Source is    */
/*always the same for each pair.)                                             */
struct PVFS_servresp_mirror
{
    PVFS_handle src_handle;
    uint32_t src_server_nr;
    uint32_t *bytes_written;
    uint32_t *write_status_code;
    uint32_t dst_count;
};

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_servresp_mirror(pptr,x) do {         \
   int i;                                                \
   encode_PVFS_handle(pptr,&(x)->src_handle);            \
   encode_uint32_t(pptr,&(x)->src_server_nr);            \
   encode_uint32_t(pptr,&(x)->dst_count);                \
   for (i=0; i<(x)->dst_count; i++)                      \
   {                                                     \
       encode_uint32_t(pptr,&(x)->bytes_written[i]);     \
       encode_uint32_t(pptr,&(x)->write_status_code[i]); \
   }                                                     \
} while (0)

#define decode_PVFS_servresp_mirror(pptr,x) do {            \
  int i;                                                    \
  decode_PVFS_handle(pptr,&(x)->src_handle);                \
  decode_uint32_t(pptr,&(x)->src_server_nr);                \
  decode_uint32_t(pptr,&(x)->dst_count);                    \
  (x)->bytes_written     = decode_malloc((x)->dst_count *   \
                                         sizeof(uint32_t)); \
  (x)->write_status_code = decode_malloc((x)->dst_count *   \
                                         sizeof(uint32_t)); \
  for (i=0; i<(x)->dst_count; i++ )                         \
  {                                                         \
      decode_uint32_t(pptr,&(x)->bytes_written[i]);         \
      decode_uint32_t(pptr,&(x)->write_status_code[i]);     \
  }                                                         \
} while (0)
#endif

#define extra_size_PVFS_servresp_mirror                 \
  ( (sizeof(uint32_t) * PVFS_REQ_LIMIT_HANDLES_COUNT) + \
    (sizeof(uint32_t) * PVFS_REQ_LIMIT_HANDLES_COUNT) )


/* truncate ****************************************************/
/* - resizes an existing datafile */

struct PVFS_servreq_truncate
{
    PVFS_handle handle;  /* handle of obj to resize */
    PVFS_fs_id fs_id;    /* file system */
    PVFS_size size;      /* new size */
    int32_t flags;       /* future use */
    int32_t sid_count;   /* reflexive */
    PVFS_SID *sid_array; /* reflexive */
};
endecode_fields_5a_struct(
    PVFS_servreq_truncate,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    skip4,,
    PVFS_size, size,
    int32_t, flags,
    int32_t, sid_count,
    PVFS_SID, sid_array);
#define extra_size_PVFS_serveq_truncate \
                (PVFS_REQ_LIMIT_SIDS_COUNT * sizeof(PVFS_SID))

#define PINT_SERVREQ_TRUNCATE_FILL(__req,        \
                                   __cap,        \
                                   __fsid,       \
                                   __size,       \
                                   __handle,     \
                                   __sid_count,  \
                                   __sid_array,  \
                                   __hints)      \
do {                                             \
    memset(&(__req), 0, sizeof(__req));          \
    (__req).op = PVFS_SERV_TRUNCATE;             \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;      \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;        \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));  \
    (__req).hints = (__hints);                   \
    (__req).u.truncate.fs_id = (__fsid);         \
    (__req).u.truncate.size = (__size);          \
    (__req).u.truncate.handle = (__handle);      \
    (__req).u.truncate.sid_count = (__sid_count);\
    (__req).u.truncate.sid_array = (__sid_array);\
} while (0)

/* statfs ****************************************************/
/* - retrieves statistics for a particular file system */

struct PVFS_servreq_statfs
{
    PVFS_fs_id fs_id;  /* file system */
};
endecode_fields_1_struct(
    PVFS_servreq_statfs,
    PVFS_fs_id, fs_id);

#define PINT_SERVREQ_STATFS_FILL(__req, __cap, __fsid, __hints)\
do {                                                    \
    memset(&(__req), 0, sizeof(__req));                 \
    (__req).op = PVFS_SERV_STATFS;                      \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;                \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;               \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));         \
    (__req).hints = (__hints);                          \
    (__req).u.statfs.fs_id = (__fsid);                  \
} while (0)

struct PVFS_servresp_statfs
{
    PVFS_statfs stat;
};
endecode_fields_1_struct(
    PVFS_servresp_statfs,
    PVFS_statfs, stat);

/* io **********************************************************/
/* - performs a read or write operation */

/* V3 need to add SIDs to thei request both for failover and
 * reflexive write
 */

struct PVFS_servreq_io
{
    PVFS_handle handle;        /* target datafile */
    PVFS_fs_id fs_id;          /* file system */
    /* type of I/O operation to perform */
    enum PVFS_io_type io_type; /* enum defined in pvfs2-types.h */

    /* type of flow protocol to use for I/O transfer */
    enum PVFS_flowproto_type flow_type;

    /* relative number of this I/O server in distribution */
    uint32_t server_nr;
    /* total number of I/O servers involved in distribution */
    uint32_t server_ct;

    /* distribution */
    PINT_dist *io_dist;
    /* file datatype */
    struct PINT_Request *file_req;
    /* offset into file datatype */
    PVFS_offset file_req_offset;
    /* aggregate size of data to transfer */
    PVFS_size aggregate_size;
};
#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_servreq_io(pptr,x) do {          \
    encode_PVFS_handle(pptr, &(x)->handle);          \
    encode_PVFS_fs_id(pptr, &(x)->fs_id);            \
    encode_skip4(pptr,);                             \
    encode_enum(pptr, &(x)->io_type);                \
    encode_enum(pptr, &(x)->flow_type);              \
    encode_uint32_t(pptr, &(x)->server_nr);          \
    encode_uint32_t(pptr, &(x)->server_ct);          \
    encode_PINT_dist(pptr, &(x)->io_dist);           \
    encode_PINT_Request(pptr, &(x)->file_req);       \
    encode_PVFS_offset(pptr, &(x)->file_req_offset); \
    encode_PVFS_size(pptr, &(x)->aggregate_size);    \
} while (0)
#define decode_PVFS_servreq_io(pptr,x) do {                        \
    decode_PVFS_handle(pptr, &(x)->handle);                        \
    decode_PVFS_fs_id(pptr, &(x)->fs_id);                          \
    decode_skip4(pptr,);                                           \
    decode_enum(pptr, &(x)->io_type);                              \
    decode_enum(pptr, &(x)->flow_type);                            \
    decode_uint32_t(pptr, &(x)->server_nr);                        \
    decode_uint32_t(pptr, &(x)->server_ct);                        \
    decode_PINT_dist(pptr, &(x)->io_dist, NULL);                   \
    decode_PINT_Request(pptr, &(x)->file_req);                     \
    PINT_request_decode((x)->file_req); /* unpacks the pointers */ \
    decode_PVFS_offset(pptr, &(x)->file_req_offset);               \
    decode_PVFS_size(pptr, &(x)->aggregate_size);                  \
} while (0)
/* could be huge, limit to max ioreq size beyond struct itself */
#define extra_size_PVFS_servreq_io \
           (roundup8(PVFS_REQ_LIMIT_PATH_NAME_BYTES) + \
            roundup8(PVFS_REQ_LIMIT_PINT_REQUEST_NUM * sizeof(PINT_Request)))
#endif

#define PINT_SERVREQ_IO_FILL(__req,                   \
                             __cap,                   \
                             __fsid,                  \
                             __handle,                \
                             __io_type,               \
                             __flow_type,             \
                             __datafile_nr,           \
                             __datafile_ct,           \
                             __io_dist,               \
                             __file_req,              \
                             __file_req_off,          \
                             __aggregate_size,        \
                             __hints)                 \
do {                                                  \
    memset(&(__req), 0, sizeof(__req));               \
    (__req).op                   = PVFS_SERV_IO;      \
    (__req).ctrl.mode            = PVFS_REQ_SINGLE;   \
    (__req).ctrl.type            = PVFS_REQ_PRIMARY;  \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));       \
    (__req).hints                = (__hints);         \
    (__req).u.io.fs_id           = (__fsid);          \
    (__req).u.io.handle          = (__handle);        \
    (__req).u.io.io_type         = (__io_type);       \
    (__req).u.io.flow_type       = (__flow_type);     \
    (__req).u.io.server_nr       = (__datafile_nr);   \
    (__req).u.io.server_ct       = (__datafile_ct);   \
    (__req).u.io.io_dist         = (__io_dist);       \
    (__req).u.io.file_req        = (__file_req);      \
    (__req).u.io.file_req_offset = (__file_req_off);  \
    (__req).u.io.aggregate_size  = (__aggregate_size);\
} while (0)

struct PVFS_servresp_io
{
    PVFS_size bstream_size;  /* size of datafile */
};
endecode_fields_1_struct(
    PVFS_servresp_io,
    PVFS_size, bstream_size);

/* write operations require a second response to announce completion */
struct PVFS_servresp_write_completion
{
    PVFS_size total_completed; /* amount of data transferred */
};
endecode_fields_1_struct(
    PVFS_servresp_write_completion,
    PVFS_size, total_completed);

#define SMALL_IO_MAX_SEGMENTS 64

struct PVFS_servreq_small_io
{
    PVFS_handle handle;
    PVFS_fs_id fs_id;
    enum PVFS_io_type io_type;

    uint32_t server_nr;
    uint32_t server_ct;
    uint32_t sid_count;
    PVFS_SID *sid_array;

    PINT_dist *dist;
    struct PINT_Request *file_req;
    PVFS_offset file_req_offset;
    PVFS_size aggregate_size;

    /* these are used for writes to map the regions of the memory buffer
     * to the contiguous encoded message.  They don't get encoded.
     */
    int segments;
    PVFS_offset offsets[SMALL_IO_MAX_SEGMENTS];
    PVFS_size sizes[SMALL_IO_MAX_SEGMENTS];

    PVFS_size total_bytes; /* changed from int32_t */
    char *buffer;
};

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_servreq_small_io(pptr,x) do { \
    encode_PVFS_handle(pptr, &(x)->handle); \
    encode_PVFS_fs_id(pptr, &(x)->fs_id); \
    encode_enum(pptr, &(x)->io_type); \
    encode_uint32_t(pptr, &(x)->server_nr); \
    encode_uint32_t(pptr, &(x)->server_ct); \
    encode_uint32_t(pptr, &(x)->sid_count); \
    memcpy((*pptr), \
           (char *)(x)->sid_array, \
           (x)->sid_count * sizeof(PVFS_SID)); \
    (*pptr) += (x)->sid_count * sizeof(PVFS_SID); \
    encode_PINT_dist(pptr, &(x)->dist); \
    encode_PINT_Request(pptr, &(x)->file_req); \
    encode_PVFS_offset(pptr, &(x)->file_req_offset); \
    encode_PVFS_size(pptr, &(x)->aggregate_size); \
    encode_PVFS_size(pptr, &(x)->total_bytes); \
    encode_skip4(pptr,); \
    if ((x)->io_type == PVFS_IO_WRITE) \
    { \
        int i = 0; \
        for(; i < (x)->segments; ++i) \
        { \
            memcpy((*pptr), \
                   (char *)(x)->buffer + ((x)->offsets[i]), \
                   (x)->sizes[i]);                          \
            (*pptr) += (x)->sizes[i];                       \
        }                                                   \
    }                                                       \
} while (0)

#define decode_PVFS_servreq_small_io(pptr,x) do { \
    decode_PVFS_handle(pptr, &(x)->handle); \
    decode_PVFS_fs_id(pptr, &(x)->fs_id); \
    decode_enum(pptr, &(x)->io_type); \
    decode_uint32_t(pptr, &(x)->server_nr); \
    decode_uint32_t(pptr, &(x)->server_ct); \
    decode_uint32_t(pptr, &(x)->sid_count); \
    (x)->sid_array = (*pptr); \
    (*pptr) += (x)->sid_count * sizeof(PVFS_SID); \
    decode_PINT_dist(pptr, &(x)->dist, NULL); \
    decode_PINT_Request(pptr, &(x)->file_req); \
    PINT_request_decode((x)->file_req); /* unpacks the pointers */ \
    decode_PVFS_offset(pptr, &(x)->file_req_offset); \
    decode_PVFS_size(pptr, &(x)->aggregate_size); \
    decode_PVFS_size(pptr, &(x)->total_bytes); \
    decode_skip4(pptr,); \
    if ((x)->io_type == PVFS_IO_WRITE) \
    { \
        /* instead of copying the message we just set the pointer, since \
         * we know it will not be freed unil the small io state machine  \
         * has completed.                                                \
         */                                                              \
        (x)->buffer = (*pptr);                                           \
        (*pptr) += (x)->total_bytes;                                     \
    }                                                                    \
} while (0)
#endif

#define extra_size_PVFS_servreq_small_io PINT_SMALL_IO_MAXSIZE

/* could be huge, limit to max ioreq size beyond struct itself */
#define PINT_SERVREQ_SMALL_IO_FILL(__req,                                \
                                   __cap,                                \
                                   __fsid,                               \
                                   __handle,                             \
                                   __io_type,                            \
                                   __dfile_nr,                           \
                                   __dfile_ct,                           \
                                   __sid_count,                          \
                                   __sid_array,                          \
                                   __dist,                               \
                                   __filereq,                            \
                                   __filereq_offset,                     \
                                   __segments,                           \
                                   __memreq_size,                        \
                                   __hints )                             \
do {                                                                     \
    int _sio_i;                                                          \
    (__req).op                                = PVFS_SERV_SMALL_IO;      \
    (__req).ctrl.mode                         = PVFS_REQ_SINGLE;         \
    (__req).ctrl.type                         = PVFS_REQ_PRIMARY;        \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));                          \
    (__req).hints                             = (__hints);               \
    (__req).u.small_io.fs_id                  = (__fsid);                \
    (__req).u.small_io.handle                 = (__handle);              \
    (__req).u.small_io.io_type                = (__io_type);             \
    (__req).u.small_io.server_nr              = (__dfile_nr);            \
    (__req).u.small_io.server_ct              = (__dfile_ct);            \
    (__req).u.small_io.sid_count              = (__sid_count);           \
    (__req).u.small_io.sid_array              = (__sid_array);           \
    (__req).u.small_io.dist                   = (__dist);                \
    (__req).u.small_io.file_req               = (__filereq);             \
    (__req).u.small_io.file_req_offset        = (__filereq_offset);      \
    (__req).u.small_io.aggregate_size         = (__memreq_size);         \
    (__req).u.small_io.segments               = (__segments);            \
    (__req).u.small_io.total_bytes            = 0;                       \
    for(_sio_i = 0; _sio_i < (__segments); ++_sio_i)                     \
    {                                                                    \
        (__req).u.small_io.total_bytes +=                                \
            (__req).u.small_io.sizes[_sio_i];                            \
    }                                                                    \
} while(0)

struct PVFS_servresp_small_io
{
    enum PVFS_io_type io_type;

    /* the io state machine needs the total bstream size to calculate
     * the correct return size
     */
    PVFS_size bstream_size;

    /* for writes, this is the amount written.
     * for reads, this is the number of bytes read */
    PVFS_size result_size;
    char *buffer;
};

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_servresp_small_io(pptr,x)               \
    do {                                                    \
        encode_enum(pptr, &(x)->io_type);                   \
        encode_skip4(pptr,);                                \
        encode_PVFS_size(pptr, &(x)->bstream_size);         \
        encode_PVFS_size(pptr, &(x)->result_size);          \
        if((x)->io_type == PVFS_IO_READ && (x)->buffer)     \
        {                                                   \
            memcpy((*pptr), (x)->buffer, (x)->result_size); \
            (*pptr) += (x)->result_size;                    \
        }                                                   \
    } while(0)

#define decode_PVFS_servresp_small_io(pptr,x)       \
    do {                                            \
        decode_enum(pptr, &(x)->io_type);           \
        decode_skip4(pptr,);                        \
        decode_PVFS_size(pptr, &(x)->bstream_size); \
        decode_PVFS_size(pptr, &(x)->result_size);  \
        if((x)->io_type == PVFS_IO_READ)            \
        {                                           \
            (x)->buffer = (*pptr);                  \
            (*pptr) += (x)->result_size;            \
        }                                           \
    } while(0)
#endif

#define extra_size_PVFS_servresp_small_io PINT_SMALL_IO_MAXSIZE

/* listattr ****************************************************/
/* - retrieves attributes for a list of handles based on mask of PVFS_ATTR_XXX values */

struct PVFS_servreq_listattr
{
    PVFS_fs_id  fs_id;   /* file system */
    uint32_t    attrmask;  /* mask of desired attributes */
    uint32_t    nhandles; /* number of handles */
    PVFS_handle *handles; /* handle of target object */
    int32_t     sid_count;
    PVFS_SID    *sid_array;
};
endecode_fields_3a_struct(
    PVFS_servreq_listattr,
    PVFS_fs_id, fs_id,
    uint32_t, attrmask,
    skip4,,
    uint32_t, nhandles,
    PVFS_handle, handles);
#define extra_size_PVFS_servreq_listattr \
    (PVFS_REQ_LIMIT_LISTATTR * sizeof(PVFS_handle))

#define PINT_SERVREQ_LISTATTR_FILL(__req,          \
                                  __cap,           \
                                  __fsid,          \
                                  __amask,         \
                                  __nhandles,      \
                                  __handle_array,  \
                                  __hints)         \
do {                                               \
    memset(&(__req), 0, sizeof(__req));            \
    (__req).op = PVFS_SERV_LISTATTR;               \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;           \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;          \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));    \
    (__req).hints = (__hints);                     \
    (__req).u.listattr.fs_id = (__fsid);           \
    (__req).u.listattr.attrmask = (__amask);       \
    (__req).u.listattr.nhandles = (__nhandles);    \
    (__req).u.listattr.handles = (__handle_array); \
} while (0)

struct PVFS_servresp_listattr
{
    uint32_t nhandles;
    PVFS_error       *error;
    PVFS_object_attr *attr;
};
endecode_fields_1aa_struct(
    PVFS_servresp_listattr,
    skip4,,
    uint32_t, nhandles,
    PVFS_error, error,
    PVFS_object_attr, attr);
#define extra_size_PVFS_servresp_listattr \
                ((PVFS_REQ_LIMIT_LISTATTR * sizeof(PVFS_error)) + \
                 (PVFS_REQ_LIMIT_LISTATTR * extra_size_PVFS_object_attr))


/* mgmt_setparam ****************************************************/
/* - management operation for setting runtime parameters */

struct PVFS_servreq_mgmt_setparam
{
    PVFS_fs_id fs_id;             /* file system */
    enum PVFS_server_param param; /* parameter to set */
    struct PVFS_mgmt_setparam_value value;
};
endecode_fields_3_struct(
    PVFS_servreq_mgmt_setparam,
    PVFS_fs_id, fs_id,
    enum, param,
    PVFS_mgmt_setparam_value, value);

#define PINT_SERVREQ_MGMT_SETPARAM_FILL(__req,                      \
                                        __cap,                      \
                                        __fsid,                     \
                                        __param,                    \
                                        __value,                    \
                                        __hints)                    \
do {                                                                \
    memset(&(__req), 0, sizeof(__req));                             \
    (__req).op = PVFS_SERV_MGMT_SETPARAM;                           \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;                            \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                           \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));                     \
    (__req).hints = (__hints);                                      \
    (__req).u.mgmt_setparam.fs_id = (__fsid);                       \
    (__req).u.mgmt_setparam.param = (__param);                      \
    if(__value){                                                    \
        (__req).u.mgmt_setparam.value.type = (__value)->type;       \
        (__req).u.mgmt_setparam.value.u.value = (__value)->u.value; \
    }                                                               \
} while (0)

/* mgmt_noop ********************************************************/
/* - does nothing except contact a server to see if it is responding
 * to requests
 */

#define PINT_SERVREQ_MGMT_NOOP_FILL(__req, __cap, __hints) \
do {                                                       \
    memset(&(__req), 0, sizeof(__req));                    \
    (__req).op = PVFS_SERV_MGMT_NOOP;                      \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));            \
    (__req).hints = (__hints);                             \
} while (0)


/* mgmt_perf_mon ****************************************************/
/* retrieves performance statistics from server */

struct PVFS_servreq_mgmt_perf_mon
{
    uint32_t cnt_type;     /* type of perf counters to retrieve */
    uint32_t next_id;      /* next time stamp id we want to retrieve */
    uint32_t key_count;    /* how many counters per measurements we want */
    uint32_t count;        /* how many measurements we want */
};
endecode_fields_4_struct(
    PVFS_servreq_mgmt_perf_mon,
    uint32_t, cnt_type,
    uint32_t, next_id,
    uint32_t, key_count,
    uint32_t, count);

#define PINT_SERVREQ_MGMT_PERF_MON_FILL(__req,         \
                                        __cap,         \
                                        __cnt_type,    \
                                        __next_id,     \
                                        __key_count,   \
                                        __sample_count,\
                                        __hints)       \
do {                                                   \
    memset(&(__req), 0, sizeof(__req));                \
    (__req).op = PVFS_SERV_MGMT_PERF_MON;              \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));        \
    (__req).hints = (__hints);                         \
    (__req).u.mgmt_perf_mon.cnt_type = (__cnt_type);   \
    (__req).u.mgmt_perf_mon.next_id = (__next_id);     \
    (__req).u.mgmt_perf_mon.key_count = (__key_count); \
    (__req).u.mgmt_perf_mon.count = (__sample_count);  \
} while (0)

struct PVFS_servresp_mgmt_perf_mon
{
    int64_t *perf_array;            /* array of statistics */
    uint32_t perf_array_count;      /* size of above array */
    uint32_t key_count;             /* number of keys in each sample */
    uint32_t sample_count;          /* number of samples (history) */
    uint32_t suggested_next_id;     /* next id to pick up from this point */
    uint64_t end_time_ms;           /* end time for final array entry */
    uint64_t cur_time_ms;           /* current time according to svr */
};
endecode_fields_5a_struct(
    PVFS_servresp_mgmt_perf_mon,
    uint32_t, key_count,
    uint32_t, suggested_next_id,
    uint64_t, end_time_ms,
    uint64_t, cur_time_ms,
    uint32_t, sample_count,
    uint32_t, perf_array_count,
    int64_t,  perf_array);
#define extra_size_PVFS_servresp_mgmt_perf_mon \
                (PVFS_REQ_LIMIT_IOREQ_BYTES)

/* mgmt_iterate_handles ***************************************/
/* iterates through handles stored on server */

struct PVFS_servreq_mgmt_iterate_handles
{
    PVFS_fs_id fs_id;
    int32_t handle_count;
    int32_t flags;
    PVFS_ds_position position;
};
endecode_fields_4_struct(
    PVFS_servreq_mgmt_iterate_handles,
    PVFS_fs_id, fs_id,
    int32_t, handle_count,
    int32_t, flags,
    PVFS_ds_position, position);

#define PINT_SERVREQ_MGMT_ITERATE_HANDLES_FILL(__req,              \
                                        __cap,                     \
                                        __fs_id,                   \
                                        __handle_count,            \
                                        __position,                \
                                        __flags,                   \
                                        __hints)                   \
do {                                                               \
    memset(&(__req), 0, sizeof(__req));                            \
    (__req).op = PVFS_SERV_MGMT_ITERATE_HANDLES;                   \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;                           \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                          \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));                    \
    (__req).hints = (__hints);                                     \
    (__req).u.mgmt_iterate_handles.fs_id = (__fs_id);              \
    (__req).u.mgmt_iterate_handles.handle_count = (__handle_count);\
    (__req).u.mgmt_iterate_handles.position = (__position),        \
    (__req).u.mgmt_iterate_handles.flags = (__flags);              \
} while (0)

struct PVFS_servresp_mgmt_iterate_handles
{
    PVFS_ds_position position;
    PVFS_handle *handle_array;
    int handle_count;
};
endecode_fields_2a_struct(
    PVFS_servresp_mgmt_iterate_handles,
    PVFS_ds_position, position,
    skip4,,
    int32_t, handle_count,
    PVFS_handle, handle_array);
#define extra_size_PVFS_servresp_mgmt_iterate_handles \
            (PVFS_REQ_LIMIT_HANDLES_COUNT * sizeof(PVFS_handle))

/* mgmt_dspace_info_list **************************************/
/* - returns low level dspace information for a list of handles */

struct PVFS_servreq_mgmt_dspace_info_list
{
    PVFS_fs_id fs_id;
    PVFS_handle* handle_array;
    int32_t handle_count;
};
endecode_fields_1a_struct(
    PVFS_servreq_mgmt_dspace_info_list,
    PVFS_fs_id, fs_id,
    int32_t, handle_count,
    PVFS_handle, handle_array);
#define extra_size_PVFS_servreq_mgmt_dspace_info_list \
            (PVFS_REQ_LIMIT_HANDLES_COUNT * sizeof(PVFS_handle))

#define PINT_SERVREQ_MGMT_DSPACE_INFO_LIST(__req,                   \
                                        __cap,                      \
                                        __fs_id,                    \
                                        __handle_array,             \
                                        __handle_count,             \
                                        __hints)                    \
do {                                                                \
    memset(&(__req), 0, sizeof(__req));                             \
    (__req).op = PVFS_SERV_MGMT_DSPACE_INFO_LIST;                   \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;                            \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                           \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));                     \
    (__req).hints = (__hints);                                      \
    (__req).u.mgmt_dspace_info_list.fs_id = (__fs_id);              \
    (__req).u.mgmt_dspace_info_list.handle_array = (__handle_array);\
    (__req).u.mgmt_dspace_info_list.handle_count = (__handle_count);\
} while (0)

struct PVFS_servresp_mgmt_dspace_info_list
{
    struct PVFS_mgmt_dspace_info *dspace_info_array;
    int32_t dspace_info_count;
};
endecode_fields_1a_struct(
    PVFS_servresp_mgmt_dspace_info_list,
    skip4,,
    int32_t, dspace_info_count,
    PVFS_mgmt_dspace_info, dspace_info_array);
#define extra_size_PVFS_servresp_mgmt_dspace_info_list \
                (PVFS_REQ_LIMIT_MGMT_DSPACE_INFO_LIST_COUNT * \
                 sizeof(struct PVFS_mgmt_dspace_info))

/* mgmt_event_mon **************************************/
/* - returns event logging data */

struct PVFS_servreq_mgmt_event_mon
{
    uint32_t event_count;
};
endecode_fields_1_struct(
    PVFS_servreq_mgmt_event_mon,
    uint32_t, event_count);

#define PINT_SERVREQ_MGMT_EVENT_MON_FILL(__req,                       \
                                         __cap,                       \
                                         __event_count,               \
                                         __hints)                     \
do {                                                                  \
    memset(&(__req), 0, sizeof(__req));                               \
    (__req).op = PVFS_SERV_MGMT_EVENT_MON;                            \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;                              \
    (__req).capability = (__cap);                                     \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));                       \
    (__req).hints = (__hints);                                        \
    (__req).u.mgmt_event_mon.event_count = (__event_count);           \
} while (0)

struct PVFS_servresp_mgmt_event_mon
{
    struct PVFS_mgmt_event* event_array;
    uint32_t event_count;
};
endecode_fields_1a_struct(
    PVFS_servresp_mgmt_event_mon,
    skip4,,
    uint32_t, event_count,
    PVFS_mgmt_event, event_array);
#define extra_size_PVFS_servresp_mgmt_event_mon \
                (PVFS_REQ_LIMIT_MGMT_EVENT_MON_COUNT * \
                 roundup8(sizeof(struct PVFS_mgmt_event)))

/* geteattr ****************************************************/
/* - retrieves list of extended attributes */

struct PVFS_servreq_geteattr
{
    PVFS_handle handle;  /* handle of target object */
    PVFS_fs_id fs_id;    /* file system */
    int32_t nkey;        /* number of keys to read */
    PVFS_ds_keyval *key; /* array of keys to read */
    PVFS_size *valsz;    /* array of value buffer sizes */
};
endecode_fields_2aa_struct(
    PVFS_servreq_geteattr,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    int32_t, nkey,
    PVFS_ds_keyval, key,
    PVFS_size, valsz);
#define extra_size_PVFS_servreq_geteattr               \
                ((PVFS_REQ_LIMIT_EATTR_KEY_LEN + sizeof(PVFS_size)) * \
                 PVFS_REQ_LIMIT_EATTR_LIST)

#define PINT_SERVREQ_GETEATTR_FILL(__req,       \
                                  __cap,        \
                                  __fsid,       \
                                  __handle,     \
                                  __nkey,       \
                                  __key_array,  \
                                  __size_array, \
                                  __hints)      \
do {                                            \
    memset(&(__req), 0, sizeof(__req));         \
    (__req).op = PVFS_SERV_GETEATTR;            \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;        \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;       \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req)); \
    (__req).hints = (__hints);                  \
    (__req).u.geteattr.fs_id = (__fsid);        \
    (__req).u.geteattr.handle = (__handle);     \
    (__req).u.geteattr.nkey = (__nkey);         \
    (__req).u.geteattr.key = (__key_array);     \
    (__req).u.geteattr.valsz = (__size_array);  \
} while (0)

struct PVFS_servresp_geteattr
{
    int32_t nkey;           /* number of values returned */
    PVFS_ds_keyval *val;    /* array of values returned */
    PVFS_error *err;        /* array of error codes */
};
endecode_fields_1aa_struct(
    PVFS_servresp_geteattr,
    skip4,,
    int32_t, nkey,
    PVFS_ds_keyval, val,
    PVFS_error, err);
#define extra_size_PVFS_servresp_geteattr                \
    ((PVFS_REQ_LIMIT_EATTR_VAL_LEN + sizeof(PVFS_error)) * \
     PVFS_REQ_LIMIT_EATTR_LIST)

/* seteattr ****************************************************/
/* - sets list of extended attributes */

struct PVFS_servreq_seteattr
{
    PVFS_handle    handle;     /* handle of target object */
    PVFS_fs_id     fs_id;      /* file system */
    int32_t        sid_count;  /* number of sids */
    PVFS_SID       *sid_array; /* array of sids for object */
    int32_t        flags;      /* flags */
    int32_t        nkey;       /* number of keys and vals */
    PVFS_ds_keyval *key;       /* attribute key */
    PVFS_ds_keyval *val;       /* attribute value */
};
endecode_fields_2a1aa_struct(
    PVFS_servreq_seteattr,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    int32_t, sid_count,
    PVFS_SID, sid_array,
    int32_t, flags,
    int32_t, nkey,
    PVFS_ds_keyval, key,
    PVFS_ds_keyval, val);
#define extra_size_PVFS_servreq_seteattr                            \
    ((PVFS_REQ_LIMIT_SIDS_COUNT + sizeof(PVFS_SID)) + \
     ((PVFS_REQ_LIMIT_EATTR_KEY_LEN + PVFS_REQ_LIMIT_EATTR_VAL_LEN) * \
      PVFS_REQ_LIMIT_EATTR_LIST))

#define PINT_SERVREQ_SETEATTR_FILL(__req,         \
                                  __cap,          \
                                  __fsid,         \
                                  __handle,       \
                                  __sid_count,    \
                                  __sid_array,    \
                                  __flags,        \
                                  __nkey,         \
                                  __key_array,    \
                                  __val_array,    \
                                  __hints)        \
do {                                              \
    memset(&(__req), 0, sizeof(__req));           \
    (__req).op = PVFS_SERV_SETEATTR;              \
    (__req).ctrl.mode = PVFS_REQ_REPLICATE;       \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;         \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));   \
    (__req).hints = (__hints);                    \
    (__req).u.seteattr.fs_id = (__fsid);          \
    (__req).u.seteattr.handle = (__handle);       \
    (__req).u.seteattr.sid_count = (__sid_count); \
    (__req).u.seteattr.sid_array = (__sid_array); \
    (__req).u.seteattr.flags = (__flags);         \
    (__req).u.seteattr.nkey = (__nkey);           \
    (__req).u.seteattr.key = (__key_array);       \
    (__req).u.seteattr.val = (__val_array);       \
} while (0)

/* atomiceattr *************************************************/
/* - gets current list of extended attributes and then sets new
 * list of attributes
 */

struct PVFS_servreq_atomiceattr
{
    PVFS_handle handle;
    PVFS_fs_id fs_id;
    int32_t sid_count;
    PVFS_SID *sid_array;
    int32_t flags;
    int32_t opcode;
    int32_t nkey;
    PVFS_ds_keyval *key;        /* attribute key */
    PVFS_ds_keyval *old_val;    /* attribute value to compare */
    PVFS_ds_keyval *new_val;    /* attribute value to set */
};
endecode_fields_2a3aaa_struct(
    PVFS_servreq_atomiceattr,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    int32_t, sid_count,
    PVFS_SID, sid_array,     /* reflexive */
    skip4,,
    int32_t, flags,
    int32_t, opcode,
    int32_t, nkey,
    PVFS_ds_keyval, key,
    PVFS_ds_keyval, old_val,
    PVFS_ds_keyval, new_val);
#define extra_size_PVFS_servreq_atomiceattr                         \
    ((PVFS_REQ_LIMIT_EATTR_KEY_LEN + PVFS_REQ_LIMIT_EATTR_VAL_LEN) * \
     PVFS_REQ_LIMIT_EATTR_LIST + sizeof(PVFS_size) * \
     PVFS_REQ_LIMIT_EATTR_LIST)

#define PINT_SERVREQ_ATOMICEATTR_FILL(__req,           \
                                      __cap,           \
                                      __fsid,          \
                                      __handle,        \
                                      __sid_count,     \
                                      __sid_array,     \
                                      __flags,         \
                                      __opcode,        \
                                      __nkey,          \
                                      __key_array,     \
                                      __old_val_array, \
                                      __new_val_array, \
                                      __hints)         \
do {                                                   \
    memset(&(__req), 0, sizeof(__req));                \
    (__req).op = PVFS_SERV_ATOMICEATTR;                \
    (__req).ctrl.mode = PVFS_REQ_REPLICATE;            \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;              \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));        \
    (__req).hints = (__hints);                         \
    (__req).u.atomiceattr.fs_id = (__fsid);            \
    (__req).u.atomiceattr.handle = (__handle);         \
    (__req).u.atomiceattr.sid_count = (__sid_count);   \
    (__req).u.atomiceattr.sid_array = (__sid_array);   \
    (__req).u.atomiceattr.flags = (__flags);           \
    (__req).u.atomiceattr.opcode = (__opcode);         \
    (__req).u.atomiceattr.nkey = (__nkey);             \
    (__req).u.atomiceattr.key = (__key_array);         \
    (__req).u.atomiceattr.old_val = (__old_val_array); \
    (__req).u.atomiceattr.new_val = (__new_val_array); \
} while (0)


struct PVFS_servresp_atomiceattr
{
    int32_t nkey;           /* number of values returned */
    PVFS_error *err;        /* array of error codes */
    PVFS_ds_keyval *ret_val; /* array of return values */
};
endecode_fields_1aa_struct(
    PVFS_servresp_atomiceattr,
    skip4,,
    int32_t, nkey,
    PVFS_error, err,
    PVFS_ds_keyval, ret_val);
#define extra_size_PVFS_servresp_atomiceattr             \
    ((PVFS_REQ_LIMIT_EATTR_VAL_LEN + sizeof(PVFS_error)) \
     * PVFS_REQ_LIMIT_EATTR_LIST)

/* deleattr ****************************************************/
/* - deletes extended attributes */

struct PVFS_servreq_deleattr
{
    PVFS_handle handle; /* handle of target object */
    PVFS_fs_id fs_id;   /* file system */
    PVFS_ds_keyval key; /* key to read */
    int32_t sid_count;  /* reflexive */
    PVFS_SID *sid_array;/* reflexive */
};
endecode_fields_4a_struct(
    PVFS_servreq_deleattr,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    PVFS_ds_keyval, key,
    skip4,,
    int32_t, sid_count,
    PVFS_SID, sid_array);
#define extra_size_PVFS_servreq_deleattr \
             (PVFS_REQ_LIMIT_EATTR_KEY_LEN + \
              (PVFS_REQ_LIMIT_SIDS_COUNT * sizeof(PVFS_SID)))

#define PINT_SERVREQ_DELEATTR_FILL(__req,                 \
                                  __cap,                  \
                                  __fsid,                 \
                                  __handle,               \
                                  __sid_count,            \
                                  __sid_array,            \
                                  __key,                  \
                                  __hints)                \
do {                                                      \
    memset(&(__req), 0, sizeof(__req));                   \
    (__req).op = PVFS_SERV_DELEATTR;                      \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;               \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                 \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));           \
    (__req).hints = (__hints);                            \
    (__req).u.deleattr.fs_id = (__fsid);                  \
    (__req).u.deleattr.handle = (__handle);               \
    (__req).u.deleattr.sid_count = (__sid_count);         \
    (__req).u.deleattr.sid_array = (__sid_array);         \
    (__req).u.deleattr.key.buffer_sz = (__key).buffer_sz; \
    (__req).u.deleattr.key.buffer = (__key).buffer;       \
} while (0)

/* listeattr **************************************************/
/* - list extended attributes */

struct PVFS_servreq_listeattr
{
    PVFS_handle handle;     /* handle of dir object */
    PVFS_fs_id  fs_id;      /* file system */
    PVFS_ds_position token; /* offset */
    uint32_t     nkey;      /* desired number of keys to read */
    PVFS_size   *keysz;     /* array of key buffer sizes */
};
endecode_fields_4a_struct(
    PVFS_servreq_listeattr,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    skip4,,
    PVFS_ds_position, token,
    uint32_t, nkey,
    PVFS_size, keysz);
#define extra_size_PVFS_servreq_listeattr \
                (PVFS_REQ_LIMIT_EATTR_LIST * sizeof(PVFS_size))

#define PINT_SERVREQ_LISTEATTR_FILL(__req,            \
                                  __cap,              \
                                  __fsid,             \
                                  __handle,           \
                                  __token,            \
                                  __nkey,             \
                                  __size_array,       \
                                  __hints)            \
do {                                                  \
    memset(&(__req), 0, sizeof(__req));               \
    (__req).op = PVFS_SERV_LISTEATTR;                 \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;              \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;             \
    (__req).capability = (__cap);                     \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));       \
    (__req).hints = (__hints);                        \
    (__req).u.listeattr.fs_id = (__fsid);             \
    (__req).u.listeattr.handle = (__handle);          \
    (__req).u.listeattr.token = (__token);            \
    (__req).u.listeattr.nkey = (__nkey);              \
    (__req).u.listeattr.keysz = (__size_array);       \
} while (0);

struct PVFS_servresp_listeattr
{
    PVFS_ds_position token;  /* new dir offset */
    uint32_t nkey;   /* # of keys retrieved */
    PVFS_ds_keyval *key; /* array of keys returned */
};
endecode_fields_2a_struct(
    PVFS_servresp_listeattr,
    PVFS_ds_position, token,
    skip4,,
    uint32_t, nkey,
    PVFS_ds_keyval, key);
#define extra_size_PVFS_servresp_listeattr \
                (PVFS_REQ_LIMIT_EATTR_KEY_LEN * PVFS_REQ_LIMIT_EATTR_LIST)

/* mgmt_get_uid ****************************************************/
/* retrieves uid managment history from server */

struct PVFS_servreq_mgmt_get_uid
{
    uint32_t history;      /* number of seconds we want to go back
                              when retrieving the uid history */
};
endecode_fields_1_struct(
    PVFS_servreq_mgmt_get_uid,
    uint32_t, history);

#define PINT_SERVREQ_MGMT_GET_UID_FILL(__req,         \
                                        __cap,        \
                                        __history,    \
                                        __hints)      \
do {                                                  \
    memset(&(__req), 0, sizeof(__req));               \
    (__req).op = PVFS_SERV_MGMT_GET_UID;              \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;              \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;             \
    (__req).capability = (__cap);                     \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));       \
    (__req).hints = (__hints);                        \
    (__req).u.mgmt_get_uid.history = (__history);     \
} while (0)

struct PVFS_servresp_mgmt_get_uid
{
    PVFS_uid_info_s *uid_info_array;    /* array of uid info */
    uint32_t uid_info_array_count;      /* size of above array */
};
endecode_fields_1a_struct(
    PVFS_servresp_mgmt_get_uid,
    skip4,,
    uint32_t, uid_info_array_count,
    PVFS_uid_info_s, uid_info_array);
#define extra_size_PVFS_servresp_mgmt_get_uid \
                (UID_MGMT_MAX_HISTORY * sizeof(PVFS_uid_info_s))

/* mgmt_get_dirent ************************************************/
/* - used to retrieve the handle of the specified directory entry */
struct PVFS_servreq_mgmt_get_dirent
{   
    PVFS_handle handle;
    PVFS_fs_id fs_id;
    char *entry;               /* name of entry to retrieve */
};
endecode_fields_3_struct(
    PVFS_servreq_mgmt_get_dirent,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id,
    string, entry);
#define extra_size_PVFS_servreq_mgmt_get_dirent \
            roundup8(PVFS_REQ_LIMIT_SEGMENT_BYTES + 1)
    
#define PINT_SERVREQ_MGMT_GET_DIRENT_FILL(__req,   \
                                          __cap,   \
                                          __fsid,  \
                                          __handle,\
                                          __entry,\
                                          __hints) \
do {                                                       \
    memset(&(__req), 0, sizeof(__req));                    \
    (__req).op = PVFS_SERV_MGMT_GET_DIRENT;                \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;                   \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                  \
    PVFS_REQ_COPY_CAPABILITY(__cap, __req);                \
    (__req).hints = (__hints);                             \
    (__req).u.mgmt_get_dirent.fs_id = (__fsid);            \
    (__req).u.mgmt_get_dirent.handle = (__handle);         \
    (__req).u.mgmt_get_dirent.entry = (__entry);           \
} while (0)

struct PVFS_servresp_mgmt_get_dirent
{   
    PVFS_handle handle;
    int sid_count;
    PVFS_SID *sid_array;
    PVFS_error  error;
};
endecode_fields_2a_struct(
    PVFS_servresp_mgmt_get_dirent,
    PVFS_handle, handle,
    PVFS_error, error,
    uint32_t, sid_count,
    PVFS_SID, sid_array);
#define extra_size_PVFS_servresp_mgmt_get_dirent \
                (PVFS_REQ_LIMIT_SIDS_COUNT * sizeof(PVFS_SID))

/* V3 no longer needed */
#if 0
/* mgmt_create_root_dir ************************************************/
/* - used to create root directory at very first startup time, only called noreq */
struct PVFS_servreq_mgmt_create_root_dir
{
    PVFS_handle handle;
    PVFS_fs_id fs_id;
};
endecode_fields_2_struct(
    PVFS_servreq_mgmt_create_root_dir,
    PVFS_handle, handle,
    PVFS_fs_id, fs_id);

#define PINT_SERVREQ_MGMT_CREATE_ROOT_DIR_FILL(__req,      \
                                                  __cap,   \
                                                  __fsid,  \
                                                  __handle,\
                                                  __hints) \
do {                                                       \
    memset(&(__req), 0, sizeof(__req));                    \
    (__req).op = PVFS_SERV_MGMT_CREATE_ROOT_DIR;           \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;                \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                  \
    (__req).capability = (__cap);                          \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));            \
    (__req).hints = (__hints);                             \
    (__req).u.mgmt_create_root_dir.fs_id = (__fsid);       \
    (__req).u.mgmt_create_root_dir.handle = (__handle);    \
} while (0)
#endif

/* mgmt_split_dirent ************************************************/
/* - used to send directory entries to another server for storing */

/* V3 we need reflexive SIDs here */
/* V3 need SIDs for entry handles */
struct PVFS_servreq_mgmt_split_dirent
{
    PVFS_fs_id   fs_id;
    int32_t      undo;
    int32_t      nentries;
    int32_t      sid_count;          /* sids per meta handle */
    PVFS_handle  dest_dirent_handle; /* should be dirdata handle need SIDs */
    PVFS_SID    *dest_dirent_sids;   /* should be dirdata handle need SIDs */
    PVFS_handle *entry_handles;
    char       **entry_names;
};
#if 0
endecode_fields_5aa_struct(
    PVFS_servreq_mgmt_split_dirent,
    PVFS_fs_id, fs_id,
    PVFS_handle, dest_dirent_handle,
    PINT_dist, dist,
    int32_t, undo,
    int32_t, nentries,
    int32_t, sid_count,
    PVFS_handle, entry_handles,
    string, entry_names);
#endif

#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_servreq_mgmt_split_dirent(pptr,x)                         \
    do {                                                                      \
        int i, j = 0;                                                         \
        encode_PVFS_fs_id((pptr), &(x)->fs_id);                               \
        encode_uint32_t((pptr), &(x)->undo);                                  \
        encode_uint32_t((pptr), &(x)->nentries);                              \
        encode_uint32_t((pptr), &(x)->sid_count);                             \
        encode_PVFS_handle((pptr), &(x)->dest_dirent_handle);                 \
        for (i = 0; i < (x)->sid_count; i++)                                  \
        {                                                                     \
            encode_PVFS_SID((pptr), &(x)->dest_dirent_sids[i]);               \
        }                                                                     \
        for (i = 0; i < (x)->nentries; i++)                                   \
        {                                                                     \
            encode_string((pptr), &(x)->entry_names[i]);                      \
        }                                                                     \
        for (i = 0; i < (x)->sid_count; i++)                                  \
        {                                                                     \
            encode_PVFS_handle((pptr), &(x)->entry_handles[j++]);             \
            for (i = 0; i < (x)->sid_count; i++)                              \
            {                                                                 \
                encode_PVFS_SID((pptr), (PVFS_SID *)&(x)->entry_handles[j++]);\
            }                                                                 \
        }                                                                     \
    } while (0);

#define decode_PVFS_servreq_mgmt_split_dirent(pptr,x)                         \
    do {                                                                      \
        int i, j = 0;                                                         \
        decode_PVFS_fs_id((pptr), &(x)->fs_id);                               \
        decode_uint32_t((pptr), &(x)->undo);                                  \
        decode_uint32_t((pptr), &(x)->nentries);                              \
        decode_uint32_t((pptr), &(x)->sid_count);                             \
        decode_PVFS_handle((pptr), &(x)->dest_dirent_handle);                 \
        (x)->dest_dirent_sids = decode_malloc(SASZ((x)->sid_count));          \
        (x)->entry_handles = decode_malloc(OSASZ((x)->nentries,               \
                                                 (x)->sid_count));            \
        for (i = 0; i < (x)->sid_count; i++)                                  \
        {                                                                     \
            decode_PVFS_SID((pptr), &(x)->dest_dirent_sids[i]);               \
        }                                                                     \
        for (i = 0; i < (x)->nentries; i++)                                   \
        {                                                                     \
            decode_string((pptr), &(x)->entry_names[i]);                      \
        }                                                                     \
        for (i = 0; i < (x)->nentries; i++)                                   \
        {                                                                     \
            decode_PVFS_handle((pptr), &(x)->entry_handles[j++]);             \
            for (i = 0; i < (x)->sid_count; i++)                              \
            {                                                                 \
                decode_PVFS_SID((pptr), (PVFS_SID *)&(x)->entry_handles[j++]);\
            }                                                                 \
        }                                                                     \
    } while (0);

#endif
#define extra_size_PVFS_servreq_mgmt_split_dirent \
    ((PVFS_REQ_LIMIT_HANDLES_COUNT * sizeof(PVFS_handle)) + \
     (PVFS_REQ_LIMIT_HANDLES_COUNT * roundup8(PVFS_REQ_LIMIT_SEGMENT_BYTES + 1)))

#define PINT_SERVREQ_MGMT_SPLIT_DIRENT_FILL(__req,                            \
                                       __cap,                                 \
                                       __fsid,                                \
                                       __sid_count,                           \
                                       __dest_dirent_handle,                  \
                                       __dest_dirent_sids,                    \
                                       __undo,                                \
                                       __nentries,                            \
                                       __entry_handles,                       \
                                       __entry_names,                         \
                                       __hints)                               \
do {                                                                          \
    memset(&(__req), 0, sizeof(__req));                                       \
    (__req).op = PVFS_SERV_MGMT_SPLIT_DIRENT;                                 \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;                                   \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                                     \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));                               \
    (__req).hints       = (__hints);                                          \
    (__req).u.mgmt_split_dirent.fs_id = (__fsid);                             \
    (__req).u.mgmt_split_dirent.sid_count = (__sid_count);                    \
    (__req).u.mgmt_split_dirent.dest_dirent_handle = (__dest_dirent_handle);  \
    (__req).u.mgmt_split_dirent.dest_dirent_sids = (__dest_dirent_sids);      \
    (__req).u.mgmt_split_dirent.undo          = (__undo);                     \
    (__req).u.mgmt_split_dirent.nentries      = (__nentries);                 \
    (__req).u.mgmt_split_dirent.entry_handles = (__entry_handles);            \
    (__req).u.mgmt_split_dirent.entry_names   = (__entry_names);              \
} while (0)

/* get_user_cert ******************************************************/
/* - retrieve user certificate/key from server given user id/password */


struct PVFS_servreq_mgmt_get_user_cert
{
    PVFS_fs_id fs_id;
    char *userid;
    PVFS_size enc_pwd_size;
    char *enc_pwd;
    PVFS_size enc_key_size;
    char *enc_key;
    uint32_t exp;
};
#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
#define encode_PVFS_servreq_mgmt_get_user_cert(pptr,x) do {    \
    encode_PVFS_fs_id(pptr, &(x)->fs_id);                      \
    encode_string(pptr, &(x)->userid);                         \
    encode_PVFS_size(pptr, &(x)->enc_pwd_size);                \
    memcpy((*pptr), (char *) (x)->enc_pwd, (x)->enc_pwd_size); \
    (*pptr) += (x)->enc_pwd_size;                              \
    encode_PVFS_size(pptr, &(x)->enc_key_size);                \
    memcpy((*pptr), (char *) (x)->enc_key, (x)->enc_key_size); \
    (*pptr) += (x)->enc_key_size;                              \
    encode_uint32_t(pptr, &(x)->exp);                          \
} while (0)

#define decode_PVFS_servreq_mgmt_get_user_cert(pptr,x) do { \
    decode_PVFS_fs_id(pptr, &(x)->fs_id);                   \
    decode_string(pptr, &(x)->userid);                      \
    decode_PVFS_size(pptr, &(x)->enc_pwd_size);             \
    (x)->enc_pwd = (*pptr);                                 \
    (*pptr) += (x)->enc_pwd_size;                           \
    decode_PVFS_size(pptr, &(x)->enc_key_size);             \
    (x)->enc_key = (*pptr);                                 \
    (*pptr) += (x)->enc_key_size;                           \
    decode_uint32_t(pptr, &(x)->exp);                       \
} while (0)
#endif

#define extra_size_PVFS_servreq_mgmt_get_user_cert \
                (PVFS_REQ_LIMIT_USERID_PWD * 2) + \
                 PVFS_REQ_LIMIT_ENC_KEY

#define PINT_SERVREQ_MGMT_GET_USER_CERT_FILL(__req,          \
                                             __cap,          \
                                             __fsid,         \
                                             __userid,       \
                                             __pwdsize,      \
                                             __pwd,          \
                                             __keysize,      \
                                             __key,          \
                                             __exp)          \
do {                                                         \
    memset(&(__req), 0, sizeof(__req));                      \
    (__req).op = PVFS_SERV_MGMT_GET_USER_CERT;               \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;                     \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                    \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));              \
    (__req).u.mgmt_get_user_cert.fs_id   = (__fsid);         \
    (__req).u.mgmt_get_user_cert.userid  = (__userid);       \
    (__req).u.mgmt_get_user_cert.enc_pwd_size = (__pwdsize); \
    (__req).u.mgmt_get_user_cert.enc_pwd =                   \
        (char *) (__pwd);                                    \
    (__req).u.mgmt_get_user_cert.enc_key_size = (__keysize); \
    (__req).u.mgmt_get_user_cert.enc_key =                   \
        (char *) (__key);                                    \
    (__req).u.mgmt_get_user_cert.exp     = (__exp);          \
} while (0)

struct PVFS_servresp_mgmt_get_user_cert
{
    PVFS_certificate cert;
};
endecode_fields_1_struct(
    PVFS_servresp_mgmt_get_user_cert,
    PVFS_certificate, cert);
#define extra_size_PVFS_servresp_mgmt_get_user_cert PVFS_REQ_LIMIT_CERT

/* get_user_cert_keyreq *****************************************************/
/* - request the CA public key in order to encrypt password and private key */

struct PVFS_servreq_mgmt_get_user_cert_keyreq
{
    PVFS_fs_id fs_id;
};
endecode_fields_1_struct(
    PVFS_servreq_mgmt_get_user_cert_keyreq,
    PVFS_fs_id, fs_id);

#define PINT_SERVREQ_MGMT_GET_USER_CERT_KEYREQ_FILL(__req,   \
                                                    __cap,   \
                                                    __fsid)  \
do {                                                         \
    memset(&(__req), 0, sizeof(__req));                      \
    (__req).op = PVFS_SERV_MGMT_GET_USER_CERT_KEYREQ;        \
    (__req).ctrl.mode = PVFS_REQ_SINGLE;                     \
    (__req).ctrl.type = PVFS_REQ_PRIMARY;                    \
    (__req).capability = (__cap);                            \
    PVFS_REQ_COPY_CAPABILITY((__cap), (__req));              \
    (__req).u.mgmt_get_user_cert_keyreq.fs_id = (__fsid);    \
} while (0)

struct PVFS_servresp_mgmt_get_user_cert_keyreq
{
    PVFS_security_key public_key;
};
endecode_fields_1_struct(
    PVFS_servresp_mgmt_get_user_cert_keyreq,
    PVFS_security_key, public_key);
#define extra_size_PVFS_servresp_mgmt_get_user_cert_keyreq \
                PVFS_REQ_LIMIT_SECURITY_KEY

/* server request *********************************************/
/* - generic request with union of all op specific structs */

struct PVFS_server_req
{
    enum PVFS_server_op op;
    PVFS_server_control ctrl;
    PVFS_capability capability;
    PVFS_hint hints;

    union
    {
        struct PVFS_servreq_mirror mirror;
        struct PVFS_servreq_create create;
        struct PVFS_servreq_unstuff unstuff;
        struct PVFS_servreq_batch_create batch_create;
        struct PVFS_servreq_remove remove;
        struct PVFS_servreq_batch_remove batch_remove;
        struct PVFS_servreq_io io;
        struct PVFS_servreq_getattr getattr;
        struct PVFS_servreq_setattr setattr;
        struct PVFS_servreq_mkdir mkdir;
        struct PVFS_servreq_readdir readdir;
        struct PVFS_servreq_lookup_path lookup_path;
        struct PVFS_servreq_crdirent crdirent;
        struct PVFS_servreq_rmdirent rmdirent;
        struct PVFS_servreq_chdirent chdirent;
        struct PVFS_servreq_truncate truncate;
        struct PVFS_servreq_flush flush;
        struct PVFS_servreq_mgmt_setparam mgmt_setparam;
        struct PVFS_servreq_statfs statfs;
        struct PVFS_servreq_mgmt_perf_mon mgmt_perf_mon;
        struct PVFS_servreq_mgmt_iterate_handles mgmt_iterate_handles;
        struct PVFS_servreq_mgmt_dspace_info_list mgmt_dspace_info_list;
        struct PVFS_servreq_mgmt_event_mon mgmt_event_mon;
        struct PVFS_servreq_mgmt_remove_object mgmt_remove_object;
        struct PVFS_servreq_mgmt_remove_dirent mgmt_remove_dirent;
        struct PVFS_servreq_mgmt_get_dirdata_handle mgmt_get_dirdata_handle;
        struct PVFS_servreq_geteattr geteattr;
        struct PVFS_servreq_seteattr seteattr;
        struct PVFS_servreq_atomiceattr atomiceattr;
        struct PVFS_servreq_deleattr deleattr;
        struct PVFS_servreq_listeattr listeattr;
        struct PVFS_servreq_small_io small_io;
        struct PVFS_servreq_listattr listattr;
        struct PVFS_servreq_tree_remove tree_remove;
        struct PVFS_servreq_tree_get_file_size tree_get_file_size;
        struct PVFS_servreq_tree_getattr tree_getattr;
        struct PVFS_servreq_mgmt_get_uid mgmt_get_uid;
        struct PVFS_servreq_tree_setattr tree_setattr;
        struct PVFS_servreq_mgmt_get_dirent mgmt_get_dirent;
/* V3 - no longer needed */
#if 0
        struct PVFS_servreq_mgmt_create_root_dir mgmt_create_root_dir;
#endif
        struct PVFS_servreq_mgmt_split_dirent mgmt_split_dirent;
        struct PVFS_servreq_mgmt_get_user_cert mgmt_get_user_cert;
        struct PVFS_servreq_mgmt_get_user_cert_keyreq mgmt_get_user_cert_keyreq;
    } u;
};
#ifdef __PINT_REQPROTO_ENCODE_FUNCS_C
/* insert padding to ensure the union starts on an aligned boundary */
static inline void encode_PVFS_server_req(char **pptr,
                                          const struct PVFS_server_req *x)
{
    encode_enum(pptr, &x->op);                    /* 4 bytes */
    encode_PVFS_server_control(pptr, &x->ctrl);   /* 4 bytes */
    encode_PVFS_capability(pptr, &x->capability); /* variable */
    align8(pptr);
    encode_PINT_hint(pptr, x->hints);             /* variable */
    align8(pptr);
}
static inline void decode_PVFS_server_req(char **pptr,
                                          struct PVFS_server_req *x)
{
    decode_enum(pptr, &x->op);
    decode_PVFS_server_control(pptr, &x->ctrl);
    decode_PVFS_capability(pptr, &x->capability);
    align8(pptr);
    decode_PINT_hint(pptr, &x->hints);
    align8(pptr);
}
#endif
#define extra_size_PVFS_servreq extra_size_PVFS_capability

/* server response *********************************************/
/* - generic response with union of all op specific structs */
struct PVFS_server_resp
{
    enum PVFS_server_op op;
    PVFS_error status;
    union
    {
        struct PVFS_servresp_mirror mirror;
        struct PVFS_servresp_create create;
        struct PVFS_servresp_unstuff unstuff;
        struct PVFS_servresp_batch_create batch_create; 
        struct PVFS_servresp_getattr getattr;
        struct PVFS_servresp_mkdir mkdir;
        struct PVFS_servresp_readdir readdir;
        struct PVFS_servresp_lookup_path lookup_path;
        struct PVFS_servresp_rmdirent rmdirent;
        struct PVFS_servresp_chdirent chdirent;
        struct PVFS_servresp_getconfig getconfig;
        struct PVFS_servresp_io io;
        struct PVFS_servresp_write_completion write_completion;
        struct PVFS_servresp_statfs statfs;
        struct PVFS_servresp_mgmt_perf_mon mgmt_perf_mon;
        struct PVFS_servresp_mgmt_iterate_handles mgmt_iterate_handles;
        struct PVFS_servresp_mgmt_dspace_info_list mgmt_dspace_info_list;
        struct PVFS_servresp_mgmt_event_mon mgmt_event_mon;
        struct PVFS_servresp_mgmt_get_dirdata_handle mgmt_get_dirdata_handle;
        struct PVFS_servresp_geteattr geteattr;
        struct PVFS_servresp_atomiceattr atomiceattr;
        struct PVFS_servresp_listeattr listeattr;
        struct PVFS_servresp_small_io small_io;
        struct PVFS_servresp_listattr listattr;
        struct PVFS_servresp_tree_remove tree_remove;
        struct PVFS_servresp_tree_get_file_size tree_get_file_size;
        struct PVFS_servresp_tree_getattr tree_getattr;
        struct PVFS_servresp_mgmt_get_uid mgmt_get_uid;
        struct PVFS_servresp_tree_setattr tree_setattr; 
        struct PVFS_servresp_mgmt_get_dirent mgmt_get_dirent;
        struct PVFS_servresp_mgmt_get_user_cert mgmt_get_user_cert;
        struct PVFS_servresp_mgmt_get_user_cert_keyreq mgmt_get_user_cert_keyreq;
    } u;
};
endecode_fields_2_struct(
    PVFS_server_resp,
    enum, op,
    PVFS_error, status);

#endif /* __PVFS2_REQ_PROTO_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */
