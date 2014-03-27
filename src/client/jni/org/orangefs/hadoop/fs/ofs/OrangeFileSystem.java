/*
 * (C) 2012 Clemson University
 *
 * See COPYING in top-level directory.
 */
package org.orangefs.hadoop.fs.ofs;

import java.io.FileNotFoundException;
import java.io.IOException;
import java.net.URI;
import java.util.ArrayList;

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
    private static final Log OFSLOG = LogFactory.getLog(OrangeFileSystem.class);
    private boolean initialized;

    /*
     * After OrangeFileSystem is constructed, called initialize to set fields.
     */
    public OrangeFileSystem() {
        this.orange = Orange.getInstance();
        this.pf = orange.posix.f;
        this.sf = orange.stdio.f;
        this.initialized = false;
    }

    /* Append to an existing file (optional operation). */
    @Override
    public FSDataOutputStream append(Path f, int bufferSize,
            Progressable progress) throws IOException {
        Path fOFS = new Path(getOFSPathName(f));
        OFSLOG.debug("append parameters: {\n\tPath f= " + fOFS
                + "\n\tint bufferSize= " + bufferSize);
        OrangeFileSystemOutputStream ofsOutputStream = new OrangeFileSystemOutputStream(
                fOFS.toString(), bufferSize, (short) 0, true);
        return new FSDataOutputStream(ofsOutputStream, statistics);
    }

    @Override
    public void completeLocalOutput(Path fsOutputFile, Path tmpLocalFile)
            throws IOException {
        moveFromLocalFile(tmpLocalFile, fsOutputFile);
    }

    @Override
    public void copyFromLocalFile(boolean delSrc, Path src, Path dst)
            throws IOException {
        FileUtil.copy(localFS, src, this, dst, delSrc, getConf());
    }

    @Override
    public void copyToLocalFile(boolean delSrc, Path src, Path dst)
            throws IOException {
        FileUtil.copy(this, src, localFS, dst, delSrc, getConf());
    }

    /*
     * Opens an FSDataOutputStream at the indicated Path with write-progress
     * reporting.
     */
    @Override
    public FSDataOutputStream create(Path f, FsPermission permission,
            boolean overwrite, int bufferSize, short replication,
            long blockSize, Progressable progress) throws IOException {
        Path fOFS = null;
        Path fParent = null;
        FSDataOutputStream fsdos = null;
        fOFS = new Path(getOFSPathName(f));
        OFSLOG.debug("create parameters: {\n\tPath f= " + f.toString()
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
            /* Cannot delete existing file if it exists (without overwrite). */
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
             * it.
             */
            fParent = f.getParent();
            OFSLOG.debug("fParent = " + fParent);
            if (fParent != null && !exists(fParent)) {
                /* Try to create the directories. */
                if (!mkdirs(fParent, permission)) {
                    /* mkdirs could fail if another task creates the parent 
                     * directory after we checked to see if the parent exists.
                     * So, check if the parent exists again to make sure
                     * mkdirs didn't fail because another task already
                     * successfully called mkdir on the parent
                     * directory/directories.
                     */
                    if(!exists(fParent)) {
                        throw new IOException(
                                "Failed to create parent directory/directories: "
                                        + fParent.toString());
                    }
                }
            }
        }
        fsdos = new FSDataOutputStream(new OrangeFileSystemOutputStream(fOFS
                .toString(), bufferSize, replication, false), statistics);
        /* Set the desired permission. */
        setPermission(f, permission);
        return fsdos;
    }

    /* Deprecated. Use delete(Path, boolean) instead. */
    @Override
    @Deprecated
    public boolean delete(Path f) {
        return false;
    }

    /* Delete a file/folder, potentially recursively */
    @Override
    public boolean delete(Path f, boolean recursive) throws IOException {
        boolean ret = false;
        FileStatus status = null;
        Path fOFS = new Path(getOFSPathName(f));
        OFSLOG.debug("Path f = " + f);
        OFSLOG.debug((recursive) ? "Recursive Delete!" : "Non-recursive Delete");
        try {
            status = getFileStatus(f);
        }
        catch (FileNotFoundException e) {
            OFSLOG.debug(makeAbsolute(f) + " not found!");
            return false;
        }
        catch (IOException e) {
            OFSLOG.error("File:" + makeAbsolute(f));
            return false;
        }
        if(status.isDir()) {
            if(!recursive) {
                OFSLOG.debug("Couldn't delete Path f = " + f + " since it is "
                        + "a directory but recursive is false.");
                return false;
            }
            // Call recursive delete on path
            OFSLOG.debug("Path f =" + f
                    + " is a directory and recursive is true."
                    + " Recursively deleting directory.");
            ret = (orange.stdio.recursiveDelete(fOFS.toString()) == 0) ? true
                    : false;
        }
        else {
            OFSLOG.debug("Path f =" + f
                    + " exists and is a regular file. unlinking.");
            ret = (orange.posix.unlink(fOFS.toString()) == 0) ? true : false;
        }
        // Return false on failure.
        if(!ret) {
            OFSLOG.debug("remove failed: ret == false\n");
        }
        return ret;
    }

    /* Check if exists. */
    @Override
    public boolean exists(Path f) {
        /* Stat file */
        try {
            @SuppressWarnings("unused")
            FileStatus status = getFileStatus(f);
        }
        catch (FileNotFoundException e) {
            OFSLOG.debug(makeAbsolute(f) + " not found!");
            return false;
        }
        catch (IOException e) {
            OFSLOG.error("File:" + makeAbsolute(f));
            return false;
        }
        return true;
    }

    /* Return a file status object that represents the path. */
    @Override
    public FileStatus getFileStatus(Path f) throws FileNotFoundException,
            IOException {
        Stat stats = null;
        FileStatus fileStatus = null;
        boolean isdir = false;
        int block_replication = 0; /* TODO: handle replication. */
        int intPermission = 0;
        String octal = null;
        FsPermission permission = null;
        Path fOFS = new Path(getOFSPathName(f));
        OFSLOG.debug("f = " + makeAbsolute(f));
        stats = orange.posix.stat(fOFS.toString());
        if (stats == null) {
            OFSLOG.debug("stat(" + makeAbsolute(f) + ") returned null");
            throw new FileNotFoundException();
        }
        isdir = orange.posix.isDir(stats.st_mode) == 0 ? false : true;
        OFSLOG.debug("file/directory=" + (isdir == true ? "directory" : "file"));
        /* Get UGO permissions out of st_mode... */
        intPermission = stats.st_mode & 0777;
        octal = Integer.toOctalString(intPermission);
        OFSLOG.debug("stats.st_mode: " + stats.st_mode);
        OFSLOG.debug("intPermission: " + intPermission);
        OFSLOG.debug("octal perms: " + octal);
        permission = new FsPermission(octal);
        OFSLOG.debug(permission.toString());
        String[] ug = orange.stdio.getUsernameGroupname((int) stats.st_uid,
                (int) stats.st_gid);
        if (ug == null) {
            OFSLOG.debug("getUsernameGroupname returned null");
            throw new IOException();
        }
        /**/
        OFSLOG.debug("uid, username = <" + stats.st_uid + ", " + ug[0] + ">");
        OFSLOG.debug("gid, groupname = <" + stats.st_gid + ", " + ug[1] + ">");
        /**/
        fileStatus = new FileStatus(stats.st_size, isdir, block_replication,
                stats.st_blksize, stats.st_mtime * 1000, stats.st_atime * 1000,
                permission, ug[0], ug[1], makeAbsolute(f).makeQualified(this));
        return fileStatus;
    }

    @Override
    public Path getHomeDirectory() {
        return new Path("/user/" + System.getProperty("user.name"))
                .makeQualified(this);
    }

    /*
     * Return a Path as a String that OrangeFS can use. ie. removes URI scheme
     * and authority and prepends ofsMount
     */
    private String getOFSPathName(Path path) {
        String ret = ofsMount + makeAbsolute(path).toUri().getPath();
        OFSLOG.debug("ret = " + ret);
        return ret;
    }

    /*
     * Returns a Path array representing parent directories of a given a path
     */
    public Path[] getParentPaths(Path f) throws IOException {
        String[] split = null;
        Path[] ret = null;
        String currentPath = "";
        OFSLOG.debug("getParentPaths: f = " + makeAbsolute(f).toUri().getPath());
        split = makeAbsolute(f).toUri().getPath().split(Path.SEPARATOR);
        /*
         * split.length - 2 since we ignore the first and last element of
         * 'split'. the first element is empty and the last is the basename of
         * 'f' (not a parent).
         */
        if ((split.length - 2) <= 0) {
            return null;
        }
        ret = new Path[split.length - 2];
        /*
         * Start a i = 1, since first element of split == "" i < split.length -1
         * since we are only interested in parent paths.
         */
        for (int i = 1; i < split.length - 1; i++) {
            currentPath += Path.SEPARATOR + split[i];
            ret[i - 1] = new Path(currentPath);
            OFSLOG.debug("ret[" + (i - 1) + "]= " + ret[i - 1]);
        }
        return ret;
    }

    /* Returns a URI whose scheme and authority identify this FileSystem. */
    @Override
    public URI getUri() {
        return uri;
    }

    /* Get the current working directory for the given file system. */
    @Override
    public Path getWorkingDirectory() {
        return workingDirectory;
    }

    /*
     * Called after a new FileSystem instance is constructed. Params: uri - a
     * uri whose authority section names the host, port, etc. for this
     * FileSystem conf - the configuration
     */
    @Override
    public void initialize(URI uri, Configuration conf) throws IOException {
        if (uri == null) {
            throw new IOException("uri is null");
        }
        if (conf == null) {
            throw new IOException("conf is null");
        }
        if (this.initialized == true) {
            return;
        }
        if (uri.getHost() == null) {
            throw new IOException("Incomplete OrangeFS URI, no host: " + uri);
        }
        this.uri = URI.create(uri.getScheme() + "://" + uri.getAuthority());
        OFSLOG.debug("uri: " + this.uri.toString());
        OFSLOG.debug("conf: " + conf.toString());
        setConf(conf);
        /* Get OFS statistics */
        statistics = getStatistics(uri.getScheme(), getClass());
        OFSLOG.debug("OrangeFileSystem.statistics: "
                + this.statistics.toString());
        this.ofsMount = conf.get("fs.ofs.mntLocation", null);
        if (this.ofsMount == null || this.ofsMount.length() == 0) {
            throw new IOException(
                    "Missing fs.ofs.mntLocation. Check your configuration.");
        }
        this.localFS = FileSystem.getLocal(conf);
        workingDirectory = new Path("/user/" + System.getProperty("user.name"))
                .makeQualified(this);
        OFSLOG.debug("workingDirectory = " + workingDirectory.toString());
    }

    public boolean isDir(Path f) throws FileNotFoundException {
        Path fOFS = new Path(getOFSPathName(f));
        Stat stats = orange.posix.stat(fOFS.toString());
        if (stats == null) {
            OFSLOG.error(makeAbsolute(f) + " not found!");
            throw new FileNotFoundException();
        }
        return orange.posix.isDir(stats.st_mode) != 0 ? true : false;
    }

    /* List the statuses of the files/directories in the given path if the path
     * is a directory.
     */
    @Override
    public FileStatus[] listStatus(Path f) throws IOException {
        Path fOFS = new Path(getOFSPathName(f));
        OFSLOG.debug("Path f = " + makeAbsolute(f).toString());
        ArrayList<String> arrayList = orange.stdio.getEntriesInDir(fOFS.toString());
        if(arrayList == null) {
            return null;
        }
        Object [] fileNames = arrayList.toArray();
        String fAbs = makeAbsolute(f).toString() + "/";
        FileStatus[] statusArray = new FileStatus[fileNames.length];
        for (int i = 0; i < fileNames.length; i++) {
            try {
                statusArray[i] = getFileStatus(new Path(fAbs + fileNames[i].toString()));
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

    public Path makeAbsolute(Path path) {
        if (path.isAbsolute()) {
            return path;
        }
        return new Path(workingDirectory, path);
    }

    /* Make the given file and all non-existent parents into directories. */
    @Override
    public boolean mkdirs(Path f, FsPermission permission) throws IOException {
        int ret = 0;
        long mode = 0;
        Path[] parents = null;
        mode = permission.toShort();
        OFSLOG.debug("mkdirs attempting to create directory: "
                + makeAbsolute(f).toString());
        OFSLOG.debug("permission = " + permission);
        /* Check to see if the directory already exists. */
        if (exists(f)) {
            if (isDir(f)) {
                OFSLOG.warn("directory=" + makeAbsolute(f).toString()
                        + " already exists");
                setPermission(f, permission);
                return true;
            }
            else {
                OFSLOG.warn("path exists but is not a directory: "
                        + makeAbsolute(f));
                return false;
            }
        }
        /*
         * At this point, a directory should be created unless a parent already
         * exists as a file.
         */
        parents = getParentPaths(f);
        if (parents != null) {
            /* Attempt creation of parent directories */
            for (int i = 0; i < parents.length; i++) {
                if (exists(parents[i])) {
                    if (!isDir(parents[i])) {
                        OFSLOG.warn("parent path is not a directory: "
                                + parents[i]);
                        return false;
                    }
                }
                else {
                    /* Create the missing parent and setPermission. */
                    ret = orange.posix.mkdir(getOFSPathName(parents[i]), mode);
                    if (ret == 0) {
                        setPermission(parents[i], permission);
                    }
                    else {
                        OFSLOG.error("mkdir failed on parent directory = "
                                + parents[i] + ", permission = "
                                + permission.toString());
                        return false;
                    }
                }
            }
        }
        /* Now create the directory f */
        ret = orange.posix.mkdir(getOFSPathName(f), mode);
        if (ret == 0) {
            setPermission(f, permission);
            return true;
        }
        else {
            OFSLOG.error("mkdir failed on parent path f =" + makeAbsolute(f)
                    + ", permission = " + permission.toString());
            return false;
        }
    }

    /* Opens an FSDataInputStream at the indicated Path. */
    @Override
    public FSDataInputStream open(Path f, int bufferSize) throws IOException {
        Path fOFS = new Path(getOFSPathName(f));
        return new FSDataInputStream(new OrangeFileSystemFSInputStream(fOFS
                .toString(), bufferSize, statistics));
    }

    /* Renames Path src to Path dst. */
    @Override
    public boolean rename(Path src, Path dst) throws IOException {
        int ret = orange.posix.rename(getOFSPathName(src), getOFSPathName(dst));
        return ret == 0;
    }

    @Override
    public void setPermission(Path p, FsPermission permission)
            throws IOException {
        int mode = 0;
        Path fOFS = null;
        if (permission == null) {
            return;
        }
        fOFS = new Path(getOFSPathName(p));
        mode = permission.toShort();
        OFSLOG.debug("permission (symbolic) = " + permission.toString());
        if (orange.posix.chmod(fOFS.toString(), mode) < 0) {
            throw new IOException("Failed to set permissions on path = "
                    + makeAbsolute(p) + ", mode = " + mode);
        }
    }

    /* Set the current working directory for the given file system. */
    @Override
    public void setWorkingDirectory(Path new_dir) {
        workingDirectory = makeAbsolute(new_dir);
    }

    @Override
    public Path startLocalOutput(Path fsOutputFile, Path tmpLocalFile)
            throws IOException {
        return tmpLocalFile;
    }
}
