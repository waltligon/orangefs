/* 
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
 */

package org.apache.hadoop.fs.ofs;

import java.io.IOException;
import java.io.FileNotFoundException;
import java.net.URISyntaxException;

import java.net.URI;
import java.util.Stack;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.lang.reflect.Field;

import org.apache.hadoop.fs.*;
import org.apache.hadoop.fs.permission.FsPermission;
import org.apache.hadoop.util.Progressable;
import org.apache.hadoop.conf.Configuration;

import org.orangefs.usrint.*;

/* An extension of the Hadoop FileSystem class utilizing OrangeFS as the
 * file system.
 */
public class OrangeFileSystem extends FileSystem {

    private Orange orange;
    public PVFS2POSIXJNIFlags pf;
    public PVFS2STDIOJNIFlags sf;

    private URI uri;
    private Path workingDirectory;
    private Path ofsDirectory;
    //TODO: is the local file system necessary for OrangeFS
    //private FileSystem localFS;

    public OrangeFileSystem()
    {
        this.displayMethodInfo(true, false);
        this.orange = Orange.getInstance();
        this.pf = orange.posix.f;
        this.sf = orange.stdio.f;
    }

    /* Called after a new FileSystem instance is constructed.
     * Params:
     * uri - a uri whose authority section names the host, port, etc. for this FileSystem
     * conf - the configuration
     */
    public void initialize(URI uri, Configuration conf) throws IOException {
        displayMethodInfo(true, false);

        //System.out.println("uri: " + uri.toString());
        //System.out.println("conf: " + conf.toString());

        ofsDirectory = new Path(conf.get("fs.ofs.mntLocation", "/mnt/orangefs"));
        workingDirectory = new Path(ofsDirectory.toString() + "/user/" + 
            System.getProperty("user.name"));
        System.out.println("workingDirectory = " + workingDirectory.toString());
        this.uri = uri;
        System.out.println("this.uri: " + this.uri.toString());

        System.exit(-1);
    }

    /* Returns a list of parent directories of a given path that don't exist,
     * so that they may be created prior to creation of the file/folder 
     * specified by the path. 
     */
    public Path [] getNonexistentParentPaths(Path f) throws IOException {
        displayMethodInfo(true, false);

        System.out.println("Path f = " + f);
        System.out.println("depth = " + f.depth());

        ArrayList<Path> list = new ArrayList<Path>();
        Path temp = new Path(f.getParent().toString());
        while(!exists(temp)) {
                System.out.println("Parent path: " + temp.toString() + " is missing.");
                list.add(temp);
                temp = new Path(temp.getParent().toString());
        }
        System.out.println("Missing " + list.size() + " parent directories.");
        Collections.reverse(list);
        return (Path []) list.toArray(new Path[0]);
    }

    /* Return the current user's home directory in this filesystem. 
     */
    public Path getHomeDirectory() {
        displayMethodInfo(true, false);

        return this.ofsDirectory;
    }

    private Path absolutePath(Path path) {
        displayMethodInfo(true, false);

        if(path.isAbsolute()) {
            return path;
        }
        return new Path(workingDirectory, path);
    } 

    // Append to an existing file (optional operation).
    public FSDataOutputStream append(Path f, int bufferSize,
        Progressable progress) throws IOException {
        displayMethodInfo(true, false);

        System.out.println("append parameters: {\n\tPath f= " +  f.toString() +
            "\n\tint bufferSize= " + bufferSize +
            "\n\tProgressable progress= IDK");

        Path fAbs = absolutePath(f);

        OrangeFileSystemOutputStream ofsOutputStream =
            new OrangeFileSystemOutputStream(
                fAbs.toString(), bufferSize, (short) 0, false);

        FSDataOutputStream ofsDataOutputStream = new FSDataOutputStream(
            ofsOutputStream, statistics);
        return ofsDataOutputStream;
    }

    // Opens an FSDataOutputStream at the indicated Path with write-progress
    // reporting.
    public FSDataOutputStream create(Path f, FsPermission permission,
        boolean overwrite, int bufferSize, short replication, long blockSize,
        Progressable progress) throws IOException {
        displayMethodInfo(true, false);

        System.out.println("create parameters: {\n\tPath f= " +  f.toString() +
            "\n\tFsPermission permission= " + permission.toString() +
            "\n\tboolean overwrite= " + overwrite +
            "\n\tint bufferSize= " + bufferSize +
            "\n\tshort replication= " + replication +
            "\n\tlong blockSize= " + blockSize +
            "\n\tProgressable progress= IDK");

        Path fAbs = absolutePath(f);

        /* Does this file exist*/
        if(exists(fAbs))
        {
            if(overwrite) {
                delete(fAbs);
            }
            else {
                throw new IOException("The file: " + fAbs.toString() + 
                    " exists and overwrite wasn't specified.");
            }
        }
        else {
            Path fParent = fAbs.getParent();
            /* Check if parent directory exists.. if it doesn't call mkdirs on it */
            if(fParent != null && !exists(fParent))
            {
                //TODO add permission instead of null
                if(!mkdirs(fParent, null)) {
                    throw new IOException(
                        "Failed to create parent directorie(s)");
                }
            }
        }

        OrangeFileSystemOutputStream ofsOutputStream = 
            new OrangeFileSystemOutputStream(
                fAbs.toString(), bufferSize, replication, false);

        FSDataOutputStream ofsDataOutputStream = new FSDataOutputStream(
            ofsOutputStream, statistics);
        return ofsDataOutputStream;
    }

    /* Delete a file/folder, potentially recursively */
    public boolean delete(Path f, boolean recursive) throws IOException {
        displayMethodInfo(true, false);

        int rc = 0;
        boolean ret = false;

        System.out.println((recursive) ? "Recursive Delete!" : "Standard Delete");

        Path fAbs = absolutePath(f);
        String fAbs_string = fAbs.toUri().getPath();
        if(isDir(fAbs)) {
            if(recursive) {
                ret = (orange.stdio.recursiveDelete(fAbs_string) == 0) ? true : false;
                try {
                    /* TODO: determine why the delete takes a while to 
                     * actually occurr. 
                     */
                    Thread.sleep(3000);
                } catch (InterruptedException e){
                    //
                }
            }
            else {
                ret = (orange.posix.rmdir(fAbs_string) == 0) ? true : false;
            }
        }
        else {
            ret = (orange.posix.unlink(fAbs_string) == 0) ? true : false;
        }
        //
        //System.exit(-1);
        return ret;
    }

    // Check if exists.
    public boolean exists(Path f) {
        displayMethodInfo(true, false);

        //Stat file        
        try { 
            FileStatus status = getFileStatus(f);
            if(status == null) {
                System.out.println(f.toString() + " not found\n!");
            }
        } catch (FileNotFoundException e) {
            //throw new IOException("Couldn't stat file.");
            return false;
        } catch (IOException e) {
            return false;
        }
        return true;
    }

    public boolean isDir(Path f) throws FileNotFoundException {
        displayMethodInfo(true, false);

        Path p = absolutePath(f);
        String s = p.toUri().getPath();
        Stat stats = orange.posix.stat(s);
        if(stats == null)
        {
            throw new FileNotFoundException();
        }
        return orange.posix.isDir(stats.st_mode) != 0 ? true : false;
    }

    // Return a file status object that represents the path.
    public FileStatus getFileStatus(Path f)
        throws FileNotFoundException, IOException {
        displayMethodInfo(true, false);

        Stat stats = null;
        Path p = absolutePath(f);
        String s = p.toUri().getPath();
        boolean isdir = false;

        //Stat file through JNI
        stats = orange.posix.stat(s);
        if(stats == null)
        {
            System.out.println("getFileStats not found path: " + s);
            throw new FileNotFoundException();
        }

        isdir = orange.posix.isDir(stats.st_mode) == 0 ? false : true;

        //TODO deal with these two parameters later
        int block_replication = 0;
        FsPermission perm = new FsPermission("700");

        String [] ug = orange.stdio.getUsernameGroupname((int) stats.st_uid,
            (int) stats.st_gid);
        if(ug == null) {
            System.out.println("getUsernameGroupname returned null");
            throw new IOException();
        }   

        /*
        System.out.println("uid, username = <" + stats.st_uid + 
            ", " + ug[0] + ">"); 
        System.out.println("gid, groupname = <" + stats.st_gid + 
            ", " + ug[1] + ">");
        */

        FileStatus fileStatus = new FileStatus(stats.st_size, isdir, 
            block_replication, stats.st_blksize, stats.st_mtime, 
            stats.st_atime, perm, ug[0], ug[1], p);  
        return fileStatus;
    }

    // Returns a URI whose scheme and authority identify this FileSystem.
    public URI getUri() {
        displayMethodInfo(true, false);

        /*
        try {
            URI u = new URI("");
            return u;
        } catch(URISyntaxException e) {

        }*/
        return uri;
    }

    // Get the current working directory for the given file system.
    public Path getWorkingDirectory() {
        displayMethodInfo(true, false);

        //return new Path("");
        return workingDirectory;
    }

    // List the statuses of the files/directories in the given path if the path
    // is a directory.
    public FileStatus[] listStatus(Path f) throws IOException {
        displayMethodInfo(true, false);

        Path absolutePath = absolutePath(f);
        String s = absolutePath.toUri().getPath();
        System.out.println(s);

        String [] fileNames = orange.stdio.getFilesInDir(s);
        if(fileNames == null)
            return null;

        for(int i = 0; i < fileNames.length; i++) {
            System.out.println(fileNames[i]);
            fileNames[i] = s + "/" + fileNames[i];
        }

        FileStatus[] statusArray = new FileStatus[fileNames.length];
        for(int i = 0; i < fileNames.length; i++) {
            try {
                statusArray[i] = getFileStatus(new Path(fileNames[i]));
            } catch(FileNotFoundException e) {
                return null;
            } catch(IOException e) {
                return null;
            }
        }
        return statusArray;
    }

    // Make sure that a path specifies a FileSystem.
    public Path makeQualified(Path path) {
        displayMethodInfo(true, false);
        // TODO: fix this later
        return path;
    }

    // Make the given file and all non-existent parents into directories.
    public boolean mkdirs(Path f, FsPermission permission) throws IOException {
        displayMethodInfo(true, false);

        Path absolutePath = absolutePath(f);
        String s = absolutePath.toUri().getPath();

        System.out.println("mkdirs attempting to create directory: " + s);
        System.out.println("permission toString() = " + permission.toString());
        System.out.println("permission umask String = " + permission.UMASK_LABEL);
        System.out.println("permission umask int = " + permission.DEFAULT_UMASK);
        System.out.println("permission dep = " + permission.DEPRECATED_UMASK_LABEL);

        /* Check to see if the directory already exists. */
        if(exists(absolutePath)) {
            System.err.println("mkdirs failed: " + s + " already exists");
            return false;
        }

        /* Check to see if any parent Paths don't exists */
        Path [] nonPaths = getNonexistentParentPaths(absolutePath);
        int toMkdir = nonPaths.length;
        for(int i = 0; i < toMkdir; i++)
        {
            //TODO adjust permissions
            int rc  = orange.posix.mkdir(nonPaths[i].toString(), pf.S_IRWXU | pf.S_IRWXG);
            if(rc != 0) {
                throw new IOException("The parent directory: " + 
                    nonPaths[i].toString() + " couldn't be created.");
            }
        }

        int rc  = orange.posix.mkdir(s, pf.S_IRWXU);
        if(rc == 0) {
            return true;
        }
        return false;
    }

    // Opens an FSDataInputStream at the indicated Path.
    public FSDataInputStream open(Path f, int bufferSize)
        throws IOException {
        displayMethodInfo(true, false);
        Path absolutePath = absolutePath(f);
        String s = absolutePath.toUri().getPath();
        return new FSDataInputStream(new OrangeFileSystemFSInputStream(s, bufferSize, statistics)); 
    }

    // Renames Path src to Path dst.
    public boolean rename(Path src, Path dst) throws IOException {
        displayMethodInfo(true, false);
        String src_string = absolutePath(src).toUri().getPath();
        String dst_string = absolutePath(dst).toUri().getPath();
        return (orange.posix.rename(src_string, dst_string) == 0);
    }

    // Set the current working directory for the given file system.
    public void setWorkingDirectory(Path new_dir) {
        displayMethodInfo(true, false);
        workingDirectory = absolutePath(new_dir);
    }

    public void copyFromLocalFile(boolean delSrc, Path src, Path dst)
            throws IOException {
        displayMethodInfo(true, false);
        //FileUtil.copy(localFS, src, this, dst, delSrc, getConf());
        FileUtil.copy(this, src, this, dst, delSrc, getConf());
    }

    public void copyToLocalFile(boolean delSrc, Path src, Path dst)
            throws IOException {
        displayMethodInfo(true, false);
        //FileUtil.copy(this, src, localFS, dst, delSrc, getConf());
        FileUtil.copy(this, src, this, dst, delSrc, getConf());
    }

    // Deprecated. Use delete(Path, boolean) instead
    @Deprecated
    public boolean delete(Path f) {
        displayMethodInfo(true, false);
        return false;
    }

    public void displayMethodInfo(boolean showName, boolean showStack) {
        if(showName || showStack) {
            String methodName =
                Thread.currentThread().getStackTrace()[2].getMethodName();
            if(showName) {
                System.out.println("method=[" + methodName + "]");
            }
            if(showStack) {
                System.out.print("\t");
                Thread.currentThread().dumpStack();
            }
        }
    }
}
