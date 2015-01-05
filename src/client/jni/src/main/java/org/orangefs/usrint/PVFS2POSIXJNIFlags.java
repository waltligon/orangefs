/*
 * (C) 2011 Clemson University
 * 
 * See COPYING in top-level directory.
 */
package org.orangefs.usrint;

import java.lang.reflect.Field;

public class PVFS2POSIXJNIFlags {
    /*
     * Fields set by JNI function fill_PVFS2POSIXJNIFlags.
     */
    /* 0-9 */
    public long O_WRONLY;
    public long O_RDONLY;
    public long O_RDWR;
    public long O_APPEND;
    public long O_ASYNC;
    public long O_CLOEXEC;
    public long FD_CLOEXEC;
    public long O_CREAT;
    public long O_DIRECT;
    public long O_DIRECTORY;
    /* 10-19 */
    public long O_EXCL;
    public long O_LARGEFILE;
    public long O_NOATIME;
    public long O_NOCTTY;
    public long O_NOFOLLOW;
    public long O_NONBLOCK;
    public long O_TRUNC;
    public long S_IRWXU;
    public long S_IRUSR;
    public long S_IWUSR;
    /* 20-29 */
    public long S_IXUSR;
    public long S_IRWXG;
    public long S_IRGRP;
    public long S_IWGRP;
    public long S_IXGRP;
    public long S_IRWXO;
    public long S_IROTH;
    public long S_IWOTH;
    public long S_IXOTH;
    public long S_IFMT;
    /* 30-39 */
    public long S_IFSOCK;
    public long S_IFLNK;
    public long S_IFREG;
    public long S_IFBLK;
    public long S_IFDIR;
    public long S_IFCHR;
    public long S_IFIFO;
    public long S_ISUID;
    public long S_ISGID;
    public long S_ISVTX;
    /* 40-47 */
    public long SEEK_SET;
    public long SEEK_CUR;
    public long SEEK_END;
    public long AT_FDCWD;
    public long AT_REMOVEDIR;
    public long AT_SYMLINK_NOFOLLOW;
    public long ST_RDONLY;
    public long ST_NOSUID;
    public long F_OK;
    public long X_OK;
    /* 50-51 */
    public long R_OK;
    public long W_OK;

    /* Constructor is irrelevant. Call native method fillPVFS2POSIXJNIFlags */
    private PVFS2POSIXJNIFlags() {}

    @Override
    public String toString() {
        StringBuilder result = new StringBuilder();
        String newLine = System.getProperty("line.separator");
        result.append(this.getClass().getName());
        result.append(" Object {");
        result.append(newLine);
        Field[] fields = this.getClass().getDeclaredFields();
        for (Field field : fields) {
            result.append("  ");
            try {
                result.append(field.getName());
                result.append(": ");
                result.append(field.get(this));
            } catch (IllegalAccessException ex) {
                System.out.println(ex);
            }
            result.append(newLine);
        }
        result.append("}");
        return result.toString();
    }
}
