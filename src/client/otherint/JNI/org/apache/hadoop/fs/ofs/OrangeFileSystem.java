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
    private String ofsMount;
    private Path workingDirectory;

    private FileSystem localFS;

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

        System.out.println("uri: " + uri.toString());
        System.out.println("conf: " + conf.toString());

        workingDirectory = new Path("/");
        this.uri = uri;
        this.ofsMount = conf.get("fs.ofs.mntLocation", "/mnt/pvfs2");

        workingDirectory = new Path(
            ofsMount + "/user/" + 
            System.getProperty("user.name")).makeQualified(this);

        System.out.println("workingDirectory = " + workingDirectory.toString());

        /* Set the configuration for this file system in case methods need 
         * access to its values later.
         */
        setConf(conf);

        /* Set the local file system */
        this.localFS = FileSystem.getLocal(conf);

        /* Get OFS statistics */
        statistics = getStatistics(uri.getScheme(), getClass());
        //System.out.println("FileSystem.statistics: " + this.statistics.toString());
        
        System.out.println("getHomeDirectory = " + getHomeDirectory().toString());
        //System.exit(-1);
    }

    private Path absolutePath(Path path) {
        displayMethodInfo(true, false);

        if(path.isAbsolute()) {
            return path;
        }
        return new Path(workingDirectory, path);
    }

    /* Return a Path as a String that OrangeFS can use */
    private String getOFSPath(Path path) {
        Path absolutePath = absolutePath(path);
        return absolutePath.toUri().getPath();
    }

    public Path getHomeDirectory() {
        return this.makeQualified(
        new Path(ofsMount + "/user/"+System.getProperty("user.name")));
    }

    /* Returns a list of parent directories of a given path that don't exist,
     * so that they may be created prior to creation of the file/folder 
     * specified by the path. 
     */
    public Path [] getNonexistentParentPaths(Path f) throws IOException {
        displayMethodInfo(true, false);

        f = absolutePath(f);

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

    // Append to an existing file (optional operation).
    public FSDataOutputStream append(Path f, int bufferSize,
        Progressable progress) throws IOException {
        displayMethodInfo(true, false);

        String fOFS = getOFSPath(f);

        System.out.println("append parameters: {\n\tPath f= " +  fOFS +
            "\n\tint bufferSize= " + bufferSize);

        OrangeFileSystemOutputStream ofsOutputStream =
            new OrangeFileSystemOutputStream(
                fOFS, bufferSize, (short) 0, false);

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

        f = absolutePath(f);
        String fOFS = getOFSPath(f);

        System.out.println("create parameters: {\n\tPath f= " +  fOFS +
            "\n\tFsPermission permission= " + permission.toString() +
            "\n\tboolean overwrite= " + overwrite +
            "\n\tint bufferSize= " + bufferSize +
            "\n\tshort replication= " + replication +
            "\n\tlong blockSize= " + blockSize);

        /* Does this file exist*/
        if(exists(f))
        {
            if(overwrite) {
                delete(f);
            }
            else {
                throw new IOException("The file: " + fOFS + 
                    " exists and overwrite wasn't specified.");
            }
        }
        else {
            Path fParent = f.getParent();
            /* Check if parent directory exists.. if it doesn't call mkdirs on it */
            if(fParent != null && !exists(fParent))
            {
                if(!mkdirs(fParent, permission)) {
                    throw new IOException(
                        "Failed to create parent directorie(s)");
                }
            }
        }

        OrangeFileSystemOutputStream ofsOutputStream = 
            new OrangeFileSystemOutputStream(
                fOFS, bufferSize, replication, false);

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

        String fOFS = getOFSPath(f);

        if(isDir(f)) {
            if(recursive) {
                ret = (orange.stdio.recursiveDelete(fOFS) == 0) ? true : false;
                try {
                    /* TODO: determine why the delete takes a while to 
                     * actually occurr. 
                     */
                    Thread.sleep(3000);
                } catch (InterruptedException e){
                    //TODO
                }
            }
            else {
                ret = (orange.posix.rmdir(fOFS) == 0) ? true : false;
            }
        }
        else {
            ret = (orange.posix.unlink(fOFS) == 0) ? true : false;
        }
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

        String fOFS = getOFSPath(f);

        Stat stats = orange.posix.stat(fOFS);
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

        String fOFS = getOFSPath(f);
        Stat stats = null;
        boolean isdir = false;

        //Stat file through JNI
        stats = orange.posix.stat(fOFS);
        if(stats == null)
        {
            System.out.println("getFileStats not found path: " + fOFS);
            throw new FileNotFoundException();
        }

        isdir = orange.posix.isDir(stats.st_mode) == 0 ? false : true;

        //TODO: handle block replication later
        int block_replication = 0;

        //Get UGO permissions out of st_mode...
        int intPermission = stats.st_mode & 0777;
        String octal = Integer.toOctalString(intPermission);

        System.out.println("stats.st_mode: " + stats.st_mode);
        System.out.println("intPermission: " + intPermission);
        System.out.println("octal perms: " + octal);
        FsPermission permission = new FsPermission(octal);
        System.out.println(permission.toString());

        String [] ug = orange.stdio.getUsernameGroupname((int) stats.st_uid,
            (int) stats.st_gid);
        if(ug == null) {
            System.out.println("getUsernameGroupname returned null");
            throw new IOException();
        }   

        /**/
        System.out.println("uid, username = <" + stats.st_uid + 
            ", " + ug[0] + ">"); 
        System.out.println("gid, groupname = <" + stats.st_gid + 
            ", " + ug[1] + ">");
        /**/

        FileStatus fileStatus = new FileStatus(
            stats.st_size, 
            isdir, 
            block_replication, 
            stats.st_blksize, 
            stats.st_mtime * 1000, //got seconds from pvfs, hadoop expects milliseconds
            stats.st_atime * 1000, //same here
            permission, 
            ug[0], 
            ug[1], 
            absolutePath(f).makeQualified(this));  
        return fileStatus;
    }

    // Returns a URI whose scheme and authority identify this FileSystem.
    public URI getUri() {
        displayMethodInfo(true, false);
        return uri;
    }

    // Get the current working directory for the given file system.
    public Path getWorkingDirectory() {
        displayMethodInfo(true, false);
        return workingDirectory;
    }

    // List the statuses of the files/directories in the given path if the path
    // is a directory.
    public FileStatus[] listStatus(Path f) throws IOException {
        displayMethodInfo(true, false);

        String fOFS = getOFSPath(f);
        System.out.println(fOFS);

        String [] fileNames = orange.stdio.getFilesInDir(fOFS);
        if(fileNames == null)
            return null;

        for(int i = 0; i < fileNames.length; i++) {
            System.out.println(fileNames[i]);
            fileNames[i] = fOFS + "/" + fileNames[i];
        }

        FileStatus[] statusArray = new FileStatus[fileNames.length];
        for(int i = 0; i < fileNames.length; i++) {
            try {
                statusArray[i] = getFileStatus(new Path(fileNames[i]));
            } catch(FileNotFoundException e) {
                //TODO
                return null;
            } catch(IOException e) {
                //TODO
                return null;
            }
        }
        return statusArray;
    }

    // Make the given file and all non-existent parents into directories.
    public boolean mkdirs(Path f, FsPermission permission) throws IOException {
        displayMethodInfo(true, false);

        String fOFS = getOFSPath(f);

        System.out.println("mkdirs attempting to create directory: " + fOFS);
        short shortPermission = permission.toShort();
        //String octal_string_perm = Integer.toOctalString(short_perm);
        //octal_string_perm = "0" + octal_string_perm;
        //long long_perm = Long.parseLong(octal_string_perm, 8);
        //System.out.println("permission as octal string = " + octal_string_perm);

        long longPermission = (long) shortPermission;
        System.out.println("permission toString() = " + permission.toString());
        System.out.println("permission toShort() = " + shortPermission);
        System.out.println("permission as long = " + longPermission);

        /* Check to see if the directory already exists. */
        if(exists(f)) {
            System.err.println("mkdirs failed: " + fOFS + " already exists");
            return false;
        }

        /* Check to see if any parent Paths don't exists */
        Path [] nonPaths = getNonexistentParentPaths(f);
        int toMkdir = nonPaths.length;
        for(int i = 0; i < toMkdir; i++)
        {
            int rc  = orange.posix.mkdir(getOFSPath(nonPaths[i]), longPermission);
            if(rc != 0) {
                throw new IOException("The parent directory: " + 
                    nonPaths[i].toString() + " couldn't be created.");
            }
        }

        int rc  = orange.posix.mkdir(fOFS, longPermission);
        if(rc == 0) {
            return true;
        }
        return false;
    }

    // Opens an FSDataInputStream at the indicated Path.
    public FSDataInputStream open(Path f, int bufferSize)
        throws IOException {
        displayMethodInfo(true, false);

        String fOFS = getOFSPath(f);
        return new FSDataInputStream(new OrangeFileSystemFSInputStream(fOFS, bufferSize, statistics)); 
    }

    // Renames Path src to Path dst.
    public boolean rename(Path src, Path dst) throws IOException {
        displayMethodInfo(true, false);
        String src_string = getOFSPath(src);
        String dst_string = getOFSPath(dst);
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

        //TODO unnecessary???
        String dstOFS = getOFSPath(dst);

        FileUtil.copy(localFS, src, this, new Path(dstOFS), delSrc, getConf());
    }

    public void copyToLocalFile(boolean delSrc, Path src, Path dst)
            throws IOException {
        displayMethodInfo(true, false);

        //TODO unnecessary???
        String srcOFS = getOFSPath(src);
        FileUtil.copy(this, new Path(srcOFS), localFS, dst, delSrc, getConf());
    }

    public Path startLocalOutput(Path fsOutputFile, Path tmpLocalFile) throws IOException {
        displayMethodInfo(true, true);
        return tmpLocalFile;
    }

    public void completeLocalOutput(Path fsOutputFile, Path tmpLocalFile) throws IOException {
        displayMethodInfo(true, true);
        String fsOutputFileOFS = getOFSPath(fsOutputFile);
        moveFromLocalFile(tmpLocalFile, new Path(fsOutputFileOFS));
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
