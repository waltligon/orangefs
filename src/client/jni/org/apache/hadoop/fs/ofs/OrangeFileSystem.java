/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
 */

package org.apache.hadoop.fs.ofs;

import java.io.FileNotFoundException;
import java.io.IOException;
import java.net.URI;
import java.util.ArrayList;
import java.util.Collections;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FSDataInputStream;
import org.apache.hadoop.fs.FSDataOutputStream;
import org.apache.hadoop.fs.FileStatus;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.FileUtil;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.fs.permission.FsPermission;
import org.apache.hadoop.util.Progressable;
import org.orangefs.usrint.Orange;
import org.orangefs.usrint.OrangeFileSystemOutputStream;
import org.orangefs.usrint.PVFS2POSIXJNIFlags;
import org.orangefs.usrint.PVFS2STDIOJNIFlags;
import org.orangefs.usrint.Stat;

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

    public static final Log OFSLOG = LogFactory.getLog(OrangeFileSystem.class);

    public OrangeFileSystem() {
        this.displayMethodInfo(true, false);
        this.orange = Orange.getInstance();
        this.pf = orange.posix.f;
        this.sf = orange.stdio.f;
    }

    /*
     * Called after a new FileSystem instance is constructed. Params: uri - a
     * uri whose authority section names the host, port, etc. for this
     * FileSystem conf - the configuration
     */
    @Override
    public void initialize(URI uri, Configuration conf) throws IOException {
        displayMethodInfo(true, false);

        OFSLOG.debug("uri: " + uri.toString());
        OFSLOG.debug("conf: " + conf.toString());

        workingDirectory = new Path("/");
        this.uri = uri;
        this.ofsMount = conf.get("fs.ofs.mntLocation", "/mnt/pvfs2");

        workingDirectory = new Path(ofsMount + "/user/"
                + System.getProperty("user.name")).makeQualified(this);

        OFSLOG.debug("workingDirectory = " + workingDirectory.toString());

        /*
         * Set the configuration for this file system in case methods need
         * access to its values later.
         */
        setConf(conf);

        /* Set the local file system */
        this.localFS = FileSystem.getLocal(conf);

        /* Get OFS statistics */
        statistics = getStatistics(uri.getScheme(), getClass());
        OFSLOG.debug("OrangeFileSystem.statistics: "
                + this.statistics.toString());

        OFSLOG.debug("home directory = " + getHomeDirectory().toString());
    }

    private Path absolutePath(Path path) {
        displayMethodInfo(true, false);

        if (path.isAbsolute()) {
            return path;
        }
        return new Path(workingDirectory, path);
    }

    /* Return a Path as a String that OrangeFS can use */
    private String getOFSPath(Path path) {
        Path absolutePath = absolutePath(path);
        return absolutePath.toUri().getPath();
    }

    @Override
    public Path getHomeDirectory() {
        return this.makeQualified(new Path(ofsMount + "/user/"
                + System.getProperty("user.name")));
    }

    /*
     * Returns a list of parent directories of a given path that don't exist, so
     * that they may be created prior to creation of the file/folder specified
     * by the path.
     */
    public Path[] getNonexistentParentPaths(Path f) throws IOException {
        displayMethodInfo(true, false);

        f = absolutePath(f);

        OFSLOG.debug("Path f = " + f);
        OFSLOG.debug("depth = " + f.depth());

        ArrayList<Path> list = new ArrayList<Path>();
        Path temp = new Path(f.getParent().toString());
        while (!exists(temp)) {
            OFSLOG.debug("Parent path: " + temp.toString() + " is missing.");
            list.add(temp);
            temp = new Path(temp.getParent().toString());
        }
        OFSLOG.debug("Missing " + list.size() + " parent directories.");
        Collections.reverse(list);
        return list.toArray(new Path[0]);
    }

    // Append to an existing file (optional operation).
    @Override
    public FSDataOutputStream append(Path f, int bufferSize,
            Progressable progress) throws IOException {
        displayMethodInfo(true, false);

        String fOFS = getOFSPath(f);

        OFSLOG.debug("append parameters: {\n\tPath f= " + fOFS
                + "\n\tint bufferSize= " + bufferSize);

        OrangeFileSystemOutputStream ofsOutputStream = new OrangeFileSystemOutputStream(
                fOFS, bufferSize, (short) 0, false);

        FSDataOutputStream ofsDataOutputStream = new FSDataOutputStream(
                ofsOutputStream, statistics);
        return ofsDataOutputStream;
    }

    // Opens an FSDataOutputStream at the indicated Path with write-progress
    // reporting.
    @Override
    public FSDataOutputStream create(Path f, FsPermission permission,
            boolean overwrite, int bufferSize, short replication,
            long blockSize, Progressable progress) throws IOException {

        /*
         * Call getOFS Path to get a string representing the path, f, without
         * the Hadoop prefix 'ofs://' etc.
         */
        String fOFS = null;

        /*
         * If needed store a Path object representing the parent directory of
         * the file being created.
         */
        Path fParent = null;

        displayMethodInfo(true, false);

        /* If path isn't absolute, then reference working directory */
        f = absolutePath(f);
        /* Create a string OrangeFS can understand. '/mnt/orangefs/...' */
        fOFS = getOFSPath(f);

        OFSLOG.debug("create parameters: {\n\tPath f= " + fOFS
                + "\n\tFsPermission permission= " + permission.toString()
                + "\n\tboolean overwrite= " + overwrite
                + "\n\tint bufferSize= " + bufferSize
                + "\n\tshort replication= " + replication
                + "\n\tlong blockSize= " + blockSize);

        /* Does this file exist */
        if (exists(f)) {
            /* Delete existing file */
            if (overwrite) {
                delete(f, false);
            }
            /* Cannot delete existing file if it exists. */
            else {
                throw new IOException(
                        "file: "
                                + fOFS
                                + " exists and overwrite wasn't specified with this creation.");
            }
        }
        /* New file */
        else {
            /*
             * Check if parent directory exists.. if it doesn't call mkdirs on
             * it
             */
            fParent = f.getParent();
            if (fParent != null && !exists(fParent)) {
                /* Try to create the directories. */
                if (!mkdirs(fParent, permission)) {
                    throw new IOException(
                            "Failed to create parent directorie(s): "
                                    + fParent.toString());
                }
            }
        }

        /* Create the File */
        OrangeFileSystemOutputStream ofsOutputStream = new OrangeFileSystemOutputStream(
                fOFS, bufferSize, replication, false);

        /* Create the FSDataOutputStream from the OrangeFileSystemOutputStream */
        FSDataOutputStream ofsDataOutputStream = new FSDataOutputStream(
                ofsOutputStream, statistics);

        return ofsDataOutputStream;
    }

    /* Delete a file/folder, potentially recursively */
    @Override
    public boolean delete(Path f, boolean recursive) throws IOException {

        boolean ret = false;

        displayMethodInfo(true, false);
        OFSLOG.debug((recursive) ? "Recursive Delete!" : "Non-recursive Delete");

        if (!exists(f)) {
            return false;
        }

        if (isDir(f) && recursive) {
            ret = (orange.stdio.recursiveDelete(f.toString()) == 0) ? true
                    : false;
        }
        else {
            ret = (orange.stdio.remove(f.toString()) == 0) ? true : false;
        }
        return ret;
    }

    // Check if exists.
    @Override
    public boolean exists(Path f) {
        displayMethodInfo(true, false);

        // Stat file
        try {
            FileStatus status = getFileStatus(f);
            if (status == null) {
                OFSLOG.debug(f.toString() + " not found\n!");
            }
        }
        catch (FileNotFoundException e) {
            // OFSLOG.error(f.toString() + " not found\n!");
            return false;
        }
        catch (IOException e) {
            OFSLOG.error("File:" + f.toString());
            return false;
        }
        return true;
    }

    public boolean isDir(Path f) throws FileNotFoundException {
        displayMethodInfo(true, false);

        String fOFS = getOFSPath(f);

        Stat stats = orange.posix.stat(fOFS);
        if (stats == null) {
            OFSLOG.error(f.toString() + " not found\n!");
            throw new FileNotFoundException();
        }
        return orange.posix.isDir(stats.st_mode) != 0 ? true : false;
    }

    // Return a file status object that represents the path.
    @Override
    public FileStatus getFileStatus(Path f) throws FileNotFoundException,
            IOException {
        displayMethodInfo(true, false);

        String fOFS = getOFSPath(f);
        Stat stats = null;
        boolean isdir = false;

        // Stat file through JNI
        stats = orange.posix.stat(fOFS);
        if (stats == null) {
            OFSLOG.debug("getFileStats not found path: " + fOFS);
            throw new FileNotFoundException();
        }

        isdir = orange.posix.isDir(stats.st_mode) == 0 ? false : true;

        // TODO: handle block replication later
        int block_replication = 0;

        // Get UGO permissions out of st_mode...
        int intPermission = stats.st_mode & 0777;
        String octal = Integer.toOctalString(intPermission);

        OFSLOG.debug("stats.st_mode: " + stats.st_mode);
        OFSLOG.debug("intPermission: " + intPermission);
        OFSLOG.debug("octal perms: " + octal);

        FsPermission permission = new FsPermission(octal);
        OFSLOG.debug(permission.toString());

        String[] ug = orange.stdio.getUsernameGroupname((int) stats.st_uid,
                (int) stats.st_gid);
        if (ug == null) {
            OFSLOG.warn("getUsernameGroupname returned null");
            throw new IOException();
        }

        /**/
        OFSLOG.debug("uid, username = <" + stats.st_uid + ", " + ug[0] + ">");
        OFSLOG.debug("gid, groupname = <" + stats.st_gid + ", " + ug[1] + ">");
        /**/

        FileStatus fileStatus = new FileStatus(stats.st_size, isdir,
                block_replication, stats.st_blksize, stats.st_mtime * 1000,
                stats.st_atime * 1000, permission, ug[0], ug[1],
                absolutePath(f).makeQualified(this));
        return fileStatus;
    }

    // Returns a URI whose scheme and authority identify this FileSystem.
    @Override
    public URI getUri() {
        displayMethodInfo(true, false);
        return uri;
    }

    // Get the current working directory for the given file system.
    @Override
    public Path getWorkingDirectory() {
        displayMethodInfo(true, false);
        return workingDirectory;
    }

    // List the statuses of the files/directories in the given path if the path
    // is a directory.
    @Override
    public FileStatus[] listStatus(Path f) throws IOException {
        displayMethodInfo(true, false);

        String fOFS = getOFSPath(f);
        OFSLOG.debug(fOFS);

        String[] fileNames = orange.stdio.getFilesInDir(fOFS);
        if (fileNames == null)
            return null;

        for (int i = 0; i < fileNames.length; i++) {
            OFSLOG.debug(fileNames[i]);
            fileNames[i] = fOFS + "/" + fileNames[i];
        }

        FileStatus[] statusArray = new FileStatus[fileNames.length];
        for (int i = 0; i < fileNames.length; i++) {
            try {
                statusArray[i] = getFileStatus(new Path(fileNames[i]));
            }
            catch (FileNotFoundException e) {
                // TODO
                return null;
            }
            catch (IOException e) {
                // TODO
                return null;
            }
        }
        return statusArray;
    }

    // Make the given file and all non-existent parents into directories.
    @Override
    public boolean mkdirs(Path f, FsPermission permission) throws IOException {

        int ret = 0;
        String fOFS = getOFSPath(f);
        short shortPermission = permission.toShort();
        long longPermission = shortPermission;

        displayMethodInfo(true, false);
        OFSLOG.debug("mkdirs attempting to create directory: " + f.toString());
        OFSLOG.debug("permission toString() = " + permission.toString());
        OFSLOG.debug("permission toShort() = " + shortPermission);
        OFSLOG.debug("permission as long = " + longPermission);

        /* Check to see if the directory already exists. */
        if (exists(f)) {
            OFSLOG.warn("mkdirs failed: " + f.toString() + " already exists");
            return true;
        }

        /* Check to see if any parent Paths don't exists */
        Path[] nonPaths = getNonexistentParentPaths(f);
        int toMkdir = nonPaths.length;
        for (int i = 0; i < toMkdir; i++) {
            OFSLOG.debug("creating parent directory: "
                    + getOFSPath(nonPaths[i]));
            ret = orange.posix.mkdir(getOFSPath(nonPaths[i]), longPermission);
            if (ret != 0) {
                throw new IOException("The parent directory: "
                        + nonPaths[i].toString() + " couldn't be created.");
            }
        }
        ret = orange.posix.mkdir(fOFS, longPermission);
        if (ret == 0) {
            return true;
        }
        return false;
    }

    // Opens an FSDataInputStream at the indicated Path.
    @Override
    public FSDataInputStream open(Path f, int bufferSize) throws IOException {
        displayMethodInfo(true, false);

        String fOFS = getOFSPath(f);
        return new FSDataInputStream(new OrangeFileSystemFSInputStream(fOFS,
                bufferSize, statistics));
    }

    // Renames Path src to Path dst.
    @Override
    public boolean rename(Path src, Path dst) throws IOException {
        displayMethodInfo(true, false);
        String src_string = getOFSPath(src);
        String dst_string = getOFSPath(dst);
        return (orange.posix.rename(src_string, dst_string) == 0);
    }

    // Set the current working directory for the given file system.
    @Override
    public void setWorkingDirectory(Path new_dir) {
        displayMethodInfo(true, false);
        workingDirectory = absolutePath(new_dir);
    }

    @Override
    public void copyFromLocalFile(boolean delSrc, Path src, Path dst)
            throws IOException {
        displayMethodInfo(true, false);

        // TODO unnecessary???
        String dstOFS = getOFSPath(dst);

        FileUtil.copy(localFS, src, this, new Path(dstOFS), delSrc, getConf());
    }

    @Override
    public void copyToLocalFile(boolean delSrc, Path src, Path dst)
            throws IOException {
        displayMethodInfo(true, false);

        // TODO unnecessary???
        String srcOFS = getOFSPath(src);
        FileUtil.copy(this, new Path(srcOFS), localFS, dst, delSrc, getConf());
    }

    @Override
    public Path startLocalOutput(Path fsOutputFile, Path tmpLocalFile)
            throws IOException {
        displayMethodInfo(true, true);
        return tmpLocalFile;
    }

    @Override
    public void completeLocalOutput(Path fsOutputFile, Path tmpLocalFile)
            throws IOException {
        displayMethodInfo(true, true);
        String fsOutputFileOFS = getOFSPath(fsOutputFile);
        moveFromLocalFile(tmpLocalFile, new Path(fsOutputFileOFS));
    }

    // Deprecated. Use delete(Path, boolean) instead
    @Override
    @Deprecated
    public boolean delete(Path f) {
        displayMethodInfo(true, false);
        return false;
    }

    public void displayMethodInfo(boolean showName, boolean showStack) {
        if (showName || showStack) {
            String methodName = Thread.currentThread().getStackTrace()[2]
                    .getMethodName();
            if (showName) {
                OFSLOG.debug("method=[" + methodName + "]");
            }
            /*
             * if(showStack) { System.out.print("\t");
             * Thread.currentThread().dumpStack(); }
             */
        }
    }
}
