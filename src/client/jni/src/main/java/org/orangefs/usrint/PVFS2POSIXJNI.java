/*
 * (C) 2011 Clemson University
 * 
 * See COPYING in top-level directory.
 */
package org.orangefs.usrint;

import java.io.IOException;
import java.lang.reflect.Field;
import java.nio.ByteBuffer;

public class PVFS2POSIXJNI {
    public PVFS2POSIXJNIFlags f;
    static {
        String ldlPath = System.getenv("JNI_LIBRARY_PATH");
        String libofs = "libofs.so";
        String liborangefs = "liborangefs.so";
        String libpvfs2 = "libpvfs2.so";
        try {
            System.load(ldlPath + "/" + libpvfs2);
        } catch (UnsatisfiedLinkError error) {
            error.printStackTrace();
            System.err.println("Couldn't load " + libpvfs2);
            System.err.println("JNI_LIBRARY_PATH = " + ldlPath);
            System.exit(-1);
        }
        try {
            System.load(ldlPath + "/" + liborangefs);
        } catch (UnsatisfiedLinkError error) {
            error.printStackTrace();
            System.err.println("Couldn't load " + liborangefs);
            System.err.println("JNI_LIBRARY_PATH = " + ldlPath);
            System.exit(-1);
        }
        try {
            System.load(ldlPath + "/" + libofs);
        } catch (UnsatisfiedLinkError error) {
            error.printStackTrace();
            System.err.println("Couldn't load " + libofs);
            System.err.println("JNI_LIBRARY_PATH = " + ldlPath);
            System.exit(-1);
        }
    }

    public PVFS2POSIXJNI() {
        /* Instantiate PVFS2POSIXJNIFlags */
        this.f = fillPVFS2POSIXJNIFlags();
    }

    public native int access(String path, long mode);

    public native int chdir(String path);

    public native int chmod(String path, long mode);

    public native int chown(String path, int owner, int group);

    public native int close(int fd);

    public native int creat(String path, long mode);

    public native int cwdInit(String buf, long size);

    public native int dup(int oldfd);

    public native int dup2(int oldfd, int newfd);

    public native int faccessat(int fd, String path, long mode, long flags);

    public native int fallocate(int fd, int mode, long offset, long length);

    public native int fchdir(int fd);

    public native int fchmod(int fd, long mode);

    public native int fchmodat(int fd, String path, long mode, long flags);

    public native int fchown(int fd, int owner, int group);

    public native int fchownat(int fd, String path, int owner, int group,
            long flags);

    public native int fdatasync(int fd);

    public native PVFS2POSIXJNIFlags fillPVFS2POSIXJNIFlags();

    public native long flistxattr(int fd, String list, long size);

    public native int flock(int fd, long op);

    public native int fremovexattr(int fd, String name);

    public native Stat fstat(int fd);

    public native Stat fstatat(int fd, String path, long flags);

    public native Statfs fstatfs(int fd);

    /* TODO: fstatvfs */
    public native int fsync(int fd);

    public native int ftruncate(int fd, long length);

    public native int futimes(int fd, long actime_usec, long modtime_usec);

    public native int futimesat(int dirfd, String path, long actime_usec,
            long modtime_usec);

    public native int getdtablesize();

    public native long getumask();

    public native int isDir(int mode);

    public native int lchown(String path, int owner, int group);

    public native int link(String oldpath, String newpath);

    public native int linkat(int olddirfd, String oldpath, int newdirfd,
            String newpath, long flags);

    public native long listxattr(String path, String list, long size);

    public native long llistxattr(String path, String list, long size);

    public native int lremovexattr(String path, String name);

    // @Override
    public native long lseek(int fd, long offset, long whence);

    public native Stat lstat(String path);

    public native int mkdir(String path, long mode);
    
    public native int mkdirTolerateExisting(String path, long mode);

    public native int mkdirat(int dirfd, String path, long mode);

    public native int mknod(String path, long mode, int dev);

    public native int mknodat(int dirfd, String path, long mode, int dev);

    public native int open(String path, long flags, long mode)
            throws IOException;

    public native int openWithHints(String path, long flags, long mode,
            short replicationFactor, long blockSize, int layout);

    public native int openat(int dirfd, String path, long flags, long mode);

    public native long pread(int fd, byte[] buf, long count, long offset);

    public native long pwrite(int fd, byte[] buf, long count, long offset);

    public native long read(int fd, ByteBuffer buf, long count);

    public native long readlink(String path, String buf, long bufsiz);

    public native long readlinkat(int fd, String path, String buf, long bufsiz);

    public native int removexattr(String path, String name);

    public native int rename(String oldpath, String newpath);

    public native int renameat(int olddirfd, String oldpath, int newdirfd,
            String newpath);

    public native int rmdir(String path);

    public native Stat stat(String path);

    public native Statfs statfs(String path);

    /* TODO: statvfs */
    public native int symlink(String oldpath, String newpath);

    public native int symlinkat(String oldpath, int newdirfd, String newpath);

    public native void sync();

    /* TODO? */
    // public native long readv(int fd, Iovec [] vector, int count);
    // public native long writev(int fd, Iovec [] vector, int count);
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
            } catch (IllegalAccessException ex) {
                System.out.println(ex);
            }
            result.append(newLine);
        }
        result.append("}");
        return result.toString();
    }

    public native int truncate(String path, long length);

    public native int umask(int mask);

    public native int unlink(String path);

    public native int unlinkat(int dirfd, String path, long flags);

    public native int utime(String path, long actime_sec, long modtime_sec);

    public native int utimes(String path, long actime_usec, long modtime_usec);

    public native long write(int fd, ByteBuffer buf, long count);

    // public native int getcwd(String path, long mode);

}
