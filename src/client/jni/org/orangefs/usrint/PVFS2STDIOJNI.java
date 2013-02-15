/* 
 * (C) 2011 Clemson University
 *
 * See COPYING in top-level directory.
 */

package org.orangefs.usrint;

import java.lang.reflect.Field;

public class PVFS2STDIOJNI {

    public PVFS2STDIOJNIFlags f;

    /* ========== PVFS2STDIOJNI Native Methods START ========== */
    public native PVFS2STDIOJNIFlags fillPVFS2STDIOJNIFlags();
    
    public native String [] getUsernameGroupname(int uid, int gid);
    public native String [] getFilesInDir(String path);
    public native int recursiveDelete(String path);

    public native long malloc(long size);
    public native long calloc(long nmemb, long size);
    public native void free(long ptr);

    public native long fopen(String path, String mode);
    public native int fclose(long stream);
    public native int setvbuf(long stream, long buf, long mode, long size);
    public native long fdopen(int fd, String mode);
    public native int fileno(long stream);
    public native int remove(String path); 
    public native long fread(byte [] ptr, long size, long nmemb, long stream);
    public native long fwrite(byte [] ptr, long size, long nmemb, long stream);
    public native long fdopendir(int fd);

    public native void flockfile(long stream);
    public native int ftrylockfile(long stream);
    public native void funlockfile(long stream);
    
    public native long fopen64(String path, String mode);
    public native long freopen(String path, String mode, long stream);
    public native long freopen64(String path, String mode, long stream);
    public native long fwriteUnlocked(byte [] ptr, long size, long nmemb, long stream);
    public native long freadUnlocked(byte [] ptr, long size, long nmemb, long stream);
    public native int fcloseall();
    public native long ftell(long stream);
    public native int fseek(long stream, long offset, long whence);
    public native int fseek64(long stream, long offset, long whence);
    public native long fseeko(long stream, long offset, long whence);
    public native long fseeko64(long stream, long offset, long whence);

    public native int fflush(long stream);
    public native int fflushUnlocked(long stream);
    public native int fputc(int c, long stream);
    public native int fputcUnlocked(int c, long stream);
    public native int fputs(String s, long stream);
    public native int fputsUnlocked(String s, long stream);
    public native int putc(int c, long stream);
    public native int putcUnlocked(int c, long stream);
    public native int putchar(int c);
    public native int putcharUnlocked(int c);
    public native int puts(String s);
    public native int putw(int wd, long stream);
    public native String fgets(String s, int size, long stream);
    public native String fgetsUnlocked(String s, int size, long stream);
    public native int fgetc(long stream);
    public native int fgetcUnlocked(long stream);
 
    public native int getc(long stream);
    public native int getcUnlocked(long stream);
    public native int getchar();
    public native int getcharUnlocked();
    public native int getw(long stream);
    public native String gets(String s);
    
    public native int ungetc(int c, long stream);
    public native void perror(String s);
    public native void clearerr(long stream);
    public native void clearerrUnlocked(long stream);
    public native int feof(long stream);
    public native int feofUnlocked(long stream);
    public native int ferror(long stream);
    public native int ferrorUnlocked(long stream);
    public native int filenoUnlocked(long stream);
    public native void setbuf(long stream, String buf);
    public native void setbuffer(long stream, String buf, long size);
    public native void setlinebuf(long stream);
    public native String mkdtemp(String tmplate);
    public native int mkstemp(String tmplate);
    public native long tmpfile();
    public native Dirent opendir(String name);
    public native int dirfd(long dir);
    public native void rewinddir(long dir);
    public native void seekdir(long dir, long offset);
    public native long telldir(long dir);
    public native int closedir(long dir);

    public PVFS2STDIOJNI() {
        this.f = this.fillPVFS2STDIOJNIFlags();
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
        String ldlpath = System.getenv("JNI_LIBRARY_PATH");
        //System.out.println("ldlpath=" + ldlpath);
        try {
            System.load(ldlpath + "/libPVFS2STDIOJNI.so");
        } catch (UnsatisfiedLinkError error) {
            error.printStackTrace();
            System.err.println("Couldn't load libPVFS2STDIOJNI.so.");
            System.err.println("JNI_LIBRARY_PATH = " + System.getenv("JNI_LIBRARY_PATH"));
            System.exit(-1);
        }
    }
}
