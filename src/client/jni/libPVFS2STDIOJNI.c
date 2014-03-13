/*
 * (C) 2011 Clemson University
 *
 * See COPYING in top-level directory.
 */

#include "libPVFS2JNI_common.h"
#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <pvfs2-types.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <usrint.h>
#include <posix-pvfs.h>
#include "org_orangefs_usrint_PVFS2STDIOJNI.h"

/* TODO: relocate these maybe to pvfs2-types.h? */
/* From 'man useradd': "Usernames may only be up to 32 characters long." */
#define MAX_USERNAME_LENGTH 32
/* From 'man groupadd': "Groupnames may only be up to 32 characters long." */
#define MAX_GROUPNAME_LENGTH 32

/* Forward declarations of non-native functions */
static char *combine(char* s1, char* s2, char* dest);
static int fill_dirent(JNIEnv *env, struct dirent *ptr, jobject *inst);
static int get_comb_len(char* s1, char* s2);
static int get_groupname_by_gid(gid_t gid, char *groupname);
static int get_username_by_uid(uid_t uid, char *username);
static inline int is_dot_dir(char * dirent_name);
static inline int is_lostfound_dir(char * dirent_name);
/* TODO: relocate recursive delete related functions so other utilities can
 * make use of them. */
static int recursive_delete(char *dir);
static int remove_files_in_dir(char *dir, DIR* dirp);

/** Combine the two strings with a forward slash between.
 * Don't forget to memset the string!!! The resulting string is stored in dest.
 * Ensure dest is large enough to contain the resulting string.
 */
static char *combine(char* s1, char* s2, char* dest)
{
    strcat(dest, s1);
    strcat(dest, "/");
    return strcat(dest, s2);
}

/* Convert allocated struct DIR to an instance of our Dirent Class */
static int fill_dirent(JNIEnv *env, struct dirent *ptr, jobject *inst)
{
    int num_fields = 5;
    char *field_names[] =
    {
        "d_ino", "d_off", "d_reclen", "d_type", "d_name" };
    char *field_types[] =
    {
        "J", "J", "I", "Ljava/lang/String;", "Ljava/lang/String;" };
    jfieldID fids[num_fields];
    char *cls_name = "org/orangefs/usrint/Dirent";
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
    (*env)->SetLongField(env, *inst, fids[0], ptr->d_ino);
    (*env)->SetLongField(env, *inst, fids[1], ptr->d_off);
    (*env)->SetIntField(env, *inst, fids[2], ptr->d_reclen);
    (*env)->SetCharField(env, *inst, fids[3], (char) ptr->d_type);
    jstring d_name_string = (*env)->NewStringUTF(env, ptr->d_name);
    (*env)->SetObjectField(env, *inst, fids[4], d_name_string);
    return 0;
}

/** Return the length of the resulting string assuming s1 and s2 will be
 * combined with a separating slash and null terminating byte.
 */
static int get_comb_len(char* s1, char* s2)
{
    return (int) (strlen(s1) + strlen(s2) + 2);
}

static int get_groupname_by_gid(gid_t gid, char *groupname)
{
    JNI_PFI();
    struct group* groupp = getgrgid(gid);
    if (groupp == NULL )
    {
        JNI_PERROR();
        return -1;
    }
    strcpy(groupname, groupp->gr_name);
    return 0;
}

static int get_username_by_uid(uid_t uid, char *username)
{
    JNI_PFI();
    struct passwd *pwdp = getpwuid(uid);
    if (pwdp == NULL )
    {
        JNI_PERROR();
        return -1;
    }
    strcpy(username, pwdp->pw_name);
    return 0;
}

/* Returns non-zero (true) if the supplied directory entry name corresponds
 * to the dot directories: {".", ".."}. Otherwise, returns 0;
 */
static inline int is_dot_dir(char * dirent_name)
{
    return (int) ((strcmp(dirent_name, ".") == 0)
            || (strcmp(dirent_name, "..") == 0));
}

/* Returns non-zero(true) if the supplied directory entry name corresponds to
 * the OrangeFS specific directory called "lost+found". */
static inline int is_lostfound_dir(char * dirent_name)
{
    return (int) (strcmp(dirent_name, "lost+found") == 0);
}

/* Recursively delete the path specified by dir. */
static int recursive_delete(char *dir)
{
    JNI_PFI();
    JNI_PRINT("dir=%s\n", dir);
    int ret = -1;
    DIR * dirp = NULL;
    struct dirent * direntp = NULL;

    /* Open the directory specified by dir */
    errno = 0;
    dirp = opendir(dir);
    if (!dirp)
    {
        JNI_PERROR();
        return -1;
    }

    /* Remove all files in the current directory */
    if (remove_files_in_dir(dir, dirp) != 0)
    {
        JNI_ERROR("remove_files_in_dir failed on directory: %s\n", dir);
        return -1;
    }

    /* Rewind directory stream and call recursive delete on the directories
     * in this directory.
     */
    rewinddir(dirp);
    JNI_PRINT("rewinddir succeeded!\n");
    while(1)
    {
        JNI_PRINT("calling readdir on dirp=%p\n", (void *) dirp);
        errno = 0;
        if((direntp = readdir(dirp)) == NULL)
        {
            if(errno != 0)
            {
                JNI_PERROR();
                return -1;
            }
            break;
        }
        if(is_dot_dir(direntp->d_name) || is_lostfound_dir(direntp->d_name))
        {
            continue;
        }
        size_t abs_path_len = get_comb_len(dir, direntp->d_name);
        char abs_path[abs_path_len];
        memset((void *) abs_path, 0, abs_path_len);
        combine(dir, direntp->d_name, abs_path);
        /* Determine if this entry is a file or directory. */
        struct stat buf;
        JNI_PRINT("calling pvfs_stat_mask on file: %s\n", abs_path);
        errno = 0;
        ret = pvfs_stat_mask(abs_path, &buf, PVFS_ATTR_SYS_TYPE);
        if(ret < 0)
        {
            JNI_PERROR();
            return ret;
        }
        if(S_ISDIR(buf.st_mode))
        {
            ret = recursive_delete(abs_path);
            if(ret < 0)
            {
                JNI_ERROR("recursive_delete failed on path:%s\n", abs_path);
                return -1;
            }
        }
    }

    /* Close current directory before we attempt removal */
    errno = 0;
    if (closedir(dirp) != 0)
    {
        JNI_PERROR();
        return -1;
    }
    errno = 0;
    if (rmdir(dir) != 0)
    {
        JNI_PERROR();
        return -1;
    }
    return 0;
}

/* Remove all files discovered in the open directory pointed to by dirp. */
static int remove_files_in_dir(char *dir, DIR* dirp)
{
    int ret = -1;
    struct dirent* direntp = NULL;
    JNI_PFI();
    /* Rewind this directory stream back to the beginning. */
    JNI_PRINT("rewinding dirp = %p\n", dirp);
    rewinddir(dirp);
    JNI_PRINT("rewinddir succeeded!\n");
    /* Loop over directory contents */
    while(1)
    {
        errno = 0;
        if((direntp = readdir(dirp)) == NULL)
        {
            if(errno != 0)
            {
                JNI_PERROR();
                return -1;
            }
            break;
        }
        JNI_PRINT("direntp->d_name = %s\n", direntp->d_name);
        size_t abs_path_len = get_comb_len(dir, direntp->d_name);
        char abs_path[abs_path_len];
        memset((void *) abs_path, 0, abs_path_len);
        combine(dir, direntp->d_name, abs_path);
        /* Determine if this entry is a file or directory. */
        struct stat buf;
        errno = 0;
        ret = pvfs_stat_mask(abs_path, &buf, PVFS_ATTR_SYS_TYPE);
        if(ret < 0)
        {
            JNI_PERROR();
            return ret;
        }
        /* Skip directories. */
        if(S_ISDIR(buf.st_mode))
        {
            continue;
        }
        /* Unlink file. */
        JNI_PRINT("Unlinking file=%s\n", abs_path);
        errno = 0;
        ret = unlink(abs_path);
        if (ret == -1)
        {
            JNI_PERROR();
            return ret;
        }
    }
    return 0;
}

/* clearerr */
JNIEXPORT void JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_clearerr(JNIEnv *env, jobject obj,
        jlong stream)
{
    JNI_PFI();
    JNI_PRINT("stream = %llu\n", (long long unsigned int) stream);
    clearerr((FILE *) stream);
}

/* clearerr_unlocked */
JNIEXPORT void JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_clearerrUnlocked(JNIEnv *env,
        jobject obj, jlong stream)
{
    JNI_PFI();
    JNI_PRINT("stream = %llu\n", (long long unsigned int) stream);
    clearerr_unlocked((FILE *) stream);
}

/* closedir */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_closedir(JNIEnv *env, jobject obj,
        jlong dir)
{
    JNI_PFI();
    jint ret = closedir((DIR *) dir);
    if (ret == -1)
    {
        JNI_PERROR();
    }
    return ret;
}

/* dirfd */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_dirfd(JNIEnv *env, jobject obj,
        jlong dir)
{
    JNI_PFI();
    JNI_PRINT("dir = %llu\n", (long long unsigned int) dir);
    jint ret = (jint) dirfd((DIR *) dir);
    if (ret == -1)
    {
        JNI_ERROR("ret == -1");
    }
    return ret;
}

/* fclose */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fclose(JNIEnv *env, jobject obj,
        jlong fp)
{
    JNI_PFI();
    JNI_PRINT("fp = %llu\n", (long long unsigned int ) fp);
    int ret = EOF;
    ret = fclose((FILE *) fp);
    if (ret == EOF)
    {
        JNI_PERROR();
    }
    return (jint) ret;
}

/* fcloseall */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fcloseall(JNIEnv *env, jobject obj)
{
    JNI_PFI();
    int ret = fcloseall();
    if (ret == EOF)
    {
        JNI_ERROR("fcloseall failed\n");
    }
    return (jint) ret;
}

/* fdopen */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fdopen(JNIEnv *env, jobject obj, jint fd,
        jstring mode)
{
    JNI_PFI();
    jlong ret = 0;
    char cmode[10];
    int cmode_len = (*env)->GetStringLength(env, mode);
    (*env)->GetStringUTFRegion(env, mode, 0, cmode_len, cmode);
    JNI_PRINT("fd = %d\n%s\n", (int) fd, cmode);
    ret = (jlong) fdopen(fd, cmode);
    if (ret == 0)
    {
        JNI_PERROR();
    }
    return ret;
}

/* fdopendir */
JNIEXPORT jobject JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fdopendir(JNIEnv *env, jobject obj,
        jint fd)
{
    JNI_PFI();
    JNI_PRINT("fd = %d\n", (int) fd);
    struct dirent *dir;
    jobject dir_obj = (jobject) 0;
    dir = (struct dirent *) fdopendir((int) fd);
    if (dir == NULL )
    {
        JNI_PERROR();
        return dir_obj;
    }
    if (fill_dirent(env, dir, &dir_obj) != 0)
    {
        JNI_ERROR("fill_dirent failed");
    }
    if(dir)
    {
        free(dir);
    }
    return dir_obj;
}

/* feof */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_feof(JNIEnv *env, jobject obj,
        jlong stream)
{
    JNI_PFI();
    JNI_PRINT("stream = %llu\n", (long long unsigned int) stream);
    int ret = feof((FILE *) stream);
    if (ret != 0)
    {
        JNI_PRINT("EOF reached\n");
    }
    return (jint) ret;
}

/* feof_unlocked  */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_feofUnlocked(JNIEnv *env, jobject obj,
        jlong stream)
{
    JNI_PFI();
    JNI_PRINT("stream = %llu\n", (long long unsigned int) stream);
    int ret = feof_unlocked((FILE *) stream);
    if (ret != 0)
    {
        JNI_PRINT("EOF reached\n");
    }
    return (jint) ret;
}

/* ferror */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_ferror(JNIEnv *env, jobject obj,
        jlong stream)
{
    JNI_PFI();
    JNI_PRINT("stream = %llu\n", (long long unsigned int) stream);
    int ret = ferror((FILE *) stream);
    if (ret != 0)
    {
        JNI_ERROR("ferror detected error.\n");
    }
    return (jint) ret;
}

/* ferror_unlocked */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_ferrorUnlocked(JNIEnv *env, jobject obj,
        jlong stream)
{
    JNI_PFI();
    JNI_PRINT("stream = %llu\n", (long long unsigned int) stream);
    int ret = ferror_unlocked((FILE *) stream);
    if (ret != 0)
    {
        JNI_ERROR("ferror_unlocked detected error.\n");
    }
    return (jint) ret;
}

/* fflush */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fflush(JNIEnv *env, jobject obj,
        jlong stream)
{
    JNI_PFI();
    JNI_PRINT("stream = %llu\n", (long long unsigned int) stream);
    jint ret = (jint) fflush((FILE *) stream);
    if (ret != 0)
    {
        JNI_PERROR();
    }
    return ret;
}

/*fflush_unlocked*/
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fflush_unlocked(JNIEnv *env, jobject obj,
        jlong stream)
{
    JNI_PFI();
    JNI_PRINT("stream = %llu\n", (long long unsigned int) stream);
    jint ret = (jint) fflush_unlocked((FILE *) stream);
    if (ret < 0)
    {
        JNI_PERROR();
    }
    return ret;
}

/* fgetc */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fgetc(JNIEnv *env, jobject obj,
        jlong stream)
{
    JNI_PFI();
    JNI_PRINT("stream = %llu\n", (long long unsigned int) stream);
    int ret = fgetc((FILE *) stream);
    if (ret == EOF)
    {
        JNI_ERROR("ret == EOF");
    }
    return (jint) ret;
}

/* fgetc_unlocked */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fgetcUnlocked(JNIEnv *env, jobject obj,
        jlong stream)
{
    JNI_PFI();
    JNI_PRINT("stream = %llu\n", (long long unsigned int) stream);
    int ret = fgetc_unlocked((FILE *) stream);
    if (ret == EOF)
    {
        JNI_ERROR("ret == EOF");
    }
    return (jint) ret;
}

/* fgets */
JNIEXPORT jstring JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fgets(JNIEnv *env, jobject obj,
        jint size, jlong stream)
{
    JNI_PFI();
    JNI_PRINT("size = %d\nstream = %llu\n", (int) size,
            (long long unsigned int) stream);
    char cs[(int) size];
    char *ret = (char *) NULL;
    ret = fgets(cs, (int) size, (FILE *) stream);
    if (ret == (char *) NULL )
    {
        JNI_ERROR("ret == EOF");
    }
    return (*env)->NewStringUTF(env, ret);
}

/* fgets_unlocked */
JNIEXPORT jstring JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fgetsUnlocked(JNIEnv *env, jobject obj,
        jint size, jlong stream)
{
    JNI_PFI();
    JNI_PRINT("size = %d\nstream = %llu\n", (int) size,
            (long long unsigned int) stream);
    char cs[(int) size];
    char *ret = (char *) NULL;
    ret = fgets_unlocked(cs, (int) size, (FILE *) stream);
    if (ret == (char *) NULL )
    {
        JNI_ERROR("ret == EOF");
    }
    return (*env)->NewStringUTF(env, ret);
}

/* fileno */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fileno(JNIEnv *env, jobject obj,
        jlong stream)
{
    JNI_PFI();
    jint fd = (jint) fileno((FILE *) stream);
    JNI_PRINT("stream = %llu\n", (long long unsigned int ) stream);
    if (fd == -1)
    {
        JNI_PERROR();
    }
    return fd;
}

/* fileno_unlocked */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_filenoUnlocked(JNIEnv *env, jobject obj,
        jlong stream)
{
    JNI_PFI();
    jint fd = (jint) fileno_unlocked((FILE *) stream);
    JNI_PRINT("stream = %llu\n", (long long unsigned int ) stream);
    if (fd == -1)
    {
        JNI_PERROR();
    }
    return fd;
}

/* Fill PVFS2STDIOJNIFlags object */
JNIEXPORT jobject JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fillPVFS2STDIOJNIFlags(JNIEnv *env,
        jobject obj)
{
    int num_fields = 14;
    jfieldID fids[num_fields];
    char *field_names[] =
    {
        /* index 0-9 */
        "SEEK_SET", "SEEK_CUR", "SEEK_END", "DT_BLK", "DT_CHR", "DT_DIR",
        "DT_FIFO", "DT_LNK", "DT_REG", "DT_SOCK",
        /* index 10-13 */
        "DT_UNKNOWN", "_IONBF", "_IOLBF", "_IOFBF" };

    char *field_types[] =
    {
        /* index 0-9 */
        "J", "J", "J", "J", "J", "J", "J", "J", "J", "J",
        /* index 10-13 */
        "J", "J", "J", "J" };

    char *cls_name = "org/orangefs/usrint/PVFS2STDIOJNIFlags";
    jclass cls = (*env)->FindClass(env, cls_name);
    if (!cls)
    {
        JNI_ERROR("invalid class: %s\n", cls_name);
        return (jobject) 0 ;
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
            return cls;
        }

    }

    /* Load PVFS2STDIOJNIFlags object with data replaced by C-Preprocessor
     * using set methods.
     */
    /* seek whence */
    (*env)->SetLongField(env, inst, fids[0], SEEK_SET);
    (*env)->SetLongField(env, inst, fids[1], SEEK_CUR);
    (*env)->SetLongField(env, inst, fids[2], SEEK_END);

    /* readdir d_type */
    (*env)->SetLongField(env, inst, fids[3], DT_BLK);
    (*env)->SetLongField(env, inst, fids[4], DT_CHR);
    (*env)->SetLongField(env, inst, fids[5], DT_DIR);
    (*env)->SetLongField(env, inst, fids[6], DT_FIFO);
    (*env)->SetLongField(env, inst, fids[7], DT_LNK);
    (*env)->SetLongField(env, inst, fids[8], DT_REG);
    (*env)->SetLongField(env, inst, fids[9], DT_SOCK);
    (*env)->SetLongField(env, inst, fids[10], DT_UNKNOWN);

    /* setvbuf modes */
    (*env)->SetLongField(env, inst, fids[11], _IONBF);
    (*env)->SetLongField(env, inst, fids[12], _IOLBF);
    (*env)->SetLongField(env, inst, fids[13], _IOFBF);

    return inst;
}

/* flockfile */
JNIEXPORT void JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_flockfile(JNIEnv *env, jobject obj,
        jlong stream)
{
    JNI_PFI();
    JNI_PRINT("stream = %llu\n", (long long unsigned int ) stream);
    flockfile((FILE *) stream);
    return;
}

/* fopen */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fopen(JNIEnv *env, jobject obj,
        jstring path, jstring mode)
{
    JNI_PFI();
    jlong ret = -1;
    char cpath[256];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    char cmode[10];
    int cmode_len = (*env)->GetStringLength(env, mode);
    (*env)->GetStringUTFRegion(env, mode, 0, cmode_len, cmode);
    JNI_PRINT("%s\n%s\n", cpath, cmode);
    ret = (jlong) fopen(cpath, cmode);
    JNI_PRINT("FILE * ret = %llu\n", (long long unsigned int ) ret);
    if (ret == 0)
    {
        JNI_PERROR();
    }
    return ret;
}

/*fputc*/
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fputc(JNIEnv *env, jobject obj, jint c,
        jlong stream)
{
    JNI_PFI();
    JNI_PRINT("c = %c\nstream = %llu\n", (char) c,
            (long long unsigned int) stream);
    int ret = EOF;
    ret = fputc((int) c, (FILE *) stream);
    if (ret == EOF)
    {
        JNI_ERROR("ret == EOF");
    }
    return (jint) ret;
}

/*fputc_unlocked*/
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fputcUnlocked(JNIEnv *env, jobject obj,
        jint c, jlong stream)
{
    JNI_PFI();
    JNI_PRINT("c = %d\nstream = %llu\n", (char) c,
            (long long unsigned int) stream);
    int ret = EOF;
    ret = fputc_unlocked((int) c, (FILE *) stream);
    if (ret == EOF)
    {
        JNI_ERROR("ret == EOF");
    }
    return (jint) ret;
}

/* fputs */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fputs(JNIEnv *env, jobject obj,
        jstring s, jlong stream)
{
    JNI_PFI();
    int ret = EOF;
    int cs_len = (*env)->GetStringLength(env, s);
    char cs[cs_len + 1];
    (*env)->GetStringUTFRegion(env, s, 0, cs_len, cs);
    JNI_PRINT("s = %s\nstream = %llu\n", cs, (long long unsigned int) stream);
    ret = fputs(cs, (FILE *) stream);
    if (ret == EOF)
    {
        JNI_ERROR("ret == EOF");
    }
    return (jint) ret;
}

/* fputs_unlocked */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fputsUnlocked(JNIEnv *env, jobject obj,
        jstring s, jlong stream)
{
    JNI_PFI();
    int ret = EOF;
    int cs_len = (*env)->GetStringLength(env, s);
    char cs[cs_len + 1];
    (*env)->GetStringUTFRegion(env, s, 0, cs_len, cs);
    JNI_PRINT("s = %s\nstream = %llu\n", cs, (long long unsigned int) stream);
    ret = fputs_unlocked(cs, (FILE *) stream);
    if (ret == EOF)
    {
        JNI_ERROR("ret == EOF");
    }
    return (jint) ret;
}

/* fread */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fread(JNIEnv *env, jobject obj,
        jbyteArray ptr, jlong size, jlong nmemb, jlong stream)
{
    /* TODO: rewrite this using NIO ByteBuffer like PVFS2POSIXJNI_read. */
    JNI_PFI();
    jlong ret = 0;
    jboolean is_copy;
    if (ptr == NULL )
    {
        JNI_ERROR("ptr is null");
        return ret;
    }
    jbyte * cptr = (*env)->GetByteArrayElements(env, ptr, &is_copy);
    if (cptr == NULL )
    {
        JNI_ERROR("cptr is null");
        return ret;
    }
    JNI_PRINT("size = %llu\nnmemb = %llu\nstream = %llu\n",
            (long long unsigned int ) size, (long long unsigned int ) nmemb,
            (long long unsigned int ) stream);
    ret = (jlong) fread((void *) cptr, (size_t) size, (size_t) nmemb,
            (FILE *) stream);
    if (ret < nmemb)
    {
        JNI_PRINT("ret < nmemb, so check feof, then ferror if necessary.");
    }
    JNI_PRINT("\tread %llu items totaling %llu bytes\n",
            (long long unsigned int ) ret,
            (long long unsigned int ) ret * (long long unsigned int ) size);
    /* copy back and free the ptr using 0 */
    (*env)->ReleaseByteArrayElements(env, ptr, cptr, 0);
    return ret;
}

/* fread_unlocked */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_freadUnlocked(JNIEnv *env, jobject obj,
        jbyteArray ptr, jlong size, jlong nmemb, jlong stream)
{
    /* TODO: rewrite this using NIO ByteBuffer like PVFS2POSIXJNI_read. */
    JNI_PFI();
    jlong ret = 0;
    jboolean is_copy;
    if (ptr == 0)
    {
        JNI_ERROR("ptr is null");
        return ret;
    }
    jbyte * cptr = (*env)->GetByteArrayElements(env, ptr, &is_copy);
    if (cptr == 0)
    {
        JNI_ERROR("cptr is null");
        return ret;
    }
    ret = (jlong) fread_unlocked((void *) cptr, (size_t) size, (size_t) nmemb,
            (FILE *) stream);
    if (ret < nmemb)
    {
        JNI_PRINT("ret < nmemb, so check feof, then ferror if necessary.");
    }
    JNI_PRINT("\tread %llu items totaling %llu bytes\n",
            (long long unsigned int ) ret,
            (long long unsigned int ) ret * (long long unsigned int ) size);
    /* copy back and free the ptr using 0 */
    (*env)->ReleaseByteArrayElements(env, ptr, cptr, 0);
    return ret;
}

/* freopen */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_freopen(JNIEnv *env, jobject obj,
        jstring path, jstring modes, jlong stream)
{
    JNI_PFI();
    jlong ret = -1;
    char cpath[PVFS_PATH_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    char cmodes[10];
    int cmodes_len = (*env)->GetStringLength(env, modes);
    (*env)->GetStringUTFRegion(env, modes, 0, cmodes_len, cmodes);
    JNI_PRINT("path = %s\nmodes = %s\nstream = %llu\n", cpath, cmodes,
            (long long unsigned int ) stream);
    ret = (jlong) freopen(cpath, cmodes, (FILE *) stream);
    if (ret == 0)
    {
        JNI_PERROR();
    }
    else
    {
        JNI_PRINT("ret = %llu \n", (long long unsigned int ) ret);
    }
    return ret;
}

/* fseek */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fseek(JNIEnv *env, jobject obj,
        jlong stream, jlong offset, jlong whence)
{
    JNI_PFI();
    jint ret = (jint) fseek((FILE *) stream, (long) offset, (int) whence);
    if (ret != 0)
    {
        JNI_PERROR();
    }
    return ret;
}

/* fseeko */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fseeko(JNIEnv *env, jobject obj,
        jlong stream, jlong offset, jlong whence)
{
    JNI_PFI();
    JNI_PRINT("stream = %llu\noffset = %llu\nwhence = %llu\n",
            (long long unsigned int) stream, (long long unsigned int) offset,
            (long long unsigned int) whence);
    jlong ret = (jlong) fseeko((FILE *) stream, (off_t) offset, (int) whence);
    if (ret != 0)
    {
        JNI_PERROR();
    }
    return ret;
}

/* ftell */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_ftell(JNIEnv *env, jobject obj,
        jlong stream)
{
    JNI_PFI();
    JNI_PRINT("stream = %llu\n", (long long unsigned int ) stream);
    jlong ret = (jlong) ftell((FILE *) stream);
    if (ret < 0)
    {
        JNI_PERROR();
    }
    return ret;
}

/* ftrylockfile */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_ftrylockfile(JNIEnv *env, jobject obj,
        jlong stream)
{
    JNI_PFI();
    JNI_PRINT("stream = %llu\n", (long long unsigned int ) stream);
    jint ret = (jint) ftrylockfile((FILE *) stream);
    if (ret != 0)
    {
        JNI_ERROR("ret != 0, meaning the lock couldn't be obtained.");
    }
    return ret;
}

/* funlockfile */
JNIEXPORT void JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_funlockfile(JNIEnv *env, jobject obj,
        jlong stream)
{
    JNI_PFI();
    JNI_PRINT("stream = %llu\n", (long long unsigned int ) stream);
    funlockfile((FILE *) stream);
    return;
}

/* fwrite */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fwrite(JNIEnv *env, jobject obj,
        jbyteArray ptr, jlong size, jlong nmemb, jlong stream)
{
    /* TODO: rewrite this using NIO ByteBuffer like PVFS2POSIXJNI_write. */
    JNI_PFI();
    jlong ret = 0;
    jboolean is_copy;
    if (ptr == NULL )
    {
        JNI_ERROR("ptr is null");
        return ret;
    }
    jbyte * cptr = (*env)->GetByteArrayElements(env, ptr, &is_copy);
    if (cptr == NULL )
    {
        JNI_ERROR("cptr is null");
        return ret;
    }
    ret = (jlong) fwrite((void *) cptr, (size_t) size, (size_t) nmemb,
            (FILE *) stream);
    if (ret < nmemb)
    {
        JNI_ERROR("ret < nmemb");
    }
    JNI_PRINT("\twrote %llu items totaling %llu bytes\n",
            (long long unsigned int ) ret,
            (long long unsigned int ) ret * (long long unsigned int ) size);
    /* copy back and free the ptr using 0 */
    (*env)->ReleaseByteArrayElements(env, ptr, cptr, 0);
    return ret;
}

/* fwrite_unlocked */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_fwriteUnlocked(JNIEnv *env, jobject obj,
        jbyteArray ptr, jlong size, jlong nmemb, jlong stream)
{
    /* TODO: rewrite this using NIO ByteBuffer like PVFS2POSIXJNI_write. */
    JNI_PFI();
    jlong ret = 0;
    jboolean is_copy;
    if (ptr == 0)
    {
        JNI_ERROR("ptr is NULL");
        return ret;
    }
    jbyte * cptr = (*env)->GetByteArrayElements(env, ptr, &is_copy);
    if (cptr == 0)
    {
        JNI_ERROR("cptr is NULL");
        return ret;
    }
    ret = (jlong) fwrite_unlocked((void *) cptr, (size_t) size, (size_t) nmemb,
            (FILE *) stream);
    if (ret < nmemb)
    {
        JNI_ERROR("ret < nmemb");
    }
    JNI_PRINT("\twrote %llu items totaling %llu bytes\n",
            (long long unsigned int ) ret,
            (long long unsigned int ) ret * (long long unsigned int ) size);
    /* copy back and free the ptr using 0 */
    (*env)->ReleaseByteArrayElements(env, ptr, cptr, 0);
    return ret;
}

/* getc */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_getc(JNIEnv *env, jobject obj,
        jlong stream)
{
    JNI_PFI();
    JNI_PRINT("stream = %llu\n", (long long unsigned int) stream);
    int ret = getc((FILE *) stream);
    if (ret == EOF)
    {
        JNI_ERROR("ret == EOF");
    }
    return (jint) ret;
}

/* getchar */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_getchar(JNIEnv *env, jobject obj)
{
    JNI_PFI();
    int ret = getchar();
    if (ret == EOF)
    {
        JNI_ERROR("ret == EOF");
    }
    return (jint) ret;
}

/* getchar_unlocked */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_getcharUnlocked(JNIEnv *env, jobject obj)
{
    JNI_PFI();
    int ret = getchar_unlocked();
    if (ret == EOF)
    {
        JNI_ERROR("ret == EOF");
    }
    return (jint) ret;
}

/* getc_unlocked */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_getcUnlocked(JNIEnv *env, jobject obj,
        jlong stream)
{
    JNI_PFI();
    JNI_PRINT("stream = %llu\n", (long long unsigned int) stream);
    int ret = getc_unlocked((FILE *) stream);
    if (ret == EOF)
    {
        JNI_ERROR("ret == EOF");
    }
    return (jint) ret;
}

/* Return the directory entries of this directory as an array of Java Strings */
JNIEXPORT jobject JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_getEntriesInDir(JNIEnv *env, jobject obj,
        jstring path)
{
    JNI_PFI();
    jclass arrayListClass = (*env)->FindClass(env, "java/util/ArrayList");
    if(!arrayListClass)
    {
        JNI_ERROR("FindClass failed on class java/util/ArrayList\n");
        return NULL_JOBJECT;
    }
    jmethodID midInit = (*env)->GetMethodID(env, arrayListClass, "<init>", 
            "(I)V");
    if(!midInit)
    {
        JNI_ERROR("GetMethodID failed on method: <init>\n");
        return NULL_JOBJECT;
    }
    jobject objArrayList = (*env)->NewObject(env, arrayListClass, midInit,
            (jint) JNI_INITIAL_ARRAY_LIST_SIZE);
    if(!objArrayList)
    {
        JNI_ERROR("NewObject returned NULL.\n");
        return NULL_JOBJECT;
    }
    jmethodID midAdd = (*env)->GetMethodID(env, arrayListClass, "add", 
            "(Ljava/lang/Object;)Z");
    if(!midAdd)
    {
        JNI_ERROR("GetMethodID failed on method: add.\n");
        return NULL_JOBJECT;
    }
    jmethodID midEnsureCapacity = (*env)->GetMethodID(env, arrayListClass,
            "ensureCapacity", "(I)V");
    if(!midEnsureCapacity)
    {
        JNI_ERROR("GetMethodID failed on method: ensureCapacity.\n");
        return NULL_JOBJECT;
    }

    char cpath[PVFS_NAME_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    /* Check for exception. */
    if((*env)->ExceptionCheck(env) == JNI_TRUE)
    {
        JNI_ERROR("Detected Exception following GetStringUTF.\n");
        (*env)->ExceptionDescribe(env);
        return NULL_JOBJECT;
    }

    DIR * dirp = NULL;
    struct dirent * direntp = NULL;

    /* Open the directory specified by cpath */
    dirp = opendir(cpath);
    if (!dirp)
    {
        JNI_PERROR();
        return NULL_JOBJECT;
    }

    /* Iterate over directory entries and add valid filenames to ArrayList */
    int cnt = 0;
    do
    {
        direntp = readdir(dirp);
        if (direntp)
        {
            if (is_dot_dir(direntp->d_name) ||
                    is_lostfound_dir(direntp->d_name))
            {
                continue;
            }
            /* create java string from dirent name then add to object array */
            jobject fileName = (*env)->NewStringUTF(env, direntp->d_name);
            if(!fileName)
            {
                JNI_ERROR("NewStringUTF returned Null.\n");
                goto error_out;
            }           
            //add entry name to ArrayList
            jboolean addBoolean = (*env)->CallBooleanMethod(env, objArrayList,
                    midAdd, fileName);
            if (addBoolean == JNI_FALSE)
            {
                JNI_ERROR("CallBooleanMethod returned JNI_FALSE.\n");
                goto error_out;
            }
            /* Increment count of valid directory entries */
            cnt++;
            if(cnt % JNI_INITIAL_ARRAY_LIST_SIZE == 0)
            {
                /* Ensure capacity for the next entries. Not necessary,
                 * but may reduce runtime by reducing incremental allocations
                 * that would otherwise be required for directories with many
                 * entries.
                 */
                (*env)->CallVoidMethod(env, objArrayList, midEnsureCapacity, 
                        (jint) (cnt + JNI_INITIAL_ARRAY_LIST_SIZE));
            }
        }
    } while (direntp);
    JNI_PRINT("dir entries = %d\n", cnt);
error_out:
    if(closedir(dirp) != 0)
    {
        JNI_PERROR();
        return NULL_JOBJECT;
    }
    return objArrayList;
}

JNIEXPORT jobjectArray JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_getUsernameGroupname(JNIEnv *env,
        jobject obj, jint uid, jint gid)
{
    JNI_PFI();
    int ret = 0;
    jobjectArray obj_ret = (jobjectArray) (jobject) 0;
    char username[MAX_USERNAME_LENGTH + 1];
    char groupname[MAX_GROUPNAME_LENGTH + 1];

    ret = get_username_by_uid(uid, username);
    if (ret != 0)
    {
        return obj_ret;
    }
    JNI_PRINT("uid, username = <%u, %s>\n", uid, username);

    ret = get_groupname_by_gid(gid, groupname);
    if (ret != 0)
    {
        return obj_ret;
    }
    JNI_PRINT("gid, groupname = <%u, %s>\n", gid, groupname);

    obj_ret = (*env)->NewObjectArray(env, (jsize) 2,
            (*env)->FindClass(env, "java/lang/String"),
            (*env)->NewStringUTF(env, ""));

    (*env)->SetObjectArrayElement(env, obj_ret, 0,
            (*env)->NewStringUTF(env, username));
    (*env)->SetObjectArrayElement(env, obj_ret, 1,
            (*env)->NewStringUTF(env, groupname));
    return obj_ret;
}

/* getw */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_getw(JNIEnv *env, jobject obj,
        jlong stream)
{
    JNI_PFI();
    JNI_PRINT("stream = %llu\n", (long long unsigned int) stream);
    int ret = getw((FILE *) stream);
    if (ret == EOF)
    {
        JNI_ERROR("ret == EOF");
    }
    return (jint) ret;
}

/* mkdtemp */
JNIEXPORT jstring JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_mkdtemp(JNIEnv *env, jobject obj,
        jstring tmplate)
{
    JNI_PFI();
    /* TODO: remove this hard-coded value. */
    char ctmplate[1024];
    int ctmplate_len = (*env)->GetStringLength(env, tmplate);
    (*env)->GetStringUTFRegion(env, tmplate, 0, ctmplate_len, ctmplate);
    JNI_PRINT("tmplate = %s\n", ctmplate);
    char * ret = mkdtemp(ctmplate);
    if (ret == (void *) NULL )
    {
        JNI_PERROR();
    }
    return (*env)->NewStringUTF(env, ret);
}

/* mkstemp */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_mkstemp(JNIEnv *env, jobject obj,
        jstring tmplate)
{
    JNI_PFI();
    /* TODO: remove this hard-coded value. */
    char ctmplate[1024];
    int ctmplate_len = (*env)->GetStringLength(env, tmplate);
    (*env)->GetStringUTFRegion(env, tmplate, 0, ctmplate_len, ctmplate);
    JNI_PRINT("tmplate = %s\n", ctmplate);
    int ret = mkstemp(ctmplate);
    if (ret == -1)
    {
        JNI_PERROR();
    }
    return (jint) ret;
}

/* opendir */
JNIEXPORT jobject JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_opendir(JNIEnv *env, jobject obj,
        jstring name)
{
    JNI_PFI();
    struct dirent *dir;
    jobject dir_obj = (jobject) 0;
    char cname[PVFS_NAME_MAX];
    int cname_len = (*env)->GetStringLength(env, name);
    (*env)->GetStringUTFRegion(env, name, 0, cname_len, cname);
    JNI_PRINT("name = %s\n", cname);
    dir = (struct dirent *) opendir(cname);
    if (dir == NULL )
    {
        JNI_PERROR();
        return dir_obj;
    }
    if (fill_dirent(env, dir, &dir_obj) != 0)
    {
        JNI_ERROR("fill_dirent failed");
    }
    if(dir)
    {
        free(dir);
    }
    return dir_obj;
}

/* putc */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_putc(JNIEnv *env, jobject obj, jint c,
        jlong stream)
{
    JNI_PFI();
    JNI_PRINT("c = %d\nstream = %llu\n", (char) c,
            (long long unsigned int) stream);
    int ret = EOF;
    ret = putc((int) c, (FILE *) stream);
    if (ret == EOF)
    {
        JNI_ERROR("ret == EOF");
    }
    return (jint) ret;
}

/* putchar */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_putchar(JNIEnv *env, jobject obj, jint c)
{
    JNI_PFI();
    JNI_PRINT("c = %d\n", (char) c);
    int ret = EOF;
    ret = putchar((int) c);
    if (ret == EOF)
    {
        JNI_ERROR("ret == EOF");
    }
    return (jint) ret;
}

/*putchar_unlocked*/
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_putcharUnlocked(JNIEnv *env, jobject obj,
        jint c)
{
    JNI_PFI();
    JNI_PRINT("c = %d\n", (char) c);
    int ret = EOF;
    ret = putchar_unlocked((int) c);
    if (ret == EOF)
    {
        JNI_ERROR("ret == EOF");
    }
    return (jint) ret;
}

/* putc_unlocked */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_putcUnlocked(JNIEnv *env, jobject obj,
        jint c, jlong stream)
{
    JNI_PFI();
    JNI_PRINT("c = %d\nstream = %llu\n", (char) c,
            (long long unsigned int) stream);
    int ret = EOF;
    ret = putc_unlocked((int) c, (FILE *) stream);
    if (ret == EOF)
    {
        JNI_ERROR("ret == EOF");
    }
    return (jint) ret;
}

/* puts */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_puts(JNIEnv *env, jobject obj, jstring s)
{
    JNI_PFI();
    int ret = EOF;
    int cs_len = (*env)->GetStringLength(env, s);
    char cs[cs_len + 1];
    (*env)->GetStringUTFRegion(env, s, 0, cs_len, cs);
    JNI_PRINT("s = %s\n", cs);
    ret = puts(cs);
    if (ret == EOF)
    {
        JNI_ERROR("ret == EOF");
    }
    return (jint) ret;
}

/* putw */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_putw(JNIEnv *env, jobject obj, jint wd,
        jlong stream)
{
    JNI_PFI();
    JNI_PRINT("wd = %d\n", (int) wd);
    int ret = putw((int) wd, (FILE *) stream);
    if (ret == EOF)
    {
        JNI_ERROR("ret == EOF");
    }
    return (jint) ret;
}

/* readdir */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_readdir(JNIEnv *env, jobject obj,
        jlong dirp)
{
    JNI_PFI();
    JNI_PRINT("dirp = %llu\n", (long long unsigned int) dirp);
    jlong direntp = 0;
    int errno_before = errno;
    direntp = (jlong) readdir((DIR *) dirp);
    if (direntp == (jlong) NULL )
    {
        if (errno_before != errno)
        {
            JNI_PERROR();
            return 0;
        }
        JNI_PRINT(
                "readdir returned NULL (no error), reached end of directory stream");
    }
    return direntp;
}

/* recursive_delete */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_recursiveDelete(JNIEnv *env, jobject obj,
        jstring path)
{
    JNI_PFI();
    jint ret = -1;
    char cpath[PVFS_NAME_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    ret = recursive_delete(cpath);
    if (ret == -1)
    {
        JNI_ERROR("recursiveDelete error\n");
    }
    return ret;
}

/* remove */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_remove(JNIEnv *env, jobject obj,
        jstring path)
{
    JNI_PFI();
    jint ret = -1;
    char cpath[PVFS_PATH_MAX];
    int cpath_len = (*env)->GetStringLength(env, path);
    (*env)->GetStringUTFRegion(env, path, 0, cpath_len, cpath);
    JNI_PRINT("path = %s\n", cpath);
    ret = (jint) remove(cpath);
    if (ret != 0)
    {
        JNI_PERROR();
    }
    return ret;
}

/* rewinddir */
JNIEXPORT void JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_rewinddir(JNIEnv *env, jobject obj,
        jlong dir)
{
    JNI_PFI();
    JNI_PRINT("dir = %llu\n", (long long unsigned int) dir);
    rewinddir((DIR *) dir);
}

/* seekdir */
JNIEXPORT void JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_seekdir(JNIEnv *env, jobject obj,
        jlong dir, jlong offset)
{
    JNI_PFI();
    JNI_PRINT("dir = %llu\noffset = %llu", (long long unsigned int ) dir,
            (long long unsigned int ) offset);
    seekdir((DIR *) dir, (off_t) offset);
}

#if 0
/* setbuf */
JNIEXPORT void JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_setbuf(JNIEnv *env, jobject obj,
        jlong stream, jstring buf)
{
    /* TODO: use directly allocated NIO ByteBuffer */
    JNI_PFI();
    char cbuf[1024];
    int cbuf_len = (*env)->GetStringLength(env, buf);
    (*env)->GetStringUTFRegion(env, buf, 0, cbuf_len, cbuf);
    setbuf((FILE *) stream, cbuf);
}

/* setbuffer */
JNIEXPORT void JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_setbuffer(JNIEnv *env, jobject obj,
        jlong stream, jstring buf, jlong size)
{
    /* TODO: use directly allocated NIO ByteBuffer */
    JNI_PFI();
    char cbuf[size];
    int cbuf_len = (*env)->GetStringLength(env, buf);
    (*env)->GetStringUTFRegion(env, buf, 0, cbuf_len, cbuf);
    setbuffer((FILE *) stream, cbuf, (size_t) size);
}

/* setlinebuf */
JNIEXPORT void JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_setlinebuf(JNIEnv *env, jobject obj,
        jlong stream)
{
    JNI_PFI();
    JNI_PRINT("stream = %llu\n", (long long unsigned int) stream);
    setlinebuf((FILE *) stream);
}

/* setvbuf*/
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_setvbuf(JNIEnv *env, jobject obj,
        jlong stream, jlong buf, jint mode, jlong size)
{
    /* TODO: use directly allocated NIO ByteBuffer */
    JNI_PFI();
    JNI_PRINT("stream = %llu\nmode = %d\nsize = %llu\n",
            (long long unsigned int) stream, (int) mode,
            (long long unsigned int) size);
    jint ret = (jint) setvbuf((FILE *) stream, (char *) buf, (int) mode,
            (size_t) size);
    if (ret != 0)
    {
        JNI_PERROR();
    }
    return ret;
}
#endif

/* telldir */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_telldir(JNIEnv *env, jobject obj,
        jlong dir)
{
    JNI_PFI();
    JNI_PRINT("dir = %llu", (long long unsigned int ) dir);
    jlong ret = (jlong) telldir((DIR *) dir);
    if (ret == -1)
    {
        JNI_PERROR();
    }
    return ret;
}

/* tmpfile */
JNIEXPORT jlong JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_tmpfile(JNIEnv *env, jobject obj)
{
    JNI_PFI();
    FILE * fd = tmpfile();
    if (fd == (FILE *) NULL )
    {
        JNI_PERROR();
    }
    return (jlong) fd;
}

/* ungetc */
JNIEXPORT jint JNICALL
Java_org_orangefs_usrint_PVFS2STDIOJNI_ungetc(JNIEnv *env, jobject obj, jint c,
        jlong stream)
{
    JNI_PFI();
    JNI_PRINT("size = %d\nstream = %llu\n", (int) c,
            (long long unsigned int) stream);
    int ret = ungetc((int) c, (FILE *) stream);
    if (ret == EOF)
    {
        JNI_ERROR("ret == EOF");
    }
    return (jint) ret;
}
