/* 
 * (C) 2011 Clemson University
 *
 * See COPYING in top-level directory.
 */
package org.orangefs.usrint;

import java.lang.reflect.Field;
import java.nio.ByteBuffer;

public class PVFS2POSIXJNI {
    public PVFS2POSIXJNIFlags f;
    static {
        String ldlpath = System.getenv("JNI_LIBRARY_PATH");
        try {
            System.load(ldlpath + "/libpvfs2.so");
        }
        catch (UnsatisfiedLinkError error) {
            error.printStackTrace();
            System.err.println("Couldn't load libpvfs2.so.");
            System.err.println("JNI_LIBRARY_PATH = "
                    + System.getenv("JNI_LIBRARY_PATH"));
            System.exit(-1);
        }
        try {
            System.load(ldlpath + "/libofs.so");
        }
        catch (UnsatisfiedLinkError error) {
            error.printStackTrace();
            System.err.println("Couldn't load libofs.so.");
            System.err.println("JNI_LIBRARY_PATH = "
                    + System.getenv("JNI_LIBRARY_PATH"));
            System.exit(-1);
        }
        try {
            System.load(ldlpath + "/libPVFS2POSIXJNI.so");
        }
        catch (UnsatisfiedLinkError error) {
            error.printStackTrace();
            System.err.println("Couldn't load libPVFS2POSIXJNI.so.");
            System.err.println("JNI_LIBRARY_PATH = "
                    + System.getenv("JNI_LIBRARY_PATH"));
            System.exit(-1);
        }
    }

    public PVFS2POSIXJNI() {
        /* Instantiate PVFS2POSIXJNIFlags */
        this.f = this.fillPVFS2POSIXJNIFlags();
    }

    public native int access(String path, long mode);

    public native int chdir(String path);

    public native int chmod(String path, long mode);

    public native int chown(String path, long owner, long group);

    public native int close(int fd);

    public native int creat(String path, long mode);

    public native int creat64(String path, long mode);

    public native int cwdInit(String buf, long size);

    public native int dup(int oldfd);

    public native int dup2(int oldfd, int newfd);

    public native int faccessat(int fd, String path, long mode, long flags);

    public native int fadvise(int fd, long offset, long len, long advice);

    public native int fadvise64(int fd, long offset, long len, long advice);

    public native int fallocate(int fd, long offset, long length);

    public native int fchdir(int fd);

    public native int fchmod(int fd, long mode);

    public native int fchmodat(int fd, String path, long mode, long flags);

    public native int fchown(int fd, long owner, long group);

    public native int fchownat(int fd, String path, long owner, long group,
            long flags);

    public native int fcntl(int fd, long cmd);

    public native int fdatasync(int fd);

    public native PVFS2POSIXJNIFlags fillPVFS2POSIXJNIFlags();

    public native long flistxattr(int fd, String list, long size);

    public native int flock(int fd, long op);

    public native int flush(int fd);

    public native int fremovexattr(int fd, String name);

    public native Stat fstat(int fd);

    public native Stat64 fstat64(int fd);

    public native Stat fstatat(int fd, String path, long flags);

    public native Stat64 fstatat64(int fd, String path, long flags);

    public native Statfs fstatfs(int fd);

    public native int fsync(int fd);

    public native int ftruncate(int fd, long length);

    public native int ftruncate64(int fd, long length);

    public native Timeval futimes(int fd);

    public native Timeval futimesat(int dirfd, String path);

    public native int getdtablesize();

    public native long getumask();

    public native int isDir(int mode);

    public native int lchown(String path, long owner, long group);

    public native int link(String oldpath, String newpath);

    public native int linkat(int olddirfd, String oldpath, int newdirfd,
            String newpath, long flags);

    public native long listxattr(String path, String list, long size);

    public native long llistxattr(String path, String list, long size);

    public native int lremovexattr(String path, String name);

    public native long lseek(int fd, long offset, long whence);

    public native long lseek64(int fd, long offset, long whence);

    public native Stat lstat(String path);

    public native Stat64 lstat64(String path);

    public native int mkdir(String path, long mode);

    public native int mkdirat(int dirfd, String path, long mode);

    public native int mknod(String path, long mode, int dev);

    public native int mknodat(int dirfd, String path, long mode, int dev);

    public native int open(String path, long flags, long mode);

    public native int open64(String path, long flags, long mode);

    public native int openat(int dirfd, String path, long flags, long mode);

    /* TODO? */
    // public native long readv(int fd, Iovec [] vector, int count);
    // public native long writev(int fd, Iovec [] vector, int count);
    public native int openat64(int dirfd, String path, long flags, long mode);

    public native long pread(int fd, byte[] buf, long count, long offset);

    public native long pread64(int fd, byte[] buf, long count, long offset);

    public native long pwrite(int fd, byte[] buf, long count, long offset);

    public native long pwrite64(int fd, byte[] buf, long count, long offset);

    public native long read(int fd, ByteBuffer buf, long count);

    public native long readlink(String path, String buf, long bufsiz);

    public native long readlinkat(int fd, String path, String buf, long bufsiz);

    public native int removexattr(String path, String name);

    public native int rename(String oldpath, String newpath);

    // // fuctions using the structure statfs64
    // public native Statfs64 statfs64(long x, String path);
    // public native Fstatfs64 fstatfs64(long x, int fd);
    // // fuctions using the structure statvfs
    // public native Statvfs statvfs(long x, String path);
    // public native Statvfs fstatvfs(long x, int fd);
    // // fuctions using the structure dirent
    // public native Dirent readdir(long x, int fd, int count);
    // public native int getdents (long x, int fd, int size);
    // // fuctions using the structure dirent64
    // public native int getdents64 (long x, int fd, int size);
    public native int renameAt(int olddirfd, String oldpath, int newdirfd,
            String newpath);

    public native int rmdir(String path);

    public native Stat stat(String path);

    public native Stat64 stat64(String path);

    public native Statfs statfs(String path);

    public native int symlink(String oldpath, String newpath);

    public native int symlinkat(String oldpath, int newdirfd, String newpath);

    public native void sync();

    /* Generic Object Dump to String */
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
            }
            catch (IllegalAccessException ex) {
                System.out.println(ex);
            }
            result.append(newLine);
        }
        result.append("}");
        return result.toString();
    }

    public native int truncate(String path, long length);

    public native int truncate64(String path, long length);

    public native int umask(long mask);

    public native int unlink(String path);

    public native int unlinkat(int dirfd, String path, long flags);

    public native Utimbuf utime(String path);

    public native Timeval utimes(String path);

    public native long write(int fd, ByteBuffer buf, long count);
}
