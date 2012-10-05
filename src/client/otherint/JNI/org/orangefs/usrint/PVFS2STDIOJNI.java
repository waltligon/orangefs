/* 
 * (C) 2011 Clemson University
 *
 * See COPYING in top-level directory.
 */

package org.orangefs.usrint;

import java.lang.reflect.Field;

public class PVFS2STDIOJNI {

    /* from stdio.h: "The possibilities for the third argument to `setvbuf'." */
    public int _IOFBF = 0; /* Fully buffered.  */
    public int _IOLBF = 1; /* Line buffered.  */
    public int _IONBF = 2; /* No buffering.  */

    /* from stdio.h: "The possibilities for the third argument to `fseek'." */
    public int SEEK_SET = 0; /* Seek from beginning of file.  */
    public int SEEK_CUR = 1; /* Seek from current position.  */
    public int SEEK_END = 2; /* Seek from end of file.  */

    public native String [] getUsernameGroupname(int uid, int gid);
    public native String [] getFilesInDir(String path);
    public native int recursiveDelete(String path);

    public native long Malloc(long size);
    public native void Free(long ptr);

    public native long Fopen(String path, String mode);
    public native int Fclose(long stream);
    public native int Setvbuf(long stream, long buf, int mode, long size);
    public native long Fdopen(int fd, String mode);
    public native int Fileno(long stream);
    public native int Remove(String path); 
    public native int Fseek(long stream, long offset, int whence);
    public native long Fread(byte [] ptr, long size, long nmemb, long stream);
    public native long Fwrite(byte [] ptr, long size, long nmemb, long stream);
    public native long Fdopendir(int fd);

    public native void Flockfile(long stream);
    public native int Ftrylockfile(long stream);
    public native void Funlockfile(long stream);
    
    public native long Fopen64(String path, String modes);
    public native long Freopen(String path, String mode, long stream);
    public native long Freopen64(String path, String modes, long stream);
    public native long FwriteUnlocked(byte [] ptr, long size, long nmemb, long stream);
    public native long FreadUnlocked(byte [] ptr, long size, long nmemb, long stream);
    public native int Fcloseall();
    public native long Ftell(long stream);
    public native int Fseeko(long stream, long offset, int whence);
    public native int Fseeko64(long stream, long offset, int whence);
    public native int Fseek64(long stream, long offset, int whence);

    public native int Fflush(long stream);
    public native int FflushUnlocked(long stream);
    public native int Fputc(int c, long stream);
    public native int FputcUnlocked(int c, long stream);
    public native int Fputs(String s, long stream);
    public native int FputsUnlocked(String s, long stream);
    public native int Putc(int c, long stream);
    public native int PutcUnlocked(int c, long stream);
    public native int Putchar(int c);
    public native int PutcharUnlocked(int c);
    public native int Puts(String s);
    public native int Putw(int wd, long stream);
    public native String Fgets(String s, int size, long stream);
    public native String FgetsUnlocked(String s, int size, long stream);
    public native int Fgetc(long stream);
    public native int FgetcUnlocked(long stream);
 
    public native int Getc(long stream);
    public native int GetcUnlocked(long stream);
    public native int Getchar();
    public native int GetcharUnlocked();
    public native int Getw(long stream);
    public native String Gets(String s);
    
    public native int Ungetc(int c, long stream);
    public native void Perror(String s);
    public native void Clearerr(long stream);
    public native void ClearerrUnlocked(long stream);
    public native int Feof(long stream);
    public native int FeofUnlocked(long stream);
    public native int Ferror(long stream);
    public native int FerrorUnlocked(long stream);
    public native int FilenoUnlocked(long stream);
    public native void Setbuf(long stream, String buf);
    public native void Setbuffer(long stream, String buf, long size);
    public native void Setlinebuf(long stream);
    public native String Mkdtemp(String tmplate);
    public native int Mkstemp(String tmplate);
    public native long Tmpfile();
    public native PVFS2STDIOJNI.Dir Opendir(String name);
    public native int Dirfd(long dir);
    public native void Rewinddir(long dir);
    public native void Seekdir(long dir, long offset);
    public native long Telldir(long dir);
    public native int Closedir(long dir);

    public PVFS2STDIOJNI() {}   

    static {
        try {
            System.loadLibrary("PVFS2STDIOJNI");
        } catch (UnsatisfiedLinkError error) {
            error.printStackTrace();
            System.err.println("Couldn't load libPVFS2STDIOJNI.so.");
            System.err.println("java.library.path = " + System.getProperty("java.library.path"));
            System.exit(-1);
        }
    }

    public class Dir {
        public long d_ino;
        public long d_off;
        public int d_reclen;
        public String d_type;
        public String d_name;

        /* Constructor */
        Dir(){}

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

    public class Dir64 {
        public long d_ino;
        public long d_off;
        public int d_reclen;
        public String d_type;
        public String d_name;

        /* Constructor */
        Dir64(){}
        
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
}
