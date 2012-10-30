package org.orangefs.usrint;

import java.lang.reflect.Field;

public class PVFS2POSIXJNIFlags {
    /* Fields set by JNI function fill_PVFS2POSIXJNIFlags.
    * See posix_flags.c 
    */
    public long O_WRONLY;
    public long O_RDONLY;
    public long PVFS_IO_READ;
    public long PVFS_IO_WRITE;
    public long O_ACCMODE;
    public long O_HINTS;
    public long PVFS_HINT_NULL;
    public long O_LARGEFILE;
    public long AT_FDCWD;
    public long O_EXCL;
    public long AT_REMOVEDIR;
    public long O_APPEND;
    public long PVFS_FD_FAILURE;
    public long PVFS_ATTR_DEFAULT_MASK;
    public long AT_SYMLINK_NOFOLLOW;
    public long O_NOFOLLOW;
    public long PVFS_ATTR_SYS_ATIME;
    public long PVFS_ATTR_SYS_MTIME;
    public long O_CLOEXEC;
    public long O_ASYNC;
    public long O_CREAT;
    public long O_DIRECT;
    public long O_DIRECTORY;
    public long O_NOATIME;
    public long O_NOCTTY;
    public long O_NONBLOCK;
    public long FD_CLOEXEC;
    public long ST_RDONLY;
    public long ST_NOSUID;
    public long S_IRUSR;
    public long S_IWUSR;
    public long S_IXUSR;
    public long S_IRWXG;
    public long S_IRGRP;
    public long S_IWGRP;
    public long S_IXGRP;
    public long S_IRWXO;
    public long S_IROTH;
    public long S_IWOTH;
    public long S_IXOTH;
/*
    public long S_ISREG;
    public long S_ISDIR;
    public long S_ISCHR;
    public long S_ISBLK;
    public long S_ISFIFO;
    public long S_ISLINK;
    public long S_ISSOCK;
*/
    public long S_IFMT;
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
    public long S_IRWXU;

    /* Constructor won't matter since we're calling JNI allocObject */
    PVFS2POSIXJNIFlags(){}

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
