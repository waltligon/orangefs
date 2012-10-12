package org.orangefs.usrint;

import java.lang.reflect.Field;

public class PVFS2POSIXJNI {

    /* ========== PVFS2POSIXJNI Native Methods START ========== */
    public native int isDir(int mode);

    public native int open(String path, String flags, String mode);
    public native int open64(String path, String flags); /* 06/15/2012 */
    public native int openat(int dirfd, String path, String flags);   /* 06/15/2012 */
    public native int openat64(int dirfd, String path, String flags);   /* 06/15/2012 */
    public native int creat(String path, String mode);   
    public native int creat64(String path, String mode);   
    public native int unlink(String path);
    public native int unlinkat(int dirfd, String path, String flags);  /* 06/15/2012 */
    public native int rename(String oldpath, String newpath); 
    public native int renameAt(int olddirfd, String oldpath, int newdirfd, String newpath);
    public native int close(int fd); 
    public native int flush(int fd);  /* 06/15/2012 */
    
    public native long read(int fd, long buf, long count); /* 06/18/2012 (INCOMPLETE)*/
    public native long lseek(int fd, long offset, String whence); /* 06/19/2012 */
    public native long lseek64(int fd, long offset, String whence); /* 06/19/2012 */
    public native int truncate(String path, long length); /* 06/19/2012 */
    public native int truncate64(String path, long length); /* 06/19/2012 */
    public native int fallocate(int fd, long offset, long length); /* 06/19/2012 */
    public native int ftruncate(int fd, long length); /* 06/19/2012 */
    public native int ftruncate64(int fd, long length); /* 06/19/2012 */
    public native int dup(int oldfd); /* 06/19/2012 */
    public native int dup2(int oldfd, int newfd); /* 06/19/2012 */
    
    public native int chown(String path, long owner, long group); /* 06/20/2012 */
    public native int fchown(int fd, long owner, long group); /* 06/20/2012 */
    public native int fchownat(int fd, String path, long owner, long group, String flag); /* 06/20/2012 */
    public native int lchown(String path, long owner, long group); /* 06/20/2012 */
    public native int chmod(String path, String mode); /* 06/20/2012 */
    public native int fchmod(int fd, String mode); /* 06/20/2012 */
    public native int fchmodat(int fd, String path, String mode, String flag); /* 06/20/2012 */
    
    public native int mkdir(String path, String mode);/* 06/19/2012 (TESTED)*/
    public native int mkdirat(int dirfd, String path, String mode);  /* 06/19/2012 */
    public native int rmdir(String path); /* 06/19/2012  (TESTED) */
    public native long readlink(String path, String buf, long bufsiz);/* 06/19/2012 */
    public native long readlinkat(int fd, String path, String buf, long bufsiz);/* 06/19/2012 */
    public native int symlink(String oldpath, String newpath);/* 06/19/2012  (TESTED) */
    public native int symlinkat(String oldpath, int newdirfd, String newpath);/* 06/19/2012 */
    public native int link(String oldpath, String newpath);/* 06/19/2012 */
    public native int linkat(int olddirfd, String oldpath, int newdirfd, String newpath, String flags);/* 06/19/2012 */
    public native int access(String path, String mode); /* 06/20/2012 */
    public native int faccessat(int fd, String path, String mode, String flags); /* 06/20/2012 */
    public native int flock(int fd, String op); /* 06/20/2012 */
    /* TODO: fix the Fcntl */
    public native int fcntl(int fd, String cmd); /* 06/20/2012 */
    public native int fsync(int fd); /* 06/20/2012 */
    public native int fdatasync(int fd); /* 06/20/2012 */
    /**/
    public native int fadvise(int fd, long offset, long len, String advice); /* 06/20/2012 */
    public native int fadvise64(int fd, long offset, long len, String advice); /* 06/20/2012 */
    public native int mknod(String path, String mode, int dev); /* 06/20/2012 */
    public native int mknodat(int dirfd, String path, String mode, int dev); /* 06/20/2012 */
    public native int chdir(String path); /* 06/20/2012 */
    public native int fchdir(int fd); /* 06/20/2012 */
    public native int umask(String mask); /* 06/20/2012 */

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
    public native PVFS2POSIXJNI.Stat stat(String path);/* 06/26/2012 (STRUCTURE TESTED) */
    public native PVFS2POSIXJNI.Stat fstat(int fd); /* 06/27/2012 (STRUCTURE TESTED) */
    public native PVFS2POSIXJNI.Stat fstatat(int fd, String path, String flag); /* 06/27/2012 (STRUCTURE TESTED) */
    public native PVFS2POSIXJNI.Stat lstat(String path); /* 06/27/2012 */
    
    //fuctions using the structure stat64
    public native PVFS2POSIXJNI.Stat64 stat64(String path);
    public native PVFS2POSIXJNI.Stat64 fstat64(int fd); 
    public native PVFS2POSIXJNI.Stat64 fstatat64(int fd, String path, String flag);
    public native PVFS2POSIXJNI.Stat64 lstat64(String path);
    
    //fuctions using the structure statfs
    public native PVFS2POSIXJNI.Statfs statfs(String path);
    public native PVFS2POSIXJNI.Statfs fstatfs(int fd);
    
    //fuctions using the structure statfs64
    public native int statfs64(long x, String path);
    public native int fstatfs64(long x, int fd);
    
    //fuctions using the structure statvfs
    public native int statvfs(long x, String path);
    public native int fstatvfs(long x, int fd);
    
    //fuctions using the structure dirent
    public native int readdir(long x, int fd, int count);
    public native int getdents (long x, int fd, int size);
    
    //fuctions using the structure dirent64
    public native int getdents64 (long x, int fd, int size);
    
    //fuctions using the structure timeval
    public native PVFS2POSIXJNI.Timeval futimesat(int dirfd, String path);
    public native PVFS2POSIXJNI.Timeval utimes(String path);
    public native PVFS2POSIXJNI.Timeval futimes(int fd);
    
    //fuctions using the structure utimbuf
    public native PVFS2POSIXJNI.Utimbuf utime(String path);
    
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

    /* Custom Classes representing C structs */
    public class Stat
    {
        public long st_dev;
        public long st_ino;
        public int st_mode;
        public int st_nlink;
        public long st_uid;
        public long st_gid;
        public long st_rdev;
        public long st_size;
        public int st_blksize;
        public long st_blocks;
        public long st_atime;
        public long st_mtime;
        public long st_ctime;

        /* Constructor */
        Stat(){}

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
    }
    
    public class Stat64{

        long st_dev;
        long st_ino;
        int st_mode;
        int st_nlink;
        long st_uid;
        long st_gid;
        long st_rdev;
        long st_size;
        int st_blksize;
        long st_blocks;
        long st_atime;
        long st_mtime;
        long st_ctime;
    }

/*    
    public class Dirent{
        long d_ino;
        long d_off;
        int d_reclen;
        String d_type;
        String d_name;
        
    }
        
    public class Dirent64{
        long d_ino;
        long d_off;
        int d_reclen;
        String d_type;
        String d_name;
        
    }
*/
  
    public class Statfs{
        long f_type;
        long f_bsize;
        long f_blocks;
        long f_bfree;
        long f_bavail;
        long f_files;
        long f_ffree;
        long f_flags;
        long f_namelen;
        long f_frsize;
        long [] f_spare = new long[5];
    }
    
    public class Statvfs{
        
        long f_bsize;
        long f_frsize;
        long f_blocks;
        long f_bfree;
        long f_bavail;
        long f_files;
        long f_ffree;
        long f_favail;
        long f_fsid;
        long f_flag;
        long f_namemax;
    }
    public class Statfs64{
        long f_type;
        long f_bsize;
        long f_blocks;
        long f_bfree;
        long f_bavail;
        long f_files;
        long f_ffree;
        long f_flags;
        long f_namelen;
        long f_frsize;
        long [] f_spare = new long[5];
    }
        
    public class Fsid{
        int[] val = new int[2]; 
    }

    public class Utimbuf{
        long actime;
        long modtime;
    }
    public class Timeval{
        long tv_sec;
        long tv_usec;
    }
}

