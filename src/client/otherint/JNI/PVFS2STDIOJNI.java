/* 
 * (C) 2011 Clemson University
 *
 * See COPYING in top-level directory.
 */

public class PVFS2STDIOJNI {

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
    public native void InitStdio();
    public native void LockInitStream(long stream);
    public native void LockStream(long stream);
    public native int TrylockStream(long stream);
    public native void UnlockStream(long stream);
    public native void LockFiniStream(long stream);
    public native void Flockfile(long stream);
    public native int Ftrylockfile(long stream);
    public native void Funlockfile(long stream);
    public native int Mode2flags(String mode);
    
    public native long Fopen64(String path, String modes);
    public native int InitStream(long stream, int flags, int bufsize);
    public native long Freopen(String path, String mode, long stream);
    public native long Freopen64(String path, String modes, long stream);
    public native long FwriteUnlocked(byte [] ptr, long size, long nmemb, long stream);
    public native long FreadUnlocked(byte [] ptr, long size, long nmemb, long stream);
    public native int Fcloseall();
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
    public native long Opendir(String name);
    public native int Dirfd(long dir);
    public native void Rewinddir(long dir);
    public native void Seekdir(long dir, long offset);
    public native long Telldir(long dir);
    public native int Closedir(long dir);

    public PVFS2STDIOJNI() {}   

    static {
        System.loadLibrary("PVFS2STDIOJNI");
    }
}
