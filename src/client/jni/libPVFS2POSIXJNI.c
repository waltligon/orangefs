/*
 * (C) 2011 Clemson University
 *
 * See COPYING in top-level directory.
 */

#include "libPVFS2JNI_common.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pvfs2-hint.h>
#include <pvfs2-types.h>
#include <pvfs2-usrint.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/vfs.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <utime.h>
#include "org_orangefs_usrint_PVFS2POSIXJNI.h"

/* Forward Declarations */
static int fill_stat(JNIEnv *env, struct stat *ptr, jobject *inst);
//static int fill_statfs(JNIEnv *env, struct statfs *ptr, jobject *inst);

/* Convert allocated struct to an instance of our Stat Class */
static int fill_stat(JNIEnv *env, struct stat *ptr, jobject *inst)
{
    int num_fields = 13;
    char *field_names[] =
    {
        "st_dev", "st_ino", "st_mode", "st_nlink", "st_uid", "st_gid",
        "st_rdev", "st_size", "st_blksize", "st_blocks", "st_atime", "st_mtime",
        "st_ctime" };
    char *field_types[] =
    {
        "J", "J", "I", "I", "J", "J", "J", "J", "I", "J", "J", "J", "J" };
    jfieldID fids[num_fields];
    char *cls_name = "org/orangefs/usrint/Stat";
    jclass cls = (*env)->FindClass(env, cls_name);
    if (!cls)
    {
        JNI_ERROR("invalid class: %s\n", cls_name);
        return -1;
    }
    int fid_index = 0;
    for (; fid_index < num_fields; fid_index++)
    {
        fids[fid_index] = (*env)->GetFieldID(env, cls, field_names[fid_index],
                field_types[fid_index]);
        if (!fids[fid_index])
        {
            JNI_ERROR("invalid field requested: %s\n", field_names[fid_index]);
            return -1;
        }
    }
    *inst = (*env)->AllocObject(env, cls);
    /* Load object with data from structure using
     * constructor or set methods.
     */
    (*env)->SetLongField(env, *inst, fids[0], ptr->st_dev);
    (*env)->SetLongField(env, *inst, fids[1], ptr->st_ino);
    (*env)->SetIntField(env, *inst, fids[2], ptr->st_mode);
    (*env)->SetIntField(env, *inst, fids[3], ptr->st_nlink);
    (*env)->SetLongField(env, *inst, fids[4], ptr->st_uid);
    (*env)->SetLongField(env, *inst, fids[5], ptr->st_gid);
    (*env)->SetLongField(env, *inst, fids[6], ptr->st_rdev);
    (*env)->SetLongField(env, *inst, fids[7], ptr->st_size);
    (*env)->SetIntField(env, *inst, fids[8], ptr->st_blksize);
    (*env)->SetLongField(env, *inst, fids[9], ptr->st_blocks);
    (*env)->SetLongField(env, *inst, fids[10], ptr->st_atime);
    (*env)->SetLongField(env, *inst, fids[11], ptr->st_mtime);
    (*env)->SetLongField(env, *inst, fids[12], ptr->st_ctime);
    return 0;
}

/* Convert allocated structure to an instance of our Statfs Class */

static int fill_statfs(JNIEnv *env, struct statfs *ptr, jobject *inst)
{
    int num_fields = 10;
    char *field_names[] =
    {
        "f_type", "f_bsize", "f_blocks", "f_bfree", "f_bavail",
        "f_files", "f_ffree", "f_fsid", "f_namelen", "f_frsize"};
    char *field_types[] =
    {
        "J", "J", "J", "J", "J", "J", "J", "I", "J", "J"};
    jfieldID fids[num_fields];
    char *cls_name = "org/orangefs/usrint/Statfs";
    jclass cls = (*env)->FindClass(env, cls_name);
    if (!cls)
    {
        JNI_ERROR("invalid class: %s\n", cls_name);
        return -1;
    }
    int fid_index = 0;
    for (; fid_index < num_fields; fid_index++)
    {
        fids[fid_index] = (*env)->GetFieldID(env, cls, field_names[fid_index],
                field_types[fid_index]);
        if (!fids[fid_index])
        {
            JNI_ERROR("invalid field requested: %s\n", field_names[fid_index]);
            return -1;
        }
    }
    /* Initialize jobject */
    jmethodID mid = (*env)->GetMethodID(env, cls, "<init>", "()V");
    *inst = (*env)->NewObject(env, cls, mid);

    /* Load object with data from structure using
     * constructor or set methods.
     */
    (*env)->SetLongField(env, *inst, fids[0], ptr->f_type);
    (*env)->SetLongField(env, *inst, fids[1], ptr->f_bsize);
    (*env)->SetLongField(env, *inst, fids[2], ptr->f_blocks);
    (*env)->SetLongField(env, *inst, fids[3], ptr->f_bfree);
    (*env)->SetLongField(env, *inst, fids[4], ptr->f_bavail);
    (*env)->SetLongField(env, *inst, fids[5], ptr->f_files);
    (*env)->SetLongField(env, *inst, fids[6], ptr->f_ffree);
    (*env)->SetIntField(env, *inst, fids[7], ptr->f_fsid.__val[0]);
    (*env)->SetLongField(env, *inst, fids[8], ptr->f_namelen);
    (*env)->SetLongField(env, *inst, fids[9], 0); /* TODO: ptr->f_frsize */
    return 0;
}

/* access */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_access(JNIEnv *env, jobject obj,
        jstring path, jlong mode)
{
    JNI_PFI();
    int ret = -1;
    int cpath_len = (*env)->GetStringLength(env, path);
    char cpath[cpath_len + 1];
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    ret = access(cpath, (mode_t) mode);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* chdir */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_chdir(JNIEnv *env, jobject obj,
        jstring path)
{
    JNI_PFI();
    int ret = -1;
    char cpath[PVFS_PATH_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    ret = chdir(cpath);
    if (ret < 0)
    {
        JNI_PERROR();
    }
    return ret;
}

/* chmod */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_chmod(JNIEnv *env, jobject obj,
        jstring path, jlong mode)
{
    JNI_PFI();
    int ret = -1;
    int cpath_len = (*env)->GetStringLength(env, path);
    char cpath[cpath_len + 1];
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    JNI_PRINT("mode = %u\n", (unsigned int) mode);
    ret = chmod(cpath, (mode_t) mode);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* chown */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_chown(JNIEnv *env, jobject obj,
        jstring path, jint owner, jint group)
{
    JNI_PFI();
    int ret = -1;
    char cpath[PVFS_PATH_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    ret = chown(cpath, (uid_t) owner, (gid_t) group);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* close */
JNIEXPORT int JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_close(JNIEnv *env, jobject obj, int fd)
{
    JNI_PFI();
    int ret = close(fd);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* creat */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_creat(JNIEnv *env, jobject obj,
        jstring path, jlong mode)
{
    JNI_PFI();
    jint ret = 0;

    int cpath_len = (*env)->GetStringLength(env, path);
    char cpath[cpath_len + 1];
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);

    ret = (jint) creat(cpath, (mode_t) mode);
    if (ret == -1)
    {
        JNI_PERROR();
    }
    return ret;
}

/* dup */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_dup(JNIEnv *env, jobject obj, int oldfd)
{
    JNI_PFI();
    int ret = 0;
    ret = dup(oldfd);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* dup2 */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_dup2(JNIEnv *env, jobject obj, int oldfd,
        int newfd)
{
    JNI_PFI();
    int ret = 0;
    ret = dup2(oldfd, newfd);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* faccessat */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_faccessat(JNIEnv *env, jobject obj,
        int fd, jstring path, jlong mode, jlong flags)
{
    JNI_PFI();
    int ret = -1;
    int cpath_len = (*env)->GetStringLength(env, path);
    char cpath[cpath_len + 1];
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    ret = faccessat(fd, cpath, (mode_t) mode, (int) flags);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* fallocate */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_fallocate(JNIEnv *env, jobject obj,
        int fd, int mode, jlong offset, jlong length)
{
    JNI_PFI();
    int ret = 0;
    ret = fallocate(fd, mode, (off_t) offset, (off_t) length);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* fchdir */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_fchdir(JNIEnv *env, jobject obj, int fd)
{
    JNI_PFI();
    int ret = fchdir(fd);
    if (ret < 0)
    {
        JNI_PERROR();
    }
    return ret;
}

/* fchmod */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_fchmod(JNIEnv *env, jobject obj, int fd,
        jlong mode)
{
    JNI_PFI();
    int ret = fchmod(fd, (mode_t) mode);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* fchmodat */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_fchmodat(JNIEnv *env, jobject obj,
        int fd, jstring path, jlong mode, jlong flag)
{
    JNI_PFI();
    int ret = -1;
    int cpath_len = (*env)->GetStringLength(env, path);
    char cpath[cpath_len + 1];
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    ret = fchmodat(fd, cpath, (mode_t) mode, (int) flag);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* fchown */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_fchown(JNIEnv *env, jobject obj, int fd,
        jint owner, jint group)
{
    JNI_PFI();
    int ret = fchown(fd, (uid_t) owner, (gid_t) group);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* fchownat */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_fchownat(JNIEnv *env, jobject obj,
        int fd, jstring path, jint owner, jint group, jlong flag)
{
    JNI_PFI();
    int ret = -1;
    char cpath[PVFS_PATH_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    ret = fchownat(fd, cpath, (uid_t) owner, (gid_t) group, (int) flag);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* fdatasync */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_fdatasync(JNIEnv *env, jobject obj,
        int fd)
{
    JNI_PFI();
    jint ret = (jint) fdatasync(fd);
    if (ret == -1)
    {
        JNI_PERROR();
    }
    return ret;
}

/* Fill PVFS2POSIXJNIFlags object */
JNIEXPORT jobject JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_fillPVFS2POSIXJNIFlags(JNIEnv *env,
        jobject obj)
{
    jclass cls = (jobject) 0;
    int num_fields = 52;
    jfieldID fids[num_fields];

    char *field_names[] =
    {
        /* index 0-9 */
        "O_WRONLY", "O_RDONLY", "O_RDWR", "O_APPEND", "O_ASYNC", "O_CLOEXEC",
        "FD_CLOEXEC", "O_CREAT", "O_DIRECT", "O_DIRECTORY",

        /* index 10-19 */
        "O_EXCL", "O_LARGEFILE", "O_NOATIME", "O_NOCTTY", "O_NOFOLLOW",
        "O_NONBLOCK", "O_TRUNC", "S_IRWXU", "S_IRUSR", "S_IWUSR",

        /* index 20-29 */
        "S_IXUSR", "S_IRWXG", "S_IRGRP", "S_IWGRP", "S_IXGRP", "S_IRWXO",
        "S_IROTH", "S_IWOTH", "S_IXOTH", "S_IFMT",

        /* index 30-39 */
        "S_IFSOCK", "S_IFLNK", "S_IFREG", "S_IFBLK", "S_IFDIR", "S_IFCHR",
        "S_IFIFO", "S_ISUID", "S_ISGID", "S_ISVTX",

        /* index 40-49 */
        "SEEK_SET", "SEEK_CUR", "SEEK_END", "AT_FDCWD", "AT_REMOVEDIR",
        "AT_SYMLINK_NOFOLLOW", "ST_RDONLY", "ST_NOSUID", "F_OK", "R_OK",

        /* index 50-51 */
        "W_OK", "X_OK" };

    char *field_types[] =
    {
        /* index 0-9 */
        "J", "J", "J", "J", "J", "J", "J", "J", "J", "J",
        /* index 10-19 */
        "J", "J", "J", "J", "J", "J", "J", "J", "J", "J",
        /* index 20-29 */
        "J", "J", "J", "J", "J", "J", "J", "J", "J", "J",
        /* index 30-39 */
        "J", "J", "J", "J", "J", "J", "J", "J", "J", "J",
        /* index 40-49 */
        "J", "J", "J", "J", "J", "J", "J", "J", "J", "J",
        /* index 50-51 */
        "J", "J" };

    char *cls_name = "org/orangefs/usrint/PVFS2POSIXJNIFlags";
    cls = (*env)->FindClass(env, cls_name);
    if (!cls)
    {
        JNI_ERROR("invalid class: %s\n", cls_name);
        return (jobject) 0;
    }
    /* Allocates Memory for the Object specified by cls w/o calling its
     * constructor.
     */
    jobject inst = (*env)->AllocObject(env, cls);
    /* Get the field ids */
    int fid_index = 0;
    for (; fid_index < num_fields; fid_index++)
    {
        fids[fid_index] = (*env)->GetFieldID(env, cls, field_names[fid_index],
                field_types[fid_index]);
        if (!fids[fid_index])
        {
            JNI_ERROR("invalid field requested: %s\n", field_names[fid_index]);
            return (jobject) 0;
        }

    }

    /* Load PVFS2POSIXJNIFlags object with data replaced by C-Preprocessor
     * using set methods.
     */

    /* access modes for creat/open flags */
    (*env)->SetLongField(env, inst, fids[0], O_WRONLY);
    (*env)->SetLongField(env, inst, fids[1], O_RDONLY);
    (*env)->SetLongField(env, inst, fids[2], O_RDWR);

    /* creat flags */
    (*env)->SetLongField(env, inst, fids[3], O_APPEND);
    (*env)->SetLongField(env, inst, fids[4], O_ASYNC);
    (*env)->SetLongField(env, inst, fids[5], O_CLOEXEC);
    (*env)->SetLongField(env, inst, fids[6], FD_CLOEXEC);
    (*env)->SetLongField(env, inst, fids[7], O_CREAT);
    (*env)->SetLongField(env, inst, fids[8], O_DIRECT);
    (*env)->SetLongField(env, inst, fids[9], O_DIRECTORY);
    (*env)->SetLongField(env, inst, fids[10], O_EXCL);
    (*env)->SetLongField(env, inst, fids[11], O_LARGEFILE);
    (*env)->SetLongField(env, inst, fids[12], O_NOATIME);
    (*env)->SetLongField(env, inst, fids[13], O_NOCTTY);
    (*env)->SetLongField(env, inst, fids[14], O_NOFOLLOW);
    /* man says "O_NONBLOCK or O_NDELAY" but OrangeFS doesn't have O_NDELAY. */
    (*env)->SetLongField(env, inst, fids[15], O_NONBLOCK);
    (*env)->SetLongField(env, inst, fids[16], O_TRUNC);

    /* Permission modes */
    (*env)->SetLongField(env, inst, fids[17], S_IRWXU);
    (*env)->SetLongField(env, inst, fids[18], S_IRUSR);
    (*env)->SetLongField(env, inst, fids[19], S_IWUSR);
    (*env)->SetLongField(env, inst, fids[20], S_IXUSR);
    (*env)->SetLongField(env, inst, fids[21], S_IRWXG);
    (*env)->SetLongField(env, inst, fids[22], S_IRGRP);
    (*env)->SetLongField(env, inst, fids[23], S_IWGRP);
    (*env)->SetLongField(env, inst, fids[24], S_IXGRP);
    (*env)->SetLongField(env, inst, fids[25], S_IRWXO);
    (*env)->SetLongField(env, inst, fids[26], S_IROTH);
    (*env)->SetLongField(env, inst, fids[27], S_IWOTH);
    (*env)->SetLongField(env, inst, fids[28], S_IXOTH);

    /* other st_mode */
    (*env)->SetLongField(env, inst, fids[29], S_IFMT);
    (*env)->SetLongField(env, inst, fids[30], S_IFSOCK);
    (*env)->SetLongField(env, inst, fids[31], S_IFLNK);
    (*env)->SetLongField(env, inst, fids[32], S_IFREG);
    (*env)->SetLongField(env, inst, fids[33], S_IFBLK);
    (*env)->SetLongField(env, inst, fids[34], S_IFDIR);
    (*env)->SetLongField(env, inst, fids[35], S_IFCHR);
    (*env)->SetLongField(env, inst, fids[36], S_IFIFO);
    (*env)->SetLongField(env, inst, fids[37], S_ISUID);
    (*env)->SetLongField(env, inst, fids[38], S_ISGID);
    (*env)->SetLongField(env, inst, fids[39], S_ISVTX);

    /* seek whence */
    (*env)->SetLongField(env, inst, fids[40], SEEK_SET);
    (*env)->SetLongField(env, inst, fids[41], SEEK_CUR);
    (*env)->SetLongField(env, inst, fids[42], SEEK_END);

    /* fcntl.h 'AT' flags*/
    (*env)->SetLongField(env, inst, fids[43], AT_FDCWD);
    (*env)->SetLongField(env, inst, fids[44], AT_REMOVEDIR);
    (*env)->SetLongField(env, inst, fids[45], AT_SYMLINK_NOFOLLOW);

    /* statvfs mount flags */
    (*env)->SetLongField(env, inst, fids[46], ST_RDONLY);
    (*env)->SetLongField(env, inst, fids[47], ST_NOSUID);

    /*Access flags*/
    (*env)->SetLongField(env, inst, fids[48], F_OK);
    (*env)->SetLongField(env, inst, fids[49], R_OK);
    (*env)->SetLongField(env, inst, fids[50], W_OK);
    (*env)->SetLongField(env, inst, fids[51], X_OK);

    return inst;
}

/* flistxattr */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_flistxattr(JNIEnv *env, jobject obj,
        int fd, jstring list, jlong size)
{
    JNI_PFI();
    long ret = 0;
    char clist[PVFS_MAX_XATTR_LISTLEN];
    int clist_len = (*env)->GetStringLength(env, list);
    (*env)->GetStringUTFRegion(env, list, 0, clist_len, clist);
    ret = flistxattr(fd, clist, (size_t) size);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* flock */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_flock(JNIEnv *env, jobject obj, int fd,
        jlong op)
{
    JNI_PFI();
    int ret = flock(fd, (int) op);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* fremovexattr */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_fremovexattr(JNIEnv *env, jobject obj,
        int fd, jstring name)
{
    JNI_PFI();
    int ret = 0;
    char cname[PVFS_NAME_MAX];
    int cname_len = (*env)->GetStringLength(env, name);
    (*env)->GetStringUTFRegion(env, name, 0, cname_len, cname);
    ret = fremovexattr(fd, cname);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* fstat */
JNIEXPORT jobject JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_fstat(JNIEnv *env, jobject obj, int fd)
{
    JNI_PFI();
    int ret = -1;
    struct stat stats;
    ret = fstat(fd, &stats);
    if (ret != 0)
    {
        JNI_PERROR();
        return (jobject) 0;
    }
    jobject stat_obj;
    if (fill_stat(env, &stats, &stat_obj) == 0)
    {
        return stat_obj;
    }
    return (jobject) 0;
}

/* fstatat */
JNIEXPORT jobject JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_fstatat(JNIEnv *env, jobject obj, int fd,
        jstring path, jlong flag)
{
    JNI_PFI();
    int ret = -1;
    struct stat stats;
    int cpath_len = (*env)->GetStringLength(env, path);
    char cpath[cpath_len + 1];
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    ret = fstatat(fd, cpath, &stats, (int) flag);
    if (ret < 0)
    {
        JNI_PERROR();
        return (jobject) 0;
    }
    jobject stat_obj;
    if (fill_stat(env, &stats, &stat_obj) == 0)
    {
        return stat_obj;
    }
    return (jobject) 0;
}

/* fstatfs */

JNIEXPORT jobject JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_fstatfs(JNIEnv *env, jobject obj, jint fd)
{
    JNI_PFI();
    JNI_PRINT("fd = %d\n", (int) fd);
    jobject statfs_obj;
    int ret = -1;
    struct statfs buf;
    ret = fstatfs(fd, &buf);
    if (ret < 0)
    {
        JNI_PERROR();
        return (jobject) 0;
    }
    if (fill_statfs(env, &buf, &statfs_obj) == 0)
    {
        return statfs_obj;
    }
    return (jobject) 0;
}

#if 0
/* fstatvfs */
JNIEXPORT jobject JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_fstatvfs(JNIEnv *env, jobject obj, jint fd)
{
    JNI_PFI();
    JNI_PRINT("fd = %d\n", (int) fd);
    struct statvfs buf;
    int ret = fstatvfs(fd, &buf);
    if (ret == -1)
    {
        JNI_PERROR();
    }
    /* TODO: create class representing statvfs structure. */
    return (jobject) 0;
}
#endif

/* fsync */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_fsync(JNIEnv *env, jobject obj, int fd)
{
    JNI_PFI();
    int ret = fsync(fd);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* ftruncate */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_ftruncate(JNIEnv *env, jobject obj,
        int fd, jlong length)
{
    JNI_PFI();
    int ret = 0;
    ret = ftruncate(fd, (off_t) length);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* futimes */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_futimes(JNIEnv *env, jobject obj,
		int fd, jlong actime_usec, jlong modtime_usec)
{
    JNI_PFI();
    int ret = -1;
    struct timeval tv[2];
    /* If either are invalid then, have utime set actime and modtime to
     * current time. */
    if (actime_usec == -1 || modtime_usec == -1)
    {
        ret = futimes(fd, NULL);
    }
    else
    {
        tv[0].tv_sec = actime_usec / 1000000L;
        tv[0].tv_usec = actime_usec % 1000000L;
        tv[1].tv_sec = modtime_usec / 1000000L;
        tv[1].tv_usec = modtime_usec % 1000000L;
        ret = futimes(fd, tv);
    }
    if (ret < 0)
    {
        JNI_PERROR();
        return -1;
    }
    return 0;
}

/* futimesat */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_futimesat(JNIEnv *env, jobject obj,
        int dirfd, jstring path, jlong actime_usec, jlong modtime_usec)
{
    JNI_PFI();
    int ret = -1;
    struct timeval tv[2];
    char cpath[PVFS_PATH_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);

    /* If either are invalid then, have utime set actime and modtime to
     * current time. */
    if (actime_usec == -1 || modtime_usec == -1)
    {
        ret = futimesat(dirfd, cpath, NULL);
    }
    else
    {
        tv[0].tv_sec = actime_usec / 1000000L;
        tv[0].tv_usec = actime_usec % 1000000L;
        tv[1].tv_sec = modtime_usec / 1000000L;
        tv[1].tv_usec = modtime_usec % 1000000L;
        ret = futimesat(dirfd, cpath, tv);
    }
    if (ret < 0)
    {
        JNI_PERROR();
        return -1;
    }
    return 0;
}

/* getdtablesize */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_getdtablesize(JNIEnv *env, jobject obj)
{
    JNI_PFI();
    int ret = getdtablesize();
    return ret;
}

/* getumask */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_getumask(JNIEnv *env, jobject obj)
{
    JNI_PFI();
    jlong ret = (jlong) getumask();
    return ret;
}

/* Returns non-zero (true) if the supplied mode corresponds to a directory. */
JNIEXPORT int JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_isDir(JNIEnv *env, jobject obj, int mode)
{
    JNI_PFI();
    JNI_PRINT("mode = %d\n", mode);
    return S_ISDIR(mode);
}

/* lchown */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_lchown(JNIEnv *env, jobject obj,
        jstring path, jint owner, jint group)
{
    JNI_PFI();
    int ret = -1;
    char cpath[PVFS_PATH_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    ret = lchown(cpath, (uid_t) owner, (gid_t) group);
    if (ret < 0)
    {
        JNI_PERROR();
    }
    return ret;
}

/* link */
JNIEXPORT int JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_link(JNIEnv *env, jobject obj,
        jstring oldpath, jstring newpath)
{
    JNI_PFI();
    int ret = 0;
    char coldpath[PVFS_PATH_MAX];
    int coldpath_len = (*env)->GetStringLength(env, oldpath);
    (*env)->GetStringUTFRegion(env, oldpath, 0, coldpath_len, coldpath);
    char cnewpath[PVFS_PATH_MAX];
    int cnewpath_len = (*env)->GetStringLength(env, newpath);
    (*env)->GetStringUTFRegion(env, newpath, 0, cnewpath_len, cnewpath);
    ret = link(coldpath, cnewpath);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* linkat */
JNIEXPORT int JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_linkat(JNIEnv *env, jobject obj,
        int olddirfd, jstring oldpath, int newdirfd, jstring newpath,
        jlong flags)
{
    JNI_PFI();
    int ret = -1;
    char coldpath[PVFS_PATH_MAX];
    int coldpath_len = (*env)->GetStringLength(env, oldpath);
    (*env)->GetStringUTFRegion(env, oldpath, 0, coldpath_len, coldpath);
    char cnewpath[PVFS_PATH_MAX];
    int cnewpath_len = (*env)->GetStringLength(env, newpath);
    (*env)->GetStringUTFRegion(env, newpath, 0, cnewpath_len, cnewpath);
    ret = linkat(olddirfd, coldpath, newdirfd, cnewpath, (int) flags);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* listxattr */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_listxattr(JNIEnv *env, jobject obj,
        jstring path, jstring list, jlong size)
{
    JNI_PFI();
    long ret = 0;
    char cpath[PVFS_PATH_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    char clist[PVFS_MAX_XATTR_LISTLEN];
    int clist_len = (*env)->GetStringLength(env, list);
    (*env)->GetStringUTFRegion(env, list, 0, clist_len, clist);
    ret = listxattr(cpath, clist, (size_t) size);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* llistxattr */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_llistxattr(JNIEnv *env, jobject obj,
        jstring path, jstring list, jlong size)
{
    JNI_PFI();
    long ret = 0;
    char cpath[PVFS_PATH_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    char clist[PVFS_MAX_XATTR_LISTLEN];
    int clist_len = (*env)->GetStringLength(env, list);
    (*env)->GetStringUTFRegion(env, list, 0, clist_len, clist);
    ret = llistxattr(cpath, clist, (size_t) size);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* lremovexattr */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_lremovexattr(JNIEnv *env, jobject obj,
        jstring path, jstring name)
{
    JNI_PFI();
    int ret = 0;
    char cpath[PVFS_PATH_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    char cname[PVFS_NAME_MAX];
    int cname_len = (*env)->GetStringLength(env, name);
    (*env)->GetStringUTFRegion(env, name, 0, cname_len, cname);
    ret = lremovexattr(cpath, cname);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* lseek */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_lseek(JNIEnv *env, jobject obj, int fd,
        jlong offset, jlong whence)
{
    JNI_PFI();
    JNI_PRINT("fd = %d, offset = %llu\n", fd, (long long unsigned int) offset);

    off_t ret = lseek(fd, (off_t) offset, (int) whence);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* lstat */
JNIEXPORT jobject JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_lstat(JNIEnv *env, jobject obj,
        jstring path)
{
    JNI_PFI();
    int ret = -1;
    struct stat stats;
    char cpath[PVFS_PATH_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    ret = lstat(cpath, &stats);
    if (ret < 0)
    {
        JNI_PERROR();
        return (jobject) 0;
    }
    jobject stat_obj;
    if (fill_stat(env, &stats, &stat_obj) == 0)
    {
        return stat_obj;
    }
    return (jobject) 0;
}

/* mkdir */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_mkdir(JNIEnv *env, jobject obj,
        jstring path, jlong mode)
{
    JNI_PFI();
    int ret = -1;
    int cpath_len = (*env)->GetStringLength(env, path);
    char cpath[cpath_len + 1];
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    JNI_PRINT("cpath = %s\n", cpath);
    ret = mkdir(cpath, (mode_t) mode);
    JNI_PRINT("mkdir returned %d\n", ret);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* mkdirTolerateExisting */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_mkdirTolerateExisting(JNIEnv *env,
        jobject obj, jstring path, jlong mode)
{
    JNI_PFI();
    int ret = -1;
    int hold_errno = 0;
    int cpath_len = (*env)->GetStringLength(env, path);
    char cpath[cpath_len + 1];
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    JNI_PRINT("cpath = %s\n", cpath);
    ret = mkdir(cpath, (mode_t) mode);
    JNI_PRINT("mkdir returned %d\n", ret);
    if (ret < 0)
    {
        hold_errno = errno;
        /* The sole purpose of this native method is return 0 when the path
         * already exists as a directory. */
        struct stat s;
        /* Verify the path is a directory */
        if(stat(cpath, &s) == 0)
        {
            if( s.st_mode & S_IFDIR )
            {
                /* cpath is an existing directory! */
                return 0;
            }
        }
        errno = hold_errno;
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* mkdirat */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_mkdirat(JNIEnv *env, jobject obj,
        int dirfd, jstring path, jlong mode)
{
    JNI_PFI();
    int ret = 0;
    int cpath_len = (*env)->GetStringLength(env, path);
    char cpath[cpath_len + 1];
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    ret = mkdirat(dirfd, cpath, (mode_t) mode);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* mknod */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_mknod(JNIEnv *env, jobject obj,
        jstring path, jlong mode, int dev)
{
    JNI_PFI();
    int ret = -1;
    int cpath_len = (*env)->GetStringLength(env, path);
    char cpath[cpath_len + 1];
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    ret = mknod(cpath, (mode_t) mode, (dev_t) dev);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* mknodat */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_mknodat(JNIEnv *env, jobject obj,
        int dirfd, jstring path, jlong mode, int dev)
{
    JNI_PFI();
    int ret = -1;
    int cpath_len = (*env)->GetStringLength(env, path);
    char cpath[cpath_len + 1];
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    ret = mknodat(dirfd, cpath, (mode_t) mode, (dev_t) dev);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* open */
JNIEXPORT int JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_open(JNIEnv *env, jobject obj,
        jstring path, jlong flags, jlong mode)
{
    JNI_PFI();
    int ret = 0;
    jclass io_exception_cls = (*env)->FindClass(env, "java/io/IOException");
    int cpath_len = (*env)->GetStringLength(env, path);
    char cpath[cpath_len + 1];
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);

    JNI_PRINT("\tpath = %s\n", cpath);
    if (((int) flags & O_CREAT) == O_CREAT)
    {
        JNI_PRINT("\tO_CREAT detected!\n");
        ret = open(cpath, (int) flags, (mode_t) mode);
    }
    else
    {
        JNI_PRINT("\tNo O_CREAT detected\n");
        ret = open(cpath, (int) flags);
    }
    if (ret < 0)
    {
        JNI_PERROR();
        (*env)->ThrowNew(env, io_exception_cls, "PVFS2POSIXJNI_open failed!");
        ret = -1;
    }
    return ret;
}

/* openWithHints */
JNIEXPORT int JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_openWithHints(JNIEnv *env, jobject obj,
        jstring path, jlong flags, jlong mode, jshort replicationFactor,
        jlong blockSize, jint layout)
{
    JNI_PFI();
    PVFS_hint hint = NULL;
    int layout_int = (int) layout;
    char * distribution_name = "simple_stripe";
    char distribution_pv[1024] = { 0 }; /* distribution param:value string */
    int ret = 0;
    jclass io_exception_cls = (*env)->FindClass(env, "java/io/IOException");
    int cpath_len = (*env)->GetStringLength(env, path);
    char cpath[cpath_len + 1];
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);

    JNI_PRINT("\tpath = %s\n", cpath);
    if (((int) flags & O_CREAT) == O_CREAT)
    {
        JNI_PRINT("\tO_CREAT detected!\n");
        JNI_PRINT("\tlayout = %d\n", layout_int);
        PVFS_hint_add(&hint,
                      PVFS_HINT_LAYOUT_NAME,
                      sizeof(layout_int),
                      &layout_int);
        if(blockSize > 0)
        {
            PVFS_hint_add(&hint,
                          PVFS_HINT_DISTRIBUTION_NAME,
                          strlen(distribution_name) + 1,
                          (void *) distribution_name);
            snprintf(distribution_pv,
                     1024,
                     "strip_size:%llu",
                     (long long unsigned int) blockSize);
            PVFS_hint_add(&hint,
                          PVFS_HINT_DISTRIBUTION_PV_NAME,
                          strlen(distribution_pv) + 1,
                          (void *) distribution_pv);
        }
        /* TODO: add hint for replication when available.
        if(replicationFactor > 1)
        {

        }
        */
        if(hint)
        {
            ret = open(cpath, (int) flags | O_HINTS, (mode_t) mode, hint);
            PVFS_hint_free(hint);
        }
        else
        {
            ret = open(cpath, (int) flags, (mode_t) mode);
        }
    }
    else
    {
        JNI_PRINT("\tNo O_CREAT detected\n");
        ret = open(cpath, (int) flags);
    }
    if (ret < 0)
    {
        JNI_PERROR();
        (*env)->ThrowNew(env, io_exception_cls, "PVFS2POSIXJNI_open failed!");
        ret = -1;
    }
    return ret;
}

/* openat */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_openat(JNIEnv *env, jobject obj,
        int dirfd, jstring path, jlong flags, jlong mode)
{
    JNI_PFI();
    int ret = -1;
    int cpath_len = (*env)->GetStringLength(env, path);
    char cpath[cpath_len + 1];
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);

    if (((int) flags & O_CREAT) == O_CREAT)
    {
        JNI_PRINT("\tO_CREAT detected!\n");
        ret = openat(dirfd, cpath, (int) flags, (mode_t) mode);
    }
    else
    {
        JNI_PRINT("\tNo O_CREAT detected\n");
        ret = openat(dirfd, cpath, (int) flags);
    }
    if (ret == -1)
    {
        JNI_PERROR();
    }
    return (jint) ret;
}

/* pread */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_pread(JNIEnv *env, jobject obj, int fd,
        jbyteArray buf, jlong count, jlong offset)
{
    /* TODO */
    return -1;
}

/* pwrite */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_pwrite(JNIEnv *env, jobject obj, int fd,
        jbyteArray buf, jlong count, jlong offset)
{
    /* TODO */
    return -1;
}

/*
 * Allocated by Java code and freed when the Byte Buffer is garbage collected.
 *  See: src/client/jni/org/orangefs/usrint/OrangeFileSystemInputStream.java
 *  Use it to read bytes from files (even non-OrangeFS files).
 */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_read(JNIEnv *env, jobject obj, int fd,
        jobject buf, jlong count)
{
    JNI_PFI();
    jlong ret = 0;
    void * buf_addr = 0;
    JNI_PRINT("\tfd = %d\n\n\tcount = %lu\n", fd, (uint64_t ) count);
    buf_addr = (*env)->GetDirectBufferAddress(env, buf);
    if (!buf_addr)
    {
        JNI_ERROR("buf_addr returned by " "GetDirectBufferAddress is NULL\n");
        ret = -1;
        return ret;
    }
    ret = (jlong) read(fd, buf_addr, (size_t) count);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* readlink */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_readlink(JNIEnv *env, jobject obj,
        jstring path, jstring buf, jlong bufsiz)
{
    JNI_PFI();
    jlong ret = -1;
    char cpath[PVFS_PATH_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    long cbufsiz = (long) bufsiz;
    char cbuf[cbufsiz];
    int cbuf_len = (*env)->GetStringLength(env, buf);
    (*env)->GetStringUTFRegion(env, path, 0, cbuf_len, cbuf);
    ret = (jlong) readlink(cpath, cbuf, (size_t) bufsiz);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* readlinkat */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_readlinkat(JNIEnv *env, jobject obj,
        int fd, jstring path, jstring buf, jlong bufsiz)
{
    JNI_PFI();
    jlong ret = -1;
    char cpath[PVFS_PATH_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    long cbufsiz = (long) bufsiz;
    char cbuf[cbufsiz];
    int cbuf_len = (*env)->GetStringLength(env, buf);
    (*env)->GetStringUTFRegion(env, path, 0, cbuf_len, cbuf);
    ret = (jlong) readlinkat(fd, cpath, cbuf, (size_t) bufsiz);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* removexattr */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_removexattr(JNIEnv *env, jobject obj,
        jstring path, jstring name)
{
    JNI_PFI();
    int ret = 0;
    char cpath[PVFS_PATH_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    char cname[PVFS_NAME_MAX];
    int cname_len = (*env)->GetStringLength(env, name);
    (*env)->GetStringUTFRegion(env, name, 0, cname_len, cname);
    ret = removexattr(cpath, cname);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* rename */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_rename(JNIEnv *env, jobject obj,
        jstring oldpath, jstring newpath)
{
    JNI_PFI();
    int ret = -1;
    char coldpath[PVFS_PATH_MAX];
    int coldpath_len = (*env)->GetStringLength(env, oldpath);
    (*env)->GetStringUTFRegion(env, oldpath, 0, coldpath_len, coldpath);
    char cnewpath[PVFS_PATH_MAX];
    int cnewpath_len = (*env)->GetStringLength(env, newpath);
    (*env)->GetStringUTFRegion(env, newpath, 0, cnewpath_len, cnewpath);
    ret = rename(coldpath, cnewpath);
    if (ret == -1)
    {
        JNI_PERROR();
    }
    return (jint) ret;
}

/* renameat */
JNIEXPORT int JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_renameat(JNIEnv *env, jobject obj,
        int olddirfd, jstring oldpath, int newdirfd, jstring newpath)
{
    JNI_PFI();
    int ret = 0;
    char coldpath[PVFS_PATH_MAX];
    int coldpath_len = (*env)->GetStringLength(env, oldpath);
    (*env)->GetStringUTFRegion(env, oldpath, 0, coldpath_len, coldpath);
    char cnewpath[PVFS_PATH_MAX];
    int cnewpath_len = (*env)->GetStringLength(env, newpath);
    (*env)->GetStringUTFRegion(env, newpath, 0, cnewpath_len, cnewpath);
    ret = renameat(olddirfd, coldpath, newdirfd, cnewpath);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* rmdir */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_rmdir(JNIEnv *env, jobject obj,
        jstring path)
{
    JNI_PFI();
    int ret = -1;
    char cpath[PVFS_PATH_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    ret = rmdir(cpath);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* stat */
JNIEXPORT jobject JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_stat(JNIEnv *env, jobject obj,
        jstring path)
{
    JNI_PFI();
    int ret = -1;
    struct stat stats;
    char cpath[PVFS_PATH_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    JNI_PRINT("path = %s\n", cpath);
    ret = stat(cpath, &stats);
    if (ret != 0)
    {
#ifdef ENABLE_JNI_PRINT
        JNI_PERROR();
#endif
        return (jobject) 0;
    }
    jobject stat_obj;
    if (fill_stat(env, &stats, &stat_obj) == 0)
    {
        return stat_obj;
    }
    return (jobject) 0;
}

/* statfs */

JNIEXPORT jobject JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_statfs(JNIEnv *env, jobject obj,
        jstring path)
{
    JNI_PFI();
    int ret = -1;
    jobject statfs_obj;
    struct statfs buf;
    char cpath[PVFS_PATH_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    JNI_PRINT("path = %s\n", cpath);
    ret = statfs(cpath, &buf);
    if (ret < 0)
    {
        JNI_PERROR();
        return (jobject) 0;
    }
    if (fill_statfs(env, &buf, &statfs_obj) == 0)
    {
        return statfs_obj;
    }
    return (jobject) 0;
}

#if 0
/* statvfs */
JNIEXPORT jobject JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_statvfs(JNIEnv *env, jobject obj,
        jlong jarg, jstring path)
{
    JNI_PFI();
    int ret = 0;
    struct statvfs buf;
    char cpath[PVFS_PATH_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    ret = statvfs(cpath, &buf);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    /* TODO return jobject */
    return (jobject) 0;
}
#endif

/* symlink */
JNIEXPORT int JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_symlink(JNIEnv *env, jobject obj,
        jstring oldpath, jstring newpath)
{
    JNI_PFI();
    int ret = -1;
    char coldpath[PVFS_PATH_MAX];
    int coldpath_len = (*env)->GetStringLength(env, oldpath);
    (*env)->GetStringUTFRegion(env, oldpath, 0, coldpath_len, coldpath);
    char cnewpath[PVFS_PATH_MAX];
    int cnewpath_len = (*env)->GetStringLength(env, newpath);
    (*env)->GetStringUTFRegion(env, newpath, 0, cnewpath_len, cnewpath);
    ret = symlink(coldpath, cnewpath);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* symlinkat */
JNIEXPORT int JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_symlinkat(JNIEnv *env, jobject obj,
        jstring oldpath, int newdirfd, jstring newpath)
{
    JNI_PFI();
    int ret = -1;
    char coldpath[PVFS_PATH_MAX];
    int coldpath_len = (*env)->GetStringLength(env, oldpath);
    (*env)->GetStringUTFRegion(env, oldpath, 0, coldpath_len, coldpath);
    char cnewpath[PVFS_PATH_MAX];
    int cnewpath_len = (*env)->GetStringLength(env, newpath);
    (*env)->GetStringUTFRegion(env, newpath, 0, cnewpath_len, cnewpath);
    ret = symlinkat(coldpath, newdirfd, cnewpath);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* sync */
JNIEXPORT void JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_sync(JNIEnv *env, jobject obj)
{
    JNI_PFI();
    /* sync is always successful */
    sync();
}

/* truncate */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_truncate(JNIEnv *env, jobject obj,
        jstring path, jlong length)
{
    JNI_PFI();
    int ret = -1;
    char cpath[PVFS_PATH_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    ret = truncate(cpath, (off_t) length);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}

/* umask */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_umask(JNIEnv *env, jobject obj,
        jint mask)
{
    JNI_PFI();
    jint ret = (jint) umask((mode_t) mask);
    JNI_PRINT("ret = %d\n", (int) ret);
    return (int) ret;
}

/* unlink */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_unlink(JNIEnv *env, jobject obj,
        jstring path)
{
    JNI_PFI();
    int ret = -1;
    int cpath_len = (*env)->GetStringLength(env, path);
    char cpath[cpath_len + 1];
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    ret = unlink(cpath);
    if (ret == -1)
    {
        JNI_PERROR();
    }
    return (jint) ret;
}

/* unlinkat */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_unlinkat(JNIEnv *env, jobject obj,
        int dirfd, jstring path, jlong flags)
{
    JNI_PFI();
    int ret = -1;
    int cpath_len = (*env)->GetStringLength(env, path);
    char cpath[cpath_len + 1];
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    ret = unlinkat(dirfd, cpath, (int) flags);
    if (ret == -1)
    {
        JNI_PERROR();
    }
    return (jint) ret;
}

/* utime */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_utime(JNIEnv *env, jobject obj,
        jstring path, jlong actime_sec, jlong modtime_sec)
{
    JNI_PFI();
    int ret = -1;
    struct utimbuf utb;
    char cpath[PVFS_PATH_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    /* If either are invalid then, have utime set actime and modtime to
     * current time. */
    if (actime_sec == -1 || modtime_sec == -1)
    {
        ret = utime(cpath, (const struct utimbuf *) NULL);
    }
    else
    {
        utb.actime = (time_t) actime_sec;
        utb.modtime = (time_t) modtime_sec;
        ret = utime(cpath, &utb);
    }
    if (ret < 0)
    {
        JNI_PERROR();
        return -1;
    }
    return 0;
}

/* utimes */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_utimes(JNIEnv *env, jobject obj,
        jstring path, jlong actime_usec, jlong modtime_usec)
{
    JNI_PFI();
    int ret = -1;
    struct timeval tv[2];
    char cpath[PVFS_PATH_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    /* If either are invalid then, have utime set actime and modtime to
     * current time. */
    if (actime_usec == -1 || modtime_usec == -1)
    {
        ret = utimes(cpath, NULL);
    }
    else
    {
        tv[0].tv_sec = actime_usec / 1000000L;
        tv[0].tv_usec = actime_usec % 1000000L;
        tv[1].tv_sec = modtime_usec / 1000000L;
        tv[1].tv_usec = modtime_usec % 1000000L;
        ret = utimes(cpath, tv);
    }
    if (ret < 0)
    {
        JNI_PERROR();
        return -1;
    }
    return 0;
}

/* write */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2POSIXJNI_write(JNIEnv *env, jobject obj, int fd,
        jobject buf, jlong count)
{
    JNI_PFI();
    jlong ret = 0;
    void * buf_addr = 0;
    JNI_PRINT("\tfd = %d\n\tcount = %lu\n", fd, (uint64_t ) count);
    buf_addr = (*env)->GetDirectBufferAddress(env, buf);
    if (!buf_addr)
    {
        JNI_ERROR("buf_addr returned by GetDirectBufferAddress is NULL\n");
        ret = -1;
        return ret;
    }
    ret = write(fd, buf_addr, (size_t) count);
    JNI_PRINT("write returned: %ld\n", ret);
    if (ret < 0)
    {
        JNI_PERROR();
        ret = -1;
    }
    return ret;
}
