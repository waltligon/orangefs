#include <jni.h>
#include "org_apache_hadoop_fs_pvfs2_PVFS2JNI.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h>
#include <getopt.h>
#include <pwd.h>
#include <grp.h>

#define __PINT_REQPROTO_ENCODE_FUNCS_C
#include "pvfs2.h"
#include "str-utils.h"
#include "xattr-utils.h"
#include "pint-sysint-utils.h"
#include "pint-request.h"
#include "pint-distribution.h"
#include "pvfs2-dist-basic.h"
#include "pvfs2-dist-simple-stripe.h"
#include "pvfs2-dist-twod-stripe.h"
#include "pvfs2-dist-varstrip.h"
#include "pint-util.h"
#include "pvfs2-internal.h"

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#ifdef HAVE_ATTR_XATTR_H
#include <attr/xattr.h>
#endif

#define MAX_DIR_ENTS 64
#define MAX_PVFS_STRERROR_LEN 256
#define PVFS_PATHNAME_MAX 2048

struct options 
{
    const char *srcfile;
    PVFS_offset offset;
    PVFS_size length;
};

enum object_type { 
    UNIX_FILE, 
    PVFS2_FILE 
};

typedef struct pvfs2_file_object_s {
    PVFS_fs_id fs_id;
    PVFS_object_ref ref;
    char pvfs2_path[PVFS_PATHNAME_MAX];
    char user_path[PVFS_PATHNAME_MAX];
    PVFS_sys_attr attr;
    PVFS_permissions perms;
} pvfs2_file_object;

typedef struct unix_file_object_s {
    int fd;
    int mode;
    char path[PVFS_NAME_MAX];
    PVFS_fs_id fs_id;
    struct stat stat;
} unix_file_object;

typedef struct file_object_s {
    int fs_type;
    union {
    unix_file_object ufs;
    pvfs2_file_object pvfs2;
    } u;
} file_object;

#define XATTR_BUF_SIZE 4096
#define PVFS_SERVER_MAX 1024

static int resolve_filename(file_object *obj, const char *filename);
static int generic_open(file_object *obj, PVFS_credentials *credentials);
static int generic_server_location(file_object *obj, PVFS_credentials *creds, char **servers, PVFS_handle *handles, int *nservers);
static int generic_dist(file_object *obj, PVFS_credentials *creds, char **dist, int *size);

/* metafile distribution */
#define DIST_KEY "system.pvfs2." METAFILE_DIST_KEYSTR
/* datafile handles */
#define DFILE_KEY "system.pvfs2." DATAFILE_HANDLES_KEYSTR

static int generic_dist(file_object *obj, PVFS_credentials *creds, char **dist, int *size)
{
    char *buffer = (char *) malloc(XATTR_BUF_SIZE);
    int ret;
    
    if (obj->fs_type == UNIX_FILE) {
#ifndef HAVE_FGETXATTR_EXTRA_ARGS
        if ((ret = fgetxattr(obj->u.ufs.fd, DIST_KEY, buffer, XATTR_BUF_SIZE)) < 0)
#else
        if ((ret = fgetxattr(obj->u.ufs.fd, DIST_KEY, buffer, XATTR_BUF_SIZE, 0, 0)) < 0)
#endif
        {
            free(buffer);
            return ret;
        }
        *size = ret;
    }
    else
    {
        PVFS_ds_keyval key, val;

        key.buffer = DIST_KEY;
        key.buffer_sz = strlen(DIST_KEY) + 1;
        val.buffer = buffer;
        val.buffer_sz = XATTR_BUF_SIZE;
        if ((ret = PVFS_sys_geteattr(obj->u.pvfs2.ref, creds, &key, &val, NULL)) < 0)
        {
            free(buffer);
            return ret;
        }
        *size = val.read_sz;
    }
    *dist = buffer;
    return 0;
}

static int generic_server_location(file_object *obj, PVFS_credentials *creds, char **servers, PVFS_handle *handles, int *nservers)
{
    char *buffer = (char *) malloc(XATTR_BUF_SIZE);
    int ret, num_dfiles, count;
    PVFS_fs_id fsid;
    
    if (obj->fs_type == UNIX_FILE)
    {
#ifndef HAVE_FGETXATTR_EXTRA_ARGS
        if ((ret = fgetxattr(obj->u.ufs.fd, DFILE_KEY, buffer, XATTR_BUF_SIZE)) < 0)
#else
        if ((ret = fgetxattr(obj->u.ufs.fd, DFILE_KEY, buffer, XATTR_BUF_SIZE, 0, 0)) < 0)
#endif
        {
            free(buffer);
            return ret;
        }
        fsid = obj->u.ufs.fs_id;
    }
    else
    {
        PVFS_ds_keyval key, val;

        key.buffer = DFILE_KEY;
        key.buffer_sz = strlen(DFILE_KEY) + 1;
        val.buffer = buffer;
        val.buffer_sz = XATTR_BUF_SIZE;
        if ((ret = PVFS_sys_geteattr(obj->u.pvfs2.ref, creds, &key, &val, NULL)) < 0)
        {
            free(buffer);
            return ret;
        }
        ret = val.read_sz;
        fsid = obj->u.pvfs2.fs_id;
    }
    /*
     * At this point, we know all the dfile handles 
     */
    num_dfiles = (ret / sizeof(PVFS_handle));
    count = num_dfiles < *nservers ? num_dfiles : *nservers;
    for (ret = 0; ret < count; ret++)
    {
        PVFS_handle *ptr = (PVFS_handle *) ((char *) buffer + ret * sizeof(PVFS_handle));
        servers[ret] = (char *) calloc(1, PVFS_MAX_SERVER_ADDR_LEN);
        handles[ret] = *ptr;
        if (servers[ret] == NULL)
        {
            break;
        }
        /* ignore any errors */
        PINT_cached_config_get_server_name(servers[ret], PVFS_MAX_SERVER_ADDR_LEN, *ptr, fsid);
    }
    if (ret != count)
    {
        int j;
        for (j = 0; j < ret; j++)
        {
            free(servers[j]);
            servers[j] = NULL;
        }
        return ret;
    }
    *nservers = count;
    return 0;
}

static int resolve_filename(file_object *obj, const char *filename)
{
    int ret;

    ret = PVFS_util_resolve(filename, &(obj->u.pvfs2.fs_id), obj->u.pvfs2.pvfs2_path, PVFS_PATHNAME_MAX);
    if (ret < 0)
    {
        obj->fs_type = UNIX_FILE;
        strncpy(obj->u.ufs.path, filename, PVFS_PATHNAME_MAX);
    } else {
        obj->fs_type = PVFS2_FILE;
        strncpy(obj->u.pvfs2.user_path, filename, PVFS_PATHNAME_MAX);
    }
    return 0;
}

static int generic_open(file_object *obj, PVFS_credentials *credentials)
{
    struct stat stat_buf;
    PVFS_sysresp_lookup resp_lookup;
    PVFS_sysresp_getattr resp_getattr;
    PVFS_object_ref ref;
    int ret = -1;

    if (obj->fs_type == UNIX_FILE)
    {
        PINT_statfs_t statfsbuf;
        memset(&stat_buf, 0, sizeof(struct stat));

        stat(obj->u.ufs.path, &stat_buf);
        if (!S_ISREG(stat_buf.st_mode))
        {
            fprintf(stderr, "Not a file!\n");
            return (-1);
        }
        obj->u.ufs.fd = open(obj->u.ufs.path, O_RDONLY);
        obj->u.ufs.mode = (int)stat_buf.st_mode;
        if (obj->u.ufs.fd < 0)
        {
            perror("open");
            fprintf(stderr, "could not open %s\n", obj->u.ufs.path);
            return (-1);
        }
        if (PINT_statfs_fd_lookup(obj->u.ufs.fd, &statfsbuf) < 0)
        {
            perror("fstatfs:");
            fprintf(stderr, "could not fstatfs %s\n", obj->u.ufs.path);
        }
        memcpy(&obj->u.ufs.fs_id, &PINT_statfs_fsid(&statfsbuf), 
               sizeof(PINT_statfs_fsid(&statfsbuf)));
        memcpy(&obj->u.ufs.stat, &stat_buf, sizeof(struct stat));
        return 0;
    }
    else
    {
        memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
        ret = PVFS_sys_lookup(obj->u.pvfs2.fs_id, 
                              (char *) obj->u.pvfs2.pvfs2_path,
                              credentials, 
                              &resp_lookup,
                              PVFS2_LOOKUP_LINK_FOLLOW, NULL);
        if (ret < 0)
        {
            PVFS_perror("PVFS_sys_lookup", ret);
            return ret;
        }
        ref.handle = resp_lookup.ref.handle;
        ref.fs_id = resp_lookup.ref.fs_id;

        memset(&resp_getattr, 0, sizeof(PVFS_sysresp_getattr));
        ret = PVFS_sys_getattr(ref, PVFS_ATTR_SYS_ALL_NOHINT,
                               credentials, &resp_getattr, NULL);
        if (ret)
        {
            fprintf(stderr, "Failed to do pvfs2 getattr on %s\n",
                    obj->u.pvfs2.pvfs2_path);
            return ret;
        }

        if (resp_getattr.attr.objtype != PVFS_TYPE_METAFILE)
        {
            fprintf(stderr, "Not a meta file!\n");
            return (-1);
        }
        obj->u.pvfs2.perms = resp_getattr.attr.perms;
        memcpy(&obj->u.pvfs2.attr, &resp_getattr.attr,
               sizeof(PVFS_sys_attr));
        obj->u.pvfs2.attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
        obj->u.pvfs2.ref = ref;
    }
    return 0;
}

static int parse_flowproto_string(
    const char *input,
    enum PVFS_flowproto_type *flowproto)
{
    int ret = 0;
    char *start = NULL;
    char flow[256];
    char *comma = NULL;

    start = strstr(input, "flowproto");
    /* we must find a match if this function is being called... */
    assert(start);

    /* scan out the option */
    ret = sscanf(start, "flowproto = %255s ,", flow);
    if (ret != 1)
    {
        gossip_err("Error: malformed flowproto option in tab file.\n");
        return (-PVFS_EINVAL);
    }

    /* chop it off at any trailing comma */
    comma = index(flow, ',');
    if (comma)
    {
        comma[0] = '\0';
    }

    if (!strcmp(flow, "dump_offsets"))
    {
        *flowproto = FLOWPROTO_DUMP_OFFSETS;
    }
    else if (!strcmp(flow, "bmi_cache"))
    {
        *flowproto = FLOWPROTO_BMI_CACHE;
    }
    else if (!strcmp(flow, "multiqueue"))
    {
        *flowproto = FLOWPROTO_MULTIQUEUE;
    }
    else
    {
        gossip_err("Error: unrecognized flowproto option: %s\n", flow);
        return (-PVFS_EINVAL);
    }
    return 0;
}

static int parse_encoding_string(
    const char *cp,
    enum PVFS_encoding_type *et)
{
    int i = 0;
    const char *cq = NULL;

    struct
    {
        const char *name;
        enum PVFS_encoding_type val;
    } enc_str[] =
        { { "default", ENCODING_DEFAULT },
          { "defaults", ENCODING_DEFAULT },
          { "direct", ENCODING_DIRECT },
          { "le_bfield", ENCODING_LE_BFIELD },
          { "xdr", ENCODING_XDR } };

    gossip_debug(GOSSIP_CLIENT_DEBUG, "%s: input is %s\n",
                 __func__, cp);
    cp += strlen("encoding");
    for (; isspace(*cp); cp++);        /* optional spaces */
    if (*cp != '=')
    {
        gossip_err("Error: %s: malformed encoding option in tab file.\n",
                   __func__);
        return -PVFS_EINVAL;
    }
    for (++cp; isspace(*cp); cp++);        /* optional spaces */
    for (cq = cp; *cq && *cq != ','; cq++);/* find option end */

    *et = -1;
    for (i = 0; i < sizeof(enc_str) / sizeof(enc_str[0]); i++)
    {
        int n = strlen(enc_str[i].name);
        if (cq - cp > n)
            n = cq - cp;
        if (!strncmp(enc_str[i].name, cp, n))
        {
            *et = enc_str[i].val;
            break;
        }
    }
    if (*et == -1)
    {
        gossip_err("Error: %s: unknown encoding type in tab file.\n",
                   __func__);
        return -PVFS_EINVAL;
    }
    return 0;
}

static void setReference(JNIEnv *env, jobject objReference, PVFS_object_ref reference) {
    jclass cls;
    jfieldID fid;
    
    cls = (*env)->GetObjectClass(env, objReference);
    fid = (*env)->GetFieldID(env, cls, "handle", "J");
    (*env)->SetLongField(env, objReference, fid, (jlong)reference.handle);
    fid = (*env)->GetFieldID(env, cls, "filesystemID", "J");
    (*env)->SetLongField(env, objReference, fid, (jlong)reference.fs_id);
}

static void getReference(JNIEnv *env, jobject objReference, PVFS_object_ref *reference) {
    jclass cls;
    jfieldID fid;
    
    cls = (*env)->GetObjectClass(env, objReference);
    fid = (*env)->GetFieldID(env, cls, "handle", "J");
    reference->handle = (*env)->GetLongField(env, objReference, fid);
    fid = (*env)->GetFieldID(env, cls, "filesystemID", "J");
    reference->fs_id = (*env)->GetLongField(env, objReference, fid);
}

static void setAttribute(JNIEnv *env, jobject objAttr, PVFS_sys_attr attr) {
    jclass cls;
    jfieldID fid;
    struct passwd *pwd = NULL;
    struct group *grp = NULL;
    
    cls = (*env)->GetObjectClass(env, objAttr);
    fid = (*env)->GetFieldID(env, cls, "owner", "J");
    (*env)->SetLongField(env, objAttr, fid, (jlong)attr.owner);
    pwd = getpwuid((uid_t)attr.owner);
    if(pwd){
        jstring jstr;
        
        fid = (*env)->GetFieldID(env, cls, "ownerS", "Ljava/lang/String;");
        jstr = (*env)->NewStringUTF(env, pwd->pw_name);
        if(jstr != NULL)
            (*env)->SetObjectField(env, objAttr, fid, jstr);
    }
    fid = (*env)->GetFieldID(env, cls, "group", "J");
    (*env)->SetLongField(env, objAttr, fid, (jlong)attr.group);
    grp = getgrgid((gid_t)attr.group);
    if(grp){
        jstring jstr;
        
        fid = (*env)->GetFieldID(env, cls, "groupS", "Ljava/lang/String;");
        jstr = (*env)->NewStringUTF(env, grp->gr_name);
        if(jstr != NULL)
            (*env)->SetObjectField(env, objAttr, fid, jstr);
    }
    fid = (*env)->GetFieldID(env, cls, "permissions", "J");
    (*env)->SetLongField(env, objAttr, fid, (jlong)attr.perms);
    fid = (*env)->GetFieldID(env, cls, "accessTime", "J");
    (*env)->SetLongField(env, objAttr, fid, (jlong)attr.atime);
    fid = (*env)->GetFieldID(env, cls, "creationTime", "J");
    (*env)->SetLongField(env, objAttr, fid, (jlong)attr.ctime);
    fid = (*env)->GetFieldID(env, cls, "modificationTime", "J");
    (*env)->SetLongField(env, objAttr, fid, (jlong)attr.mtime);
    fid = (*env)->GetFieldID(env, cls, "size", "J");
    (*env)->SetLongField(env, objAttr, fid, (jlong)attr.size);
    fid = (*env)->GetFieldID(env, cls, "type", "B");
    (*env)->SetLongField(env, objAttr, fid, (jbyte)attr.objtype);
}

static int lookup(char *full_path, PVFS_object_ref *ret_ref) {
    char pvfs2_path[PVFS_PATHNAME_MAX];
    PVFS_fs_id fs_id;
    PVFS_sysresp_lookup resp_lookup;
    int ret;
    PVFS_credentials credentials;

    PVFS_util_gen_credentials(&credentials);
    ret = PVFS_util_resolve(full_path, &fs_id, pvfs2_path, PVFS_PATHNAME_MAX);
    if (ret < 0)
    {
        return ret;
    } else {
        memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
        ret = PVFS_sys_lookup(fs_id, 
                              pvfs2_path,
                              &credentials, 
                              &resp_lookup,
//TODO Fix symbolic link
//                              PVFS2_LOOKUP_LINK_FOLLOW);
                              PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);
        if (ret < 0)
        {
            return ret;
        }
        ret_ref->handle = resp_lookup.ref.handle;
        ret_ref->fs_id = resp_lookup.ref.fs_id;
    }
    return 0;
}

static int lookupParent(char *full_path, PVFS_object_ref *ret_parent_ref, char *ret_filename) {
    int rc;
    int num_segs;
    char directory[PVFS_PATHNAME_MAX];
    char pvfs_path[PVFS_PATHNAME_MAX] = {0};
    PVFS_fs_id cur_fs;
    PVFS_sysresp_lookup resp_lookup;
    PVFS_credentials credentials;

    /* Translate path into pvfs2 relative path */
    rc = PINT_get_base_dir(full_path, directory, PVFS_PATHNAME_MAX);
    num_segs = PINT_string_count_segments(full_path);
    rc = PINT_get_path_element(full_path, num_segs - 1, ret_filename, PVFS_PATHNAME_MAX);

    if (rc)
    {
        return rc;
    }

    rc = PVFS_util_resolve(directory, &cur_fs, pvfs_path, PVFS_PATHNAME_MAX);
    if (rc)
    {
        return rc;
    }

    PVFS_util_gen_credentials(&credentials);

    memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
    rc = PVFS_sys_lookup(cur_fs, pvfs_path, &credentials, &resp_lookup, PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);
    if (rc)
    {
        return rc;
    }

    *ret_parent_ref = resp_lookup.ref;
    
    return 0;
}

static void throwPVFS2Exception(JNIEnv *env, const char *exceptionClsName, int error){
    char error_string[MAX_PVFS_STRERROR_LEN];
    PVFS_strerror_r(error, error_string, sizeof(error_string));
    (*env)->ThrowNew(env, (*env)->FindClass(env, exceptionClsName), error_string); 
}
// JAVA

JNIEXPORT jint JNICALL Java_org_apache_hadoop_fs_pvfs2_PVFS2JNI_native_1init(JNIEnv *env, jobject obj) {
    int64_t ret;

    ret = PVFS_sys_initialize(GOSSIP_NO_DEBUG);
    if (ret < 0)
    {
        return(ret);
    }
    return 0;
}
  
JNIEXPORT void JNICALL Java_org_apache_hadoop_fs_pvfs2_PVFS2JNI_native_1lookup(JNIEnv *env, jclass obj, jstring jFilename, jobject objReference) {
    PVFS_object_ref ref;
    int ret;
    char filename[PVFS_PATHNAME_MAX];
    strcpy(filename, (*env)->GetStringUTFChars(env, jFilename, NULL));
    ret = lookup(filename, &ref);

    if (ret < 0)
    {
        throwPVFS2Exception(env, "java/io/FileNotFoundException", ret); 
    } else {
        setReference(env, objReference, ref);
    }
    return ;
}
  
JNIEXPORT void JNICALL Java_org_apache_hadoop_fs_pvfs2_PVFS2JNI_native_1create(JNIEnv *env, jclass obj, jstring jFilename, jobject objReference) {
    int rc;
    char working_file[PVFS_PATHNAME_MAX];
    char filename[PVFS_PATHNAME_MAX];

    PVFS_sysresp_create resp_create;
    PVFS_credentials credentials;
    PVFS_object_ref parent_ref;
    PVFS_sys_attr attr;

    strcpy(working_file, (*env)->GetStringUTFChars(env, jFilename, NULL));
    rc = lookupParent(working_file, &parent_ref, filename);
    if (rc)
    {
        throwPVFS2Exception(env, "java/io/FileNotFoundException", rc);
        printf("Here1"); 
        return ;
    }

    PVFS_util_gen_credentials(&credentials);
    /* Set attributes */
    memset(&attr, 0, sizeof(PVFS_sys_attr));
    attr.owner = credentials.uid;
    attr.group = credentials.gid;
    attr.perms = 0644;
    attr.atime = time(NULL);
    attr.mtime = attr.atime;
    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
    attr.dfile_count = 0;

    rc = PVFS_sys_create(filename, parent_ref, attr, &credentials, NULL, &resp_create, NULL, NULL);
    if (rc)
    {
        throwPVFS2Exception(env, "java/io/FileNotFoundException", rc); 
        printf("Here2"); 
        return ;
    }
    setReference(env, objReference, resp_create.ref);
}

JNIEXPORT jint JNICALL Java_org_apache_hadoop_fs_pvfs2_PVFS2JNI_native_1read(JNIEnv *env, jclass obj, jobject objReference, jbyteArray arrBuffer, jlong jOffset, jlong jCount){
    PVFS_Request mem_req;
    PVFS_sysresp_io resp_io;
    int ret;
    PVFS_object_ref ref;
    PVFS_credentials credentials;
    void *buffer;

    PVFS_util_gen_credentials(&credentials);
    getReference(env, objReference, &ref);
        
    ret = PVFS_Request_contiguous((size_t)jCount, PVFS_BYTE, &mem_req);
    if (ret < 0)
    {
        return -1;
    }

    buffer = (void *)(*env)->GetByteArrayElements(env, arrBuffer, NULL);        
    if (buffer == NULL) {
        (*env)->ThrowNew(env, (*env)->FindClass(env, "java/io/IOException"), "Error GetIntArrayElements");
        return -1;
    }
    ret = PVFS_sys_read(ref, PVFS_BYTE, (int64_t)jOffset, buffer, mem_req, &credentials, &resp_io, NULL);
    if (ret == 0)
    {
        (*env)->ReleaseByteArrayElements(env, arrBuffer, buffer, 0);
        PVFS_Request_free(&mem_req);
        return (resp_io.total_completed);
    } 
    (*env)->ReleaseByteArrayElements(env, arrBuffer, buffer, 0);
    return -1;
}

JNIEXPORT jint JNICALL Java_org_apache_hadoop_fs_pvfs2_PVFS2JNI_native_1write(JNIEnv *env, jclass obj, jobject objReference, jbyteArray arrBuffer, jlong jOffset, jlong jCount){
    PVFS_Request mem_req;
    PVFS_sysresp_io resp_io;
    int ret;
    PVFS_object_ref ref;
    PVFS_credentials credentials;
    void *buffer;

    PVFS_util_gen_credentials(&credentials);
    getReference(env, objReference, &ref);
        
    ret = PVFS_Request_contiguous((size_t)jCount, PVFS_BYTE, &mem_req);
    if (ret < 0)
    {
        return -1;
    }
    
    buffer = (void *)(*env)->GetByteArrayElements(env, arrBuffer, NULL);        
    if (buffer == NULL) {
        (*env)->ThrowNew(env, (*env)->FindClass(env, "java/io/IOException"), "Error GetIntArrayElements");
        return -1;
    }
    ret = PVFS_sys_write(ref, PVFS_BYTE, (int64_t)jOffset, buffer, mem_req, &credentials, &resp_io, NULL);
    if (ret == 0)
    {
        (*env)->ReleaseByteArrayElements(env, arrBuffer, buffer, 0);
        PVFS_Request_free(&mem_req);
        return (resp_io.total_completed);
    } 
    (*env)->ReleaseByteArrayElements(env, arrBuffer, buffer, 0);
    return -1;
}

JNIEXPORT void JNICALL Java_org_apache_hadoop_fs_pvfs2_PVFS2JNI_native_1flush(JNIEnv *env, jclass obj, jobject objReference){
    PVFS_object_ref ref;
    PVFS_credentials credentials;

    PVFS_util_gen_credentials(&credentials);
    getReference(env, objReference, &ref);
    PVFS_sys_flush(ref, &credentials, NULL);
}

JNIEXPORT void JNICALL Java_org_apache_hadoop_fs_pvfs2_PVFS2JNI_native_1truncate(JNIEnv *env, jclass obj, jobject objReference){
    PVFS_object_ref ref;
    PVFS_credentials credentials;

    PVFS_util_gen_credentials(&credentials);
    getReference(env, objReference, &ref);
    PVFS_sys_truncate(ref, 0, &credentials, NULL	);
}

JNIEXPORT jlong JNICALL Java_org_apache_hadoop_fs_pvfs2_PVFS2JNI_native_1getFilesize(JNIEnv *env, jclass obj, jobject objReference){
    int ret = -1;
    PVFS_object_ref ref;
    PVFS_credentials credentials;
    PVFS_sysresp_getattr getattr_response;

    PVFS_util_gen_credentials(&credentials);
    getReference(env, objReference, &ref);
    memset(&getattr_response,0, sizeof(PVFS_sysresp_getattr));
    PVFS_util_gen_credentials(&credentials);

    ret = PVFS_sys_getattr(ref, PVFS_ATTR_SYS_SIZE, &credentials, &getattr_response, NULL);
    if (ret)
    {
        throwPVFS2Exception(env, "java/io/IOException", ret); 
        fprintf(stderr,"Failed to get attributes on handle %llu,%d\n", llu(ref.handle), ref.fs_id);
        PVFS_perror("Getattr failure", ret);
        return 0;
    }
    
    return getattr_response.attr.size;
}

JNIEXPORT jlong JNICALL Java_org_apache_hadoop_fs_pvfs2_PVFS2JNI_native_1getBlocksize(JNIEnv *env, jclass obj, jstring jFileName){
    file_object src;
    PINT_dist *dist;
    char *dist_buf = NULL;
    int dist_size;
    int64_t ret;
    PVFS_credentials credentials;
    jlong blockSize = 0;

// JAVA
    const char *name;
    name = (*env)->GetStringUTFChars(env, jFileName, NULL);
    if (name == NULL) {
        return 0;       
    }
// JAVA

    memset(&dist, 0, sizeof(dist));

    memset(&src, 0, sizeof(src));

    resolve_filename(&src, name);

    PVFS_util_gen_credentials(&credentials);

    ret = generic_open(&src, &credentials);
    if (ret < 0)
    {
        throwPVFS2Exception(env, "java/io/IOException", ret); 
        fprintf(stderr, "Could not open %s\n", name);
        return 0;
    }

    ret = generic_dist(&src, &credentials, &dist_buf, &dist_size);
    if (ret < 0)
    {
        throwPVFS2Exception(env, "java/io/IOException", ret); 
        fprintf(stderr, "Not a PVFS2 file!\n");
        fprintf(stderr, "Could not read distribution information!\n");
        return 0;
    }

    /* okay now print out by deserializing the buffer */
    PINT_dist_decode(&dist, dist_buf);
    if(strcmp(dist->dist_name, PVFS_DIST_SIMPLE_STRIPE_NAME)==0) {
        blockSize = ((PVFS_simple_stripe_params *)dist->params)->strip_size;
    }
    else if(strcmp(dist->dist_name, PVFS_DIST_TWOD_STRIPE_NAME)==0) {
        blockSize = ((PVFS_twod_stripe_params *)dist->params)->strip_size;
    }
    else {
        (*env)->ThrowNew(env, (*env)->FindClass(env, "java/io/IOException"), "Unsupport distribution");         
    }
    
    PINT_dist_free(dist);
    return blockSize;
}

//TODO Testing
JNIEXPORT void JNICALL Java_org_apache_hadoop_fs_pvfs2_PVFS2JNI_native_1getAttribute(JNIEnv *env, jclass obj, jobject objReference, jobject objAttr){
    int ret = -1;
    PVFS_object_ref ref;
    PVFS_credentials credentials;
    PVFS_sysresp_getattr getattr_response;

    PVFS_util_gen_credentials(&credentials);
    getReference(env, objReference, &ref);
    memset(&getattr_response,0, sizeof(PVFS_sysresp_getattr));
    PVFS_util_gen_credentials(&credentials);

    ret = PVFS_sys_getattr(ref, PVFS_ATTR_SYS_COMMON_ALL | PVFS_ATTR_SYS_SIZE, &credentials, &getattr_response, NULL);
    if (ret)
    {
        throwPVFS2Exception(env, "java/io/IOException", ret); 
        fprintf(stderr,"Failed to get attributes on handle %llu,%d\n", llu(ref.handle), ref.fs_id);
        PVFS_perror("Getattr failure", ret);
        return ;
    }
    setAttribute(env, objAttr, getattr_response.attr);
}

//TODO Testing
JNIEXPORT void JNICALL Java_org_apache_hadoop_fs_pvfs2_PVFS2JNI_native_1setOwner(JNIEnv *env, jclass obj, jobject objReference, jlong jUID, jlong jGID){
    int ret = -1;
    PVFS_object_ref ref;
    PVFS_credentials credentials;
    PVFS_sysresp_getattr getattr_response;
    PVFS_sys_attr new_attr;
    
    PVFS_util_gen_credentials(&credentials);
    getReference(env, objReference, &ref);
    memset(&getattr_response,0, sizeof(PVFS_sysresp_getattr));
    PVFS_util_gen_credentials(&credentials);

    ret = PVFS_sys_getattr(ref, PVFS_ATTR_SYS_ALL_SETABLE, &credentials, &getattr_response, NULL);
    if (ret)
    {
        throwPVFS2Exception(env, "java/io/IOException", ret); 
        fprintf(stderr,"Failed to get attributes on handle %llu,%d\n", llu(ref.handle), ref.fs_id);
        PVFS_perror("Getattr failure", ret);
        return ;
    }
    new_attr = getattr_response.attr;
    new_attr.owner = (PVFS_uid)jUID;
    new_attr.group = (PVFS_gid)jGID;
    new_attr.mask = PVFS_ATTR_SYS_UID | PVFS_ATTR_SYS_GID;
 
    ret = PVFS_sys_setattr(ref, new_attr, &credentials, NULL);
    if (ret < 0) 
    {
        throwPVFS2Exception(env, "java/io/IOException", ret); 
        PVFS_perror("PVFS_sys_setattr",ret);
        return ;
    }
}

//TODO Testing
JNIEXPORT void JNICALL Java_org_apache_hadoop_fs_pvfs2_PVFS2JNI_native_1setPermissions(JNIEnv *env, jclass obj, jobject objReference, jlong perms){
    int ret = -1;
    PVFS_object_ref ref;
    PVFS_credentials credentials;
    PVFS_sysresp_getattr getattr_response;
    PVFS_sys_attr new_attr;
    
    PVFS_util_gen_credentials(&credentials);
    getReference(env, objReference, &ref);
    memset(&getattr_response,0, sizeof(PVFS_sysresp_getattr));
    PVFS_util_gen_credentials(&credentials);

    ret = PVFS_sys_getattr(ref, PVFS_ATTR_SYS_ALL_SETABLE, &credentials, &getattr_response, NULL);
    if (ret)
    {
        throwPVFS2Exception(env, "java/io/IOException", ret); 
        fprintf(stderr,"Failed to get attributes on handle %llu,%d\n", llu(ref.handle), ref.fs_id);
        PVFS_perror("Getattr failure", ret);
        return ;
    }
    new_attr = getattr_response.attr;
    new_attr.perms = (PVFS_permissions)perms;
    new_attr.mask = PVFS_ATTR_SYS_PERM;
 
    ret = PVFS_sys_setattr(ref, new_attr, &credentials, NULL);
    if (ret < 0) 
    {
        throwPVFS2Exception(env, "java/io/IOException", ret); 
        PVFS_perror("PVFS_sys_setattr",ret);
        return ;
    }
}

JNIEXPORT void JNICALL Java_org_apache_hadoop_fs_pvfs2_PVFS2JNI_native_1listFiles(JNIEnv *env, jclass obj, jstring jDirName, jobject objFiles){
    PVFS_object_ref ref;
    char dirname[PVFS_PATHNAME_MAX];
    int ret = -1;
    int i;
    PVFS_sysresp_readdir rd_response;
    PVFS_credentials credentials;
    PVFS_ds_position token;
    uint64_t dir_version = 0;

// JAVA
    jclass cls;
    jmethodID mid;

    cls = (*env)->GetObjectClass(env, objFiles);
    mid = (*env)->GetMethodID(env, cls, "add", "(Ljava/lang/Object;)Z");
    if (mid == NULL) {
        return ;
    }
// JAVA

    strcpy(dirname, (*env)->GetStringUTFChars(env, jDirName, NULL));
    ret = lookup(dirname, &ref);

    if (ret < 0)
    {
        throwPVFS2Exception(env, "java/io/FileNotFoundException", ret);
    } else {
        token = 0;
        PVFS_util_gen_credentials(&credentials);
        do
        {
            memset(&rd_response, 0, sizeof(PVFS_sysresp_readdir));
            ret = PVFS_sys_readdir(ref, (!token ? PVFS_READDIR_START : token), MAX_DIR_ENTS, &credentials, &rd_response, NULL);
            if(ret < 0)
            {
                throwPVFS2Exception(env, "java/io/IOException", ret);
                return ;
            }
    
            if (dir_version == 0)
            {
                dir_version = rd_response.directory_version;
            }
            else if (dir_version != rd_response.directory_version)
            {
                (*env)->ThrowNew(env, (*env)->FindClass(env, "java/io/IOException"), "Directory has been modified during a readdir operation"); 
                return ;
            }
    
            for(i = 0; i < rd_response.pvfs_dirent_outcount; i++)
            {
                // JAVA
                (*env)->CallBooleanMethod(env, objFiles, mid, (*env)->NewStringUTF(env, rd_response.dirent_array[i].d_name));
                // JAVA
            }
            token += rd_response.pvfs_dirent_outcount;
    
            if (rd_response.pvfs_dirent_outcount)
            {
                free(rd_response.dirent_array);
                rd_response.dirent_array = NULL;
            }   
        } while(rd_response.pvfs_dirent_outcount == MAX_DIR_ENTS);
    }
    return ;    
}

JNIEXPORT void JNICALL Java_org_apache_hadoop_fs_pvfs2_PVFS2JNI_native_1rename(JNIEnv *env, jclass obj, jstring jOrigName, jstring jNewName){
    int rc;
    char orig_working_file[PVFS_PATHNAME_MAX];
    char orig_filename[PVFS_PATHNAME_MAX];
    PVFS_object_ref orig_parent_ref;
    char new_working_file[PVFS_PATHNAME_MAX];
    char new_filename[PVFS_PATHNAME_MAX];
    PVFS_object_ref new_parent_ref;
    PVFS_credentials credentials;

    strcpy(orig_working_file, (*env)->GetStringUTFChars(env, jOrigName, NULL));
    rc = lookupParent(orig_working_file, &orig_parent_ref, orig_filename);
    if (rc)
    {
        throwPVFS2Exception(env, "java/io/FileNotFoundException", rc); 
        return ;
    }

    strcpy(new_working_file, (*env)->GetStringUTFChars(env, jNewName, NULL));
    rc = lookupParent(new_working_file, &new_parent_ref, new_filename);
    if (rc)
    {
        throwPVFS2Exception(env, "java/io/FileNotFoundException", rc); 
        return ;
    }

    PVFS_util_gen_credentials(&credentials);
    rc = PVFS_sys_rename(orig_filename, orig_parent_ref, new_filename, new_parent_ref, &credentials, NULL);
    if (rc)
    {
        throwPVFS2Exception(env, "java/io/IOException", rc); 
        return ;
    }
}

JNIEXPORT void JNICALL Java_org_apache_hadoop_fs_pvfs2_PVFS2JNI_native_1mkdir(JNIEnv *env, jclass obj, jstring jDirName){
    int rc;
    char working_file[PVFS_PATHNAME_MAX];
    char dirname[PVFS_PATHNAME_MAX];

    PVFS_sysresp_mkdir  resp_mkdir;
    PVFS_credentials credentials;
    PVFS_object_ref parent_ref;
    PVFS_sys_attr attr;

    strcpy(working_file, (*env)->GetStringUTFChars(env, jDirName, NULL));
    rc = lookupParent(working_file, &parent_ref, dirname);
    if (rc)
    {
        throwPVFS2Exception(env, "java/io/FileNotFoundException", rc); 
        return ;
    }

    /* Set attributes */
    PVFS_util_gen_credentials(&credentials);
    memset(&attr, 0, sizeof(PVFS_sys_attr));
    attr.owner = credentials.uid;
    attr.group = credentials.gid;
    attr.perms = 0755;
    attr.atime = time(NULL);
    attr.mtime = attr.atime;
    attr.mask = PVFS_ATTR_SYS_ALL_SETABLE;
    attr.dfile_count = 0;

    rc = PVFS_sys_mkdir(dirname, parent_ref, attr, &credentials, &resp_mkdir, NULL);
    if (rc)
    {
        throwPVFS2Exception(env, "java/io/FileNotFoundException", rc); 
        return ;
    }
}

JNIEXPORT void JNICALL Java_org_apache_hadoop_fs_pvfs2_PVFS2JNI_native_1rm(JNIEnv *env, jclass obj, jstring jFileName){
    int rc;
    char working_file[PVFS_PATHNAME_MAX];
    char filename[PVFS_PATHNAME_MAX];
    PVFS_credentials credentials;
    PVFS_object_ref parent_ref;

    strcpy(working_file, (*env)->GetStringUTFChars(env, jFileName, NULL));
    rc = lookupParent(working_file, &parent_ref, filename);
    if (rc)
    {
        throwPVFS2Exception(env, "java/io/FileNotFoundException", rc); 
        return ;
    }

    PVFS_util_gen_credentials(&credentials);
    rc = PVFS_sys_remove(filename, parent_ref, &credentials, NULL);
    if (rc)
    {
        throwPVFS2Exception(env, "java/io/IOException", rc); 
        return ;
    }
}

JNIEXPORT void JNICALL Java_org_apache_hadoop_fs_pvfs2_PVFS2JNI_native_1mount(JNIEnv *env, jobject obj, jstring jMountPoint, jstring jFSName, jstring jBMIAddress, jstring jOptions) {
    const char *mountPoint;
    const char *bmiAddress;
    const char *fsName;
    const char *options;
    int ret;
    
    mountPoint = (*env)->GetStringUTFChars(env, jMountPoint, NULL);
    bmiAddress = (*env)->GetStringUTFChars(env, jBMIAddress, NULL);
    fsName = (*env)->GetStringUTFChars(env, jFSName, NULL);
    options = (*env)->GetStringUTFChars(env, jOptions, NULL);
    
    struct PVFS_sys_mntent* tmp_ent = NULL;

    tmp_ent = (struct PVFS_sys_mntent*)malloc(sizeof(struct PVFS_sys_mntent));
    if(!tmp_ent)
    {
        return ;
    }
    memset(tmp_ent, 0, sizeof(struct PVFS_sys_mntent));

    tmp_ent->num_pvfs_config_servers = 1;
    tmp_ent->pvfs_config_servers = (char**)malloc(sizeof(char*));
    if(!tmp_ent->pvfs_config_servers)
    {
        free(tmp_ent);
        return ;
    }

    tmp_ent->pvfs_config_servers[0] = strdup(bmiAddress);
    if(!tmp_ent->pvfs_config_servers[0])
    {
        free(tmp_ent->pvfs_config_servers);
        free(tmp_ent);
        return ;
    }

    tmp_ent->pvfs_fs_name = strdup(fsName);
    if(!tmp_ent->pvfs_fs_name)
    {
        free(tmp_ent->pvfs_config_servers[0]);
        free(tmp_ent->pvfs_config_servers);
        free(tmp_ent);
        return ;
    }

    tmp_ent->mnt_dir = strdup(mountPoint);
    if(!tmp_ent->mnt_dir)
    {
        free(tmp_ent->pvfs_fs_name);
        free(tmp_ent->pvfs_config_servers[0]);
        free(tmp_ent->pvfs_config_servers);
        free(tmp_ent);
        return ;
    }

    tmp_ent->mnt_opts = strdup(options);
    if(!tmp_ent->mnt_opts)
    {
        free(tmp_ent->mnt_dir);
        free(tmp_ent->pvfs_fs_name);
        free(tmp_ent->pvfs_config_servers[0]);
        free(tmp_ent->pvfs_config_servers);
        free(tmp_ent);
        return ;
    }

    tmp_ent->integrity_check = 1;
    tmp_ent->the_pvfs_config_server = tmp_ent->pvfs_config_servers[0];
    if(strstr(tmp_ent->mnt_opts, "flowproto")){
        ret = parse_flowproto_string(tmp_ent->mnt_opts, &(tmp_ent->flowproto));
        if (ret < 0)
        {
            tmp_ent->flowproto = FLOWPROTO_DEFAULT;
        }
    }
    else {
        tmp_ent->flowproto = FLOWPROTO_DEFAULT;
    }
    
    if(strstr(tmp_ent->mnt_opts, "encoding")){
        ret = parse_encoding_string(tmp_ent->mnt_opts, &(tmp_ent->encoding));
        if (ret < 0)
        {
            tmp_ent->encoding = ENCODING_DEFAULT;
        }
    }
    else {
        tmp_ent->encoding = ENCODING_DEFAULT;
    }

/*
    fprintf(stderr, "Server# = %d\n", tmp_ent->num_pvfs_config_servers);
    fprintf(stderr, "Server  = %s\n", tmp_ent->pvfs_config_servers[0]);
    fprintf(stderr, "FSName  = %s\n", tmp_ent->pvfs_fs_name);
    fprintf(stderr, "Dirs    = %s\n", tmp_ent->mnt_dir);
    fprintf(stderr, "Opts    = %s\n", tmp_ent->mnt_opts);
*/

    PVFS_sys_fs_add(tmp_ent);   
}

JNIEXPORT void JNICALL Java_org_apache_hadoop_fs_pvfs2_PVFS2JNI_native_1generateHint(JNIEnv *env, jobject obj, jobject objHint, jstring jName, jlong jOffset, jlong jLength) {
    file_object src;
    PINT_dist *dist;
    char *dist_buf = NULL;
    int dist_size;
    int64_t ret;
    PVFS_credentials credentials;
    char *servers[PVFS_SERVER_MAX];
    PVFS_handle handles[PVFS_SERVER_MAX];
    int i, nservers = PVFS_SERVER_MAX, block;
    PVFS_size file_size, length;
    PINT_request_file_data fd;
    PVFS_offset offset, poffset;

// JAVA
    const char *name;
    jclass cls;
    jmethodID mid;

    name = (*env)->GetStringUTFChars(env, jName, NULL);
    if (name == NULL) {
        return ;        
    }
    cls = (*env)->GetObjectClass(env, objHint);
    mid = (*env)->GetMethodID(env, cls, "insertBlockHint", "(Ljava/lang/String;JJ)V");
    if (mid == NULL) {
        return ;
    }
// JAVA

    memset(&dist, 0, sizeof(dist));

    memset(&src, 0, sizeof(src));

    resolve_filename(&src, name);

    PVFS_util_gen_credentials(&credentials);

    ret = generic_open(&src, &credentials);
    if (ret < 0)
    {
        throwPVFS2Exception(env, "java/io/IOException", ret); 
        fprintf(stderr, "Could not open %s\n", name);
        goto main_out;
    }

    ret = generic_dist(&src, &credentials, &dist_buf, &dist_size);
    if (ret < 0)
    {
        throwPVFS2Exception(env, "java/io/IOException", ret); 
        fprintf(stderr, "Not a PVFS2 file!\n");
        fprintf(stderr, "Could not read distribution information!\n");
        goto main_out;
    }
    ret = generic_server_location(&src, &credentials, servers, handles, &nservers);
    if (ret < 0)
    {
        throwPVFS2Exception(env, "java/io/IOException", ret); 
        fprintf(stderr, "Not a PVFS2 file!\n");
        fprintf(stderr, "Could not read server location information!\n");
        goto main_out;
    }

    if (src.fs_type == UNIX_FILE)
    {
    file_size = src.u.ufs.stat.st_size;
    }
    else
    {
    file_size = src.u.pvfs2.attr.size;
    }

    if(jLength==-1)
    {
    jLength = file_size;
    }
    else
    {
    jLength += jOffset;
    if(jLength > file_size)
        jLength = file_size;
    }

    /* okay now print out by deserializing the buffer */
    PINT_dist_decode(&dist, dist_buf);

    fd.fsize = file_size;
    fd.server_nr = 0;
    fd.server_ct = nservers;
    fd.dist = dist;
    fd.extend_flag = 1;

    block = 0;
    for(fd.server_nr = 0; fd.server_nr < fd.server_ct; fd.server_nr++)
    {
        for(offset = jOffset; offset < jLength; offset += length)
        {
            offset = dist->methods->next_mapped_offset(dist->params, &fd, offset);
            poffset = dist->methods->logical_to_physical_offset(dist->params, &fd, offset);
            length = dist->methods->contiguous_length(dist->params, &fd, poffset);
            if(offset<jLength)
            {
            block++;
            if(offset+length > jLength)
                {
                length = jLength-offset;
            }
            // JAVA
            (*env)->CallVoidMethod(env, objHint, mid, (*env)->NewStringUTF(env, servers[fd.server_nr]), llu(offset), llu(offset+length-1));
            // JAVA
            }
            else 
            {
            break;
            }
        }
    }

    for (i = 0; i < nservers; i++)
    {
        free(servers[i]);
    }
    PINT_dist_free(dist);

main_out:
    return ;
}

JNIEXPORT void JNICALL Java_org_apache_hadoop_fs_pvfs2_PVFS2JNI_native_1generateLayout(JNIEnv *env, jobject obj, jobject objLayout, jstring jName) {
    file_object src;
    PINT_dist *dist;
    char *dist_buf = NULL;
    int dist_size;
    int64_t ret;
    PVFS_credentials credentials;
    char *servers[PVFS_SERVER_MAX];
    PVFS_handle handles[PVFS_SERVER_MAX];
    int i, nservers = PVFS_SERVER_MAX, block;
    PVFS_size file_size, length;
    PINT_request_file_data fd;
    PVFS_offset offset, poffset;

// JAVA
    const char *name;
    jclass cls;
    jmethodID mid;

    name = (*env)->GetStringUTFChars(env, jName, NULL);
    if (name == NULL) {
        return ;        
    }
    cls = (*env)->GetObjectClass(env, objLayout);
    mid = (*env)->GetMethodID(env, cls, "insertBlockLayout", "(Ljava/lang/String;JJ)V");
    if (mid == NULL) {
        return ;
    }
// JAVA

    memset(&dist, 0, sizeof(dist));

    memset(&src, 0, sizeof(src));

    resolve_filename(&src, name);

    PVFS_util_gen_credentials(&credentials);

    ret = generic_open(&src, &credentials);
    if (ret < 0)
    {
        throwPVFS2Exception(env, "java/io/IOException", ret); 
        fprintf(stderr, "Could not open %s\n", name);
        goto main_out;
    }

    ret = generic_dist(&src, &credentials, &dist_buf, &dist_size);
    if (ret < 0)
    {
        throwPVFS2Exception(env, "java/io/IOException", ret); 
        fprintf(stderr, "Not a PVFS2 file!\n");
        fprintf(stderr, "Could not read distribution information!\n");
        goto main_out;
    }
    ret = generic_server_location(&src, &credentials, servers, handles, &nservers);
    if (ret < 0)
    {
        throwPVFS2Exception(env, "java/io/IOException", ret); 
        fprintf(stderr, "Not a PVFS2 file!\n");
        fprintf(stderr, "Could not read server location information!\n");
        goto main_out;
    }

    if (src.fs_type == UNIX_FILE)
    {
    file_size = src.u.ufs.stat.st_size;
    }
    else
    {
    file_size = src.u.pvfs2.attr.size;
    }

    /* okay now print out by deserializing the buffer */
    PINT_dist_decode(&dist, dist_buf);

    fd.fsize = file_size;
    fd.server_nr = 0;
    fd.server_ct = nservers;
    fd.dist = dist;
    fd.extend_flag = 1;

    block = 0;
    for(fd.server_nr = 0; fd.server_nr < fd.server_ct; fd.server_nr++)
    {
            offset = dist->methods->next_mapped_offset(dist->params, &fd, 0);
            poffset = dist->methods->logical_to_physical_offset(dist->params, &fd, offset);
            length = dist->methods->contiguous_length(dist->params, &fd, poffset);
            // JAVA
            (*env)->CallVoidMethod(env, objLayout, mid, (*env)->NewStringUTF(env, servers[fd.server_nr]), llu(offset), llu(offset+length-1));
            // JAVA
    }

    for (i = 0; i < nservers; i++)
    {
        free(servers[i]);
    }
    PINT_dist_free(dist);

main_out:
    return ;
}

