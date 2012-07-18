
public class PVFS2JNI {

    /* from unistd.h whence "*seek*" */
    public int SEEK_SET = 0;    /* Seek from beginning of file. */
    public int SEEK_CUR = 1;    /* Seek from current position. */
    public int SEEK_END = 2;    /* Seek from end of file.  */
    
    /* From "/usr/include/bits/fcntl.h" */
    public int O_ACCMODE =        0003;
    public int O_RDONLY =           00;
    public int O_WRONLY =           01;
    public int O_RDWR =             02;
    public int O_CREAT =          0100; /* not fcntl */
    public int O_EXCL =           0200; /* not fcntl */
    public int O_NOCTTY =         0400; /* not fcntl */
    public int O_TRUNC =         01000; /* not fcntl */
    public int O_APPEND =        02000;
    public int O_NONBLOCK =      04000;
    public int O_NDELAY =   O_NONBLOCK;
    public int O_SYNC =         010000;
    public int O_FSYNC =        O_SYNC;
    public int O_ASYNC =        020000;

        /* #ifdef __USE_GNU */
    public int O_DIRECT =       040000; /* Direct disk access.  */
    public int O_DIRECTORY =   0200000; /* Must be a directory.  */
    public int O_NOFOLLOW =    0400000; /* Do not follow links.  */
    public int O_NOATIME =    01000000; /* Do not set atime.  */
    public int O_CLOEXEC =    02000000; /* Set close_on_exec.  */
    
    /* custom */
    public int O_LARGEFILE =   0100000;
    
    /* mode */
    public int S_IRWXU = 00700;     /* user (file owner) has read, write and execute permission */
    public int S_IRUSR = 00400;     /* user has read permission */
    public int S_IWUSR = 00200;     /* user has write permission */
    public int S_IXUSR = 00100;     /* user has execute permission */
    public int S_IRWXG = 00070;     /* group has read, write and execute permission */
    public int S_IRGRP = 00040;     /* group has read permission */
    public int S_IWGRP = 00020;     /* group has write permission */
    public int S_IXGRP = 00010;     /* group has execute permission */
    public int S_IRWXO = 00007;     /* others have read, write and execute permission */
    public int S_IROTH = 00004;     /* others have read permission */
    public int S_IWOTH = 00002;     /* others have write permission */
    public int S_IXOTH = 00001;     /* others have execute permission */
        
    /* ========== PVFS2JNI Native Methods START ========== */
    public native int pvfsOpen(String path, int flags, int mode);
    public native int pvfsOpen64(String path, int flags); /* 06/15/2012 */
    public native int pvfsOpenat(int dirfd, String path, int flags);   /* 06/15/2012 */
    public native int pvfsOpenat64(int dirfd, String path, int flags);   /* 06/15/2012 */
    public native int pvfsCreat(String path, int mode);   
    public native int pvfsCreat64(String path, int mode);   
    public native int pvfsUnlink(String path);
    public native int pvfsUnlinkat(int dirfd, String path, int flags);  /* 06/15/2012 */
    public native int pvfsRename(String oldpath, String newpath); 
    public native int pvfsRenameAt(int olddirfd, String oldpath, int newdirfd, String newpath);
    public native int pvfsClose(int fd); 
    public native int pvfsFlush(int fd);  /* 06/15/2012 */
    
    public native long pvfsRead(int fd, long buf, long count); /* 06/18/2012 (INCOMPLETE)*/
    public native long pvfsLseek(int fd, long offset, int whence); /* 06/19/2012 */
    public native long pvfsLseek64(int fd, long offset, int whence); /* 06/19/2012 */
    public native int pvfsTruncate(String path, long length); /* 06/19/2012 */
    public native int pvfsTruncate64(String path, long length); /* 06/19/2012 */
    public native int pvfsFallocate(int fd, long offset, long length); /* 06/19/2012 */
    public native int pvfsFtruncate(int fd, long length); /* 06/19/2012 */
    public native int pvfsFtruncate64(int fd, long length); /* 06/19/2012 */
    public native int pvfsDup(int oldfd); /* 06/19/2012 */
    public native int pvfsDup2(int oldfd, int newfd); /* 06/19/2012 */
    
    public native int pvfsChown(String path, long owner, long group); /* 06/20/2012 */
    public native int pvfsFchown(int fd, long owner, long group); /* 06/20/2012 */
    public native int pvfsFchownat(int fd, String path, long owner, long group, int flag); /* 06/20/2012 */
    public native int pvfsLchown(String path, long owner, long group); /* 06/20/2012 */
    public native int pvfsChmod(String path, int mode); /* 06/20/2012 */
    public native int pvfsFchmod(int fd, int mode); /* 06/20/2012 */
    public native int pvfsFchmodat(int fd, String path, int mode, int flag); /* 06/20/2012 */
    
    public native int pvfsMkdir(String path, int mode);/* 06/19/2012 (TESTED)*/
    public native int pvfsMkdirat(int dirfd, String path, int mode);  /* 06/19/2012 */
    public native int pvfsRmdir(String path); /* 06/19/2012  (TESTED) */
    public native long pvfsReadlink(String path, String buf, long bufsiz);/* 06/19/2012 */
    public native long pvfsReadlinkat(int fd, String path, String buf, long bufsiz);/* 06/19/2012 */
    public native int pvfsSymlink(String oldpath, String newpath);/* 06/19/2012  (TESTED) */
    public native int pvfsSymlinkat(String oldpath, int newdirfd, String newpath);/* 06/19/2012 */
    public native int pvfsLink(String oldpath, String newpath);/* 06/19/2012 */
    public native int pvfsLinkat(int olddirfd, String oldpath, int newdirfd, String newpath, int flags);/* 06/19/2012 */
    public native int pvfsAccess(String path, int mode); /* 06/20/2012 */
    public native int pvfsFaccessat(int fd, String path, int mode, int flags); /* 06/20/2012 */
    public native int pvfsFlock(int fd, int op); /* 06/20/2012 */
    public native int pvfsFcntl(int fd, int cmd); /* 06/20/2012 */
    public native int pvfsFsync(int fd); /* 06/20/2012 */
    public native int pvfsFdatasync(int fd); /* 06/20/2012 */
    public native int pvfsFadvise(int fd, long offset, long len, int advice); /* 06/20/2012 */
    public native int pvfsFadvise64(int fd, long offset, long len, int advice); /* 06/20/2012 */
    public native int pvfsMknod(String path,int mode, int dev); /* 06/20/2012 */
    public native int pvfsMknodat(int dirfd, String path,int mode, int dev); /* 06/20/2012 */
    public native int pvfsChdir(String path); /* 06/20/2012 */
    public native int pvfsFchdir(int fd); /* 06/20/2012 */
    public native int pvfsUmask(int mask); /* 06/20/2012 */

    public native long pvfsRead(int fd, byte [] buf, long count);
    public native long pvfsPread(int fd, byte [] buf, long count, long offset);
    public native long pvfsPread64(int fd, byte [] buf, long count, long offset);
    public native long pvfsWrite(int fd, byte [] buf, long count);
    public native long pvfsPwrite(int fd, byte [] buf, long count, long offset);
    public native long pvfsPwrite64(int fd, byte [] buf, long count, long offset);
    //public native long pvfsReadv(int fd, Iovec [] vector, int count);
    //public native long pvfsWritev(int fd, Iovec [] vector, int count);
    
    //fuctions using the structure stat
    public native int pvfsStat(long x, String path);/* 06/26/2012 (STRUCTURE TESTED) */
    public native int pvfsStatMask(long x, String path, long mask); /* 06/27/2012 (STRUCTURE TESTED) */
    public native int pvfsFstat(long x, int fd); /* 06/27/2012 (STRUCTURE TESTED) */
    public native int pvfsFstatMask(long x, int fd, long mask); /* 06/27/2012 (STRUCTURE TESTED) */
    public native int pvfsFstatat(long x, int fd, String path, int flag); /* 06/27/2012 (STRUCTURE TESTED) */
    public native int pvfsLstat(long x, String path); /* 06/27/2012 */
    public native int pvfsLstatMask(long x, String path, long mask); /* 06/27/2012 */
    
    //fuctions using the structure stat64
    public native int pvfsStat64(long x, String path); /* 06/28/2012  (STRUCTURE TESTED) */
    public native int pvfsFstat64(long x, int fd);
    public native int pvfsFstatat64(long x, int fd, String path, int flag);
    public native int pvfsLstat64(long x, String path);
    
    //fuctions using the structure statfs
    public native int pvfsStatfs(long x, String path);
    public native int pvfsFstatfs(long x, int fd);
    
    //fuctions using the structure statfs64
    public native int pvfsStatfs64(long x, String path);
    public native int pvfsFstatfs64(long x, int fd);
    
    //fuctions using the structure statvfs
    public native int pvfsStatvfs(long x, String path);
    public native int pvfsFstatvfs(long x, int fd);
    
    
    
    //fuctions using the structure dirent
    public native int pvfsReaddir(long x, int fd, int count);
    public native int pvfsGetdents (long x, int fd, int size);
    
    //fuctions using the structure dirent64
    public native int pvfsGetdents64 (long x, int fd, int size);
    
    //fuctions using the structure timeval
    public native int pvfsFutimesat (long x, long y, int dirfd, String path);
    public native int pvfsUtimes (long x, long y,String path);
    public native int pvfsFutimes (long x, long y, int fd);
    
    //fuctions using the structure utimbuf
    public native int pvfsUtime(long x, String path);
    
    
    public native long pvfsListxattr(String path, String list, long size);
    public native long pvfsLlistxattr (String path, String list, long size);
    public native long pvfsFlistxattr (int fd, String list, long size);
    
    public native int pvfsRemovexattr (String path, String name);   
    public native int pvfsLremovexattr (String path, String name); 
    public native int pvfsFremovexattr (int fd, String name);    
    public native int pvfsCwdInit (String buf, long size);
    
    public native long pvfsGetumask();
    public native int pvfsGetdtablesize();
    public native void pvfsSync();
    
    public native long FillStat64(Stat64 t);
    public native long FillStat(Stat t);
    public native long FillDirent(Dirent t);
    public native long FillDirent64(Dirent64 t);
    public native long FillStatfs(Statfs t, long jarg);
    public native long FillStatfs64(Statfs64 t, long jarg);
    public native long FillStatvfs(Statvfs t);
    public native long FillFsid(Fsid t);
    public native long FillUtimbuf(Utimbuf t);
    public native long FillTimeval(Timeval t);
    
    
    
    public native long UseStat64(long x);
    public native long UseStat(long x);
    public native long UseDirent(long x);
    public native long UseDirent64(long x);
    public native long UseStatfs(long x);
    public native long UseStatvfs(long x);
    public native long UseStatfs64(long x);
    public native long UseUtimbuf(long x);
    public native long UseTimeval(long x, long y);
    public native long UseFsid(long x);
    /* ========== PVFS2JNI Native Methods END ========== */
    
    public PVFS2JNI()
    {
        
    }
    
    static {
        System.loadLibrary("PVFS2JNI");
    }

    /* Custom Classes representing C structs */
    public class Stat
    {
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

