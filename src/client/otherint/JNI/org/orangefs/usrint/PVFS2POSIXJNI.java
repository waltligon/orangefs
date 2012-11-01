/* 
 * (C) 2011 Clemson University
 *
 * See COPYING in top-level directory.
 */
 
package org.orangefs.usrint;

import org.orangefs.usrint.*;
import java.lang.reflect.Field;

public class PVFS2POSIXJNI {

    private PVFS2POSIXJNIFlags f;
    
    public PVFS2POSIXJNIFlags getF() {
        return f;
    };

    /* ========== PVFS2POSIXJNI Native Methods START ========== */
    public native PVFS2POSIXJNIFlags fillPVFS2POSIXJNIFlags();

    public native int isDir(int mode);

    public native int open(String path, long flags, long mode);
    public native int open64(String path, long flags, long mode); /* 06/15/2012 */
    public native int openat(int dirfd, String path, long flags, long mode);   /* 06/15/2012 */
    public native int openat64(int dirfd, String path, long flags, long mode);   /* 06/15/2012 */
    public native int creat(String path, long mode);   
    public native int creat64(String path, long mode);   
    public native int unlink(String path);
    public native int unlinkat(int dirfd, String path, long flags);  /* 06/15/2012 */
    public native int rename(String oldpath, String newpath); 
    public native int renameAt(int olddirfd, String oldpath, int newdirfd, String newpath);
    public native int close(int fd); 
    public native int flush(int fd);  /* 06/15/2012 */
    
    public native long read(int fd, long buf, long count); /* 06/18/2012 (INCOMPLETE)*/
    public native long lseek(int fd, long offset, long whence); /* 06/19/2012 */
    public native long lseek64(int fd, long offset, long whence); /* 06/19/2012 */
    public native int truncate(String path, long length); /* 06/19/2012 */
    public native int truncate64(String path, long length); /* 06/19/2012 */
    public native int fallocate(int fd, long offset, long length); /* 06/19/2012 */
    public native int ftruncate(int fd, long length); /* 06/19/2012 */
    public native int ftruncate64(int fd, long length); /* 06/19/2012 */
    public native int dup(int oldfd); /* 06/19/2012 */
    public native int dup2(int oldfd, int newfd); /* 06/19/2012 */
    
    public native int chown(String path, long owner, long group); /* 06/20/2012 */
    public native int fchown(int fd, long owner, long group); /* 06/20/2012 */
    public native int fchownat(int fd, String path, long owner, long group, long flags); /* 06/20/2012 */
    public native int lchown(String path, long owner, long group); /* 06/20/2012 */
    public native int chmod(String path, long mode); /* 06/20/2012 */
    public native int fchmod(int fd, long mode); /* 06/20/2012 */
    public native int fchmodat(int fd, String path, long mode, long flags); /* 06/20/2012 */
    
    public native int mkdir(String path, long mode);/* 06/19/2012 (TESTED)*/
    public native int mkdirat(int dirfd, String path, long mode);  /* 06/19/2012 */
    public native int rmdir(String path); /* 06/19/2012  (TESTED) */
    public native long readlink(String path, String buf, long bufsiz);/* 06/19/2012 */
    public native long readlinkat(int fd, String path, String buf, long bufsiz);/* 06/19/2012 */
    public native int symlink(String oldpath, String newpath);/* 06/19/2012  (TESTED) */
    public native int symlinkat(String oldpath, int newdirfd, String newpath);/* 06/19/2012 */
    public native int link(String oldpath, String newpath);/* 06/19/2012 */
    public native int linkat(int olddirfd, String oldpath, int newdirfd, String newpath, long flags);/* 06/19/2012 */
    public native int access(String path, long mode); /* 06/20/2012 */
    public native int faccessat(int fd, String path, long mode, long flags); /* 06/20/2012 */
    public native int flock(int fd, long op); /* 06/20/2012 */
    /* TODO: fix the Fcntl */
    public native int fcntl(int fd, long cmd); /* 06/20/2012 */
    public native int fsync(int fd); /* 06/20/2012 */
    public native int fdatasync(int fd); /* 06/20/2012 */
    /**/
    public native int fadvise(int fd, long offset, long len, long advice); /* 06/20/2012 */
    public native int fadvise64(int fd, long offset, long len, long advice); /* 06/20/2012 */
    public native int mknod(String path, long mode, int dev); /* 06/20/2012 */
    public native int mknodat(int dirfd, String path, long mode, int dev); /* 06/20/2012 */
    public native int chdir(String path); /* 06/20/2012 */
    public native int fchdir(int fd); /* 06/20/2012 */
    public native int umask(long mask); /* 06/20/2012 */

    public native long read(int fd, byte [] buf, long count);
    public native long pread(int fd, byte [] buf, long count, long offset);
    public native long pread64(int fd, byte [] buf, long count, long offset);
    public native long write(int fd, byte [] buf, long count);
    public native long pwrite(int fd, byte [] buf, long count, long offset);
    public native long pwrite64(int fd, byte [] buf, long count, long offset);

    /* TODO? */
    //public native long readv(int fd, Iovec [] vector, int count);
    //public native long writev(int fd, Iovec [] vector, int count);
    
    //fuctions using the structure stat
    public native Stat stat(String path);
    public native Stat fstat(int fd);
    public native Stat fstatat(int fd, String path, long flags);
    public native Stat lstat(String path);
    
    //fuctions using the structure stat64
    public native Stat64 stat64(String path);
    public native Stat64 fstat64(int fd); 
    public native Stat64 fstatat64(int fd, String path, long flags);
    public native Stat64 lstat64(String path);

    /* TODO, use classes representing C structs to make these native methods 
     * work.
     */
    //fuctions using the structure statfs
    public native Statfs statfs(String path);
    public native Statfs fstatfs(int fd);
    
    //fuctions using the structure statfs64
    //public native Statfs64 statfs64(long x, String path);
    //public native Fstatfs64 fstatfs64(long x, int fd);
    
    //fuctions using the structure statvfs
    //public native Statvfs statvfs(long x, String path);
    //public native Statvfs fstatvfs(long x, int fd);
    
    //fuctions using the structure dirent
    //public native Dirent readdir(long x, int fd, int count);
    //public native int getdents (long x, int fd, int size);
    //fuctions using the structure dirent64
    //public native int getdents64 (long x, int fd, int size);
    /* Fix the native methods listed above. */
    
    //fuctions using the structure timeval
    public native Timeval futimesat(int dirfd, String path);
    public native Timeval utimes(String path);
    public native Timeval futimes(int fd);
    
    //fuctions using the structure utimbuf
    public native Utimbuf utime(String path);
    
    public native long listxattr(String path, String list, long size);
    public native long llistxattr(String path, String list, long size);
    public native long flistxattr(int fd, String list, long size);
    
    public native int removexattr(String path, String name);   
    public native int lremovexattr(String path, String name); 
    public native int fremovexattr(int fd, String name);    
    public native int cwdInit(String buf, long size);
    
    public native long getumask();
    public native int getdtablesize();
    public native void sync();

    /* ========== PVFS2POSIXJNI Native Methods END ========== */
    
    public PVFS2POSIXJNI()
    {
        /* Instantiate PVFS2POSIXJNIFlags */
        this.f = this.fillPVFS2POSIXJNIFlags();
    }
    
    /* Generic Object Dump to String */
    public String toString() {
        StringBuilder result = new StringBuilder();
        String newLine = System.getProperty("line.separator");

        result.append(this.getClass().getName());
        result.append(" Object {");
        result.append(newLine);

        Field[] fields = this.getClass().getDeclaredFields();

        for(Field field : fields ) {
            result.append("  ");
            try {
                result.append(field.getName());
                result.append(": ");
                result.append(field.get(this));
            } catch(IllegalAccessException ex) {
                System.out.println(ex);
            }
            result.append(newLine);
        }
        result.append("}");
        return result.toString();
    }
    
    static {
        try {
            System.loadLibrary("PVFS2POSIXJNI");
        } catch (UnsatisfiedLinkError error) {
            error.printStackTrace();
            System.err.println("Couldn't load libPVFS2POSIXJNI.so.");
            System.err.println("java.library.path = " + System.getProperty("java.library.path"));
            System.exit(-1);
        }
    }
}

