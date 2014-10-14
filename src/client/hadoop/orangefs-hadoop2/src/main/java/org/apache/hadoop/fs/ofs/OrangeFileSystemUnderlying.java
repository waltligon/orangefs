/*
 * (C) 2014 Clemson University.
 * 
 * See COPYING in top-level directory.
 */
package org.apache.hadoop.fs.ofs;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.*;
import org.apache.hadoop.fs.permission.FsPermission;
import org.apache.hadoop.util.Progressable;
import org.orangefs.usrint.*;
import org.orangefs.usrint.Stat;

import java.io.FileNotFoundException;
import java.io.IOException;
import java.net.URI;
import java.util.ArrayList;
import java.util.Arrays;

/*
 * An extension of the Hadoop FileSystem class utilizing OrangeFS as the file
 * system.
 */
public class OrangeFileSystemUnderlying extends FileSystem {
    private Orange orange;
    private int ofsBufferSize;
    private long ofsBlockSize;
    private OrangeFileSystemLayout ofsLayout;
    public PVFS2POSIXJNIFlags pf;
    public PVFS2STDIOJNIFlags sf;
    private URI uri;
    private String ofsMount;
    private Path workingDirectory;
    private FileSystem localFS;
    private static final Log OFSLOG = LogFactory
            .getLog(OrangeFileSystemUnderlying.class);
    private boolean initialized;

    public static final int DEFAULT_OFS_FILE_BUFFER_SIZE = 4 * 1024 * 1024;
    public static final long DEFAULT_OFS_FILE_BLOCK_SIZE = 64L * 1024 * 1024;
    public static final OrangeFileSystemLayout DEFAULT_OFS_FILE_LAYOUT =
            OrangeFileSystemLayout.PVFS_SYS_LAYOUT_RANDOM;

    /*
     * After OrangeFileSystem is constructed, called initialize to set fields.
     */
    public OrangeFileSystemUnderlying() {
        this.orange = Orange.getInstance();
        this.pf = orange.posix.f;
        this.sf = orange.stdio.f;
        this.initialized = false;
    }

    /*
     * After OrangeFileSystem is constructed, called initialize to set fields.
     */
    public OrangeFileSystemUnderlying(Configuration conf, URI mURI)
            throws IOException {
        this.orange = Orange.getInstance();
        this.pf = orange.posix.f;
        this.sf = orange.stdio.f;
        this.initialized = false;
        initialize(mURI, conf);
    }

    /* Append to an existing file (optional operation). */
    @Override
    public FSDataOutputStream append(Path f, int bufferSize,
            Progressable progress)
            throws IOException {
        Path fOFS = new Path(getOFSPathName(f));
        OFSLOG.debug("append parameters: {\n\tPath f= " + fOFS);
        OFSLOG.debug("override bufferSize: old = " + bufferSize + ", new = "
                + ofsBufferSize);
        OrangeFileSystemOutputStream ofsOutputStream =
                new OrangeFileSystemOutputStream(fOFS.toString(),
                        ofsBufferSize, (short) 0, 0L, true, ofsLayout);
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
            long blockSize, Progressable progress)
            throws IOException {
        Path fOFS;
        FSDataOutputStream fsdos;
        fOFS = new Path(getOFSPathName(f));
        /*
         * Force override blockSize and bufferSize for OrangeFS files at
         * creation.
         */
        OFSLOG.debug("override blockSize: old = " + blockSize + ", new = "
                + ofsBlockSize);
        OFSLOG.debug("override bufferSize: old = " + bufferSize + ", new = "
                + ofsBufferSize);
        OFSLOG.debug("create parameters: {\n\tPath f= " + f.toString()
                + "\n\tFsPermission permission= " + permission.toString()
                + "\n\tboolean overwrite= " + overwrite
                + "\n\tint ofsBufferSize= " + ofsBufferSize
                + "\n\tshort replication= " + replication
                + "\n\tlong ofsBlockSize= " + ofsBlockSize);

        fsdos =
                new FSDataOutputStream(new OrangeFileSystemOutputStream(
                        fOFS.toString(), ofsBufferSize, replication,
                        ofsBlockSize, false, ofsLayout), statistics);
        /* Set the desired permission. */
        setPermission(f, permission);
        return fsdos;
    }

    /* Delete a file/folder, potentially recursively */
    @Override
    public boolean delete(Path f, boolean recursive)
            throws IOException {
        statistics.incrementWriteOps(1);
        boolean ret;
        FileStatus status;
        Path fOFS = new Path(getOFSPathName(f));
        OFSLOG.debug("Path f = " + f);
        if (recursive) {
            OFSLOG.debug("Recursive Delete!");
        } else {
            OFSLOG.debug("Non-recursive Delete");
        }
        try {
            status = getFileStatus(f);
        } catch (FileNotFoundException e) {
            OFSLOG.debug(makeAbsolute(f) + " not found!");
            return false;
        } catch (IOException e) {
            OFSLOG.error("File:" + makeAbsolute(f));
            return false;
        }
        if (status.isDirectory()) {
            if (!recursive) {
                OFSLOG.debug("Couldn't delete Path f = " + f + " since it is "
                        + "a directory but recursive is false.");
                return false;
            }
            // Call recursive delete on path
            OFSLOG.debug("Path f =" + f
                    + " is a directory and recursive is true."
                    + " Recursively deleting directory.");
            ret = orange.stdio.recursiveDeleteDir(fOFS.toString()) == 0;
        } else {
            OFSLOG.debug("Path f =" + f
                    + " exists and is a regular file. unlinking.");
            ret = orange.posix.unlink(fOFS.toString()) == 0;
        }
        // Return false on failure.
        if (!ret) {
            OFSLOG.debug("remove failed: ret == false\n");
        }
        return ret;
    }

    /* Check if exists. */
    @Override
    public boolean exists(Path f)
            throws IOException {
        /* Stat file */
        try {
            @SuppressWarnings("unused")
            FileStatus status = getFileStatus(f);
        } catch (FileNotFoundException e) {
            OFSLOG.debug(makeAbsolute(f) + " not found!");
            return false;
        } catch (IOException e) {
            OFSLOG.error("File:" + makeAbsolute(f));
            return false;
        }
        return true;
    }

    /* Return a file status object that represents the path. */
    @Override
    public FileStatus getFileStatus(Path f)
            throws FileNotFoundException, IOException {
        Stat stats;
        FileStatus fileStatus;
        boolean isdir;
        int block_replication = 0; /* TODO: handle replication. */
        int intPermission;
        String octal;
        FsPermission permission;
        String username;
        String groupname;

        Path fOFS = new Path(getOFSPathName(f));
        OFSLOG.debug("f = " + makeAbsolute(f));
        statistics.incrementReadOps(1);
        stats = orange.posix.stat(fOFS.toString());
        if (stats == null) {
            OFSLOG.debug("stat(" + makeAbsolute(f) + ") returned null");
            throw new FileNotFoundException();
        }
        isdir = orange.posix.isDir(stats.st_mode) == 1;
        if (isdir == true) {
            OFSLOG.debug("file/directory=directory");
        } else {
            OFSLOG.debug("file/directory=file");
        }
        /* Get UGO permissions out of st_mode... */
        intPermission = stats.st_mode & 0777;
        octal = Integer.toOctalString(intPermission);
        OFSLOG.debug("stats.st_mode: " + stats.st_mode);
        OFSLOG.debug("intPermission: " + intPermission);
        OFSLOG.debug("octal perms: " + octal);
        permission = new FsPermission(octal);
        OFSLOG.debug(permission.toString());

        username = orange.stdio.getUsername((int) stats.st_uid);
        if (username == null) {
            throw new IOException("getUsername returned null");
        }
        groupname = orange.stdio.getGroupname((int) stats.st_gid);
        if (groupname == null) {
            throw new IOException("getGroupname returned null");
        }
        /**/
        OFSLOG.debug("uid, username = " + stats.st_uid + ", " + username);
        OFSLOG.debug("gid, groupname = " + stats.st_gid + ", " + groupname);
        /**/
        fileStatus =
                new FileStatus(stats.st_size, isdir, block_replication,
                        stats.st_blksize, stats.st_mtime * 1000,
                        stats.st_atime * 1000, permission, username, groupname,
                        makeAbsolute(f).makeQualified(this.uri,
                                this.workingDirectory));
        return fileStatus;
    }

    /* Return a file system status object that represents the path. */
    public FsStatus getFsStatus(Path f)
            throws FileNotFoundException, IOException {
        Statfs statfs = null;
        int fd =
                orange.posix
                        .open(getOFSPathName(f), orange.posix.f.O_RDONLY, 0);
        statfs = orange.posix.fstatfs(fd);

        if (statfs == null) {
            OFSLOG.debug("statfs(" + makeAbsolute(f) + ") returned null");
            throw new FileNotFoundException();
        }

        return new FsStatus(statfs.getCapacity(), statfs.getUsed(),
                statfs.getRemaining());
    }

    @Override
    public Path getHomeDirectory() {
        return new Path("/user/" + System.getProperty("user.name"))
                .makeQualified(this.uri, this.workingDirectory);
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
    private Path[] getParentPaths(Path f)
            throws IOException {
        String[] split;
        Path[] ret;
        String currentPath = "";
        OFSLOG.debug("getParentPaths:" + " f = "
                + makeAbsolute(f).toUri().getPath());
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
    public void initialize(URI uri, Configuration conf)
            throws IOException {
        if (this.initialized) {
            return;
        }
        if (uri == null) {
            throw new IOException("uri is null");
        }
        if (conf == null) {
            throw new IOException("conf is null");
        }
        if (uri.getAuthority() == null) {
            throw new IOException("Incomplete OrangeFS URI, no authority: "
                    + uri);
        }
        setConf(conf);

        ofsBufferSize =
                conf.getInt("fs.ofs.file.buffer.size",
                        DEFAULT_OFS_FILE_BUFFER_SIZE);

        ofsBlockSize =
                conf.getLong("fs.ofs.block.size", DEFAULT_OFS_FILE_BLOCK_SIZE);

        ofsLayout = conf.getEnum("fs.ofs.file.layout", DEFAULT_OFS_FILE_LAYOUT);

        int index;
        boolean uriAuthorityMatchFound = false;
        String uriAuthority = uri.getAuthority();
        String ofsSystems[] = conf.getStrings("fs.ofs.systems");
        String ofsMounts[] = conf.getStrings("fs.ofs.mntLocations");

        if (ofsSystems == null || ofsMounts == null) {
            throw new IOException("Configuration value fs.ofs.systems or"
                    + " fs.ofs.mntLocations is null."
                    + " These configuration values must be defined and"
                    + " have at least one entry each.");
        }

        OFSLOG.debug("Number of specified OrangeFS systems: "
                + ofsSystems.length);
        OFSLOG.debug("Number of specified" + " OrangeFS mounts: "
                + ofsMounts.length);

        if (ofsSystems.length < 1) {
            throw new IOException("Configuration value fs.ofs.systems must"
                    + "contain at least one entry!");
        }

        if (ofsMounts.length < 1) {
            throw new IOException("Configuration value fs.ofs.mntLocations"
                    + " must contain at least one entry!");
        }

        if (ofsSystems.length != ofsMounts.length) {
            throw new IOException("Configuration values fs.ofs.systems and"
                    + " fs.ofs.mntLocations must contain the same number of"
                    + " comma-separated elements.");
        }

        OFSLOG.debug("Determining file system associated with URI authority.");
        for (index = 0; index < ofsSystems.length; index++) {
            OFSLOG.debug("{ofsSystems[" + index + "], ofsMounts[" + index
                    + "]} = {" + ofsSystems[index] + ", " + ofsMounts[index]
                    + "}");
            if (uriAuthority.equals(ofsSystems[index])) {
                OFSLOG.debug("Match found. Continuing with fs initialization.");
                uriAuthorityMatchFound = true;
                break;
            }
        }

        if (uriAuthorityMatchFound) {
            this.ofsMount = ofsMounts[index];
            OFSLOG.debug("Matching uri authority found at index = " + index);
        } else {
            OFSLOG.error("No OrangeFS file system found matching the "
                    + "following authority: " + uriAuthority);
            throw new IOException("There was no matching authority found in"
                    + " fs.ofs.systems. Check your configuration. -->"
                    + uriAuthority + " = " + ofsSystems[index - 1]);
        }

        OFSLOG.debug("URI authority = " + uriAuthority);
        this.uri = URI.create(uri.getScheme() + "://" + uriAuthority);

        OFSLOG.debug("uri: " + this.uri.toString());
        OFSLOG.debug("conf: " + conf.toString());

        /* Get OFS statistics */
        statistics = getStatistics(uri.getScheme(), getClass());
        OFSLOG.debug("OrangeFileSystem.statistics: "
                + this.statistics.toString());

        this.localFS = FileSystem.getLocal(conf);
        workingDirectory =
                new Path("/user/" + System.getProperty("user.name"))
                        .makeQualified(this.uri, null);
        OFSLOG.debug("workingDirectory = " + workingDirectory.toString());
        super.initialize(uri, conf);
        this.initialized = true;
    }

    public boolean isDirectory(Path f)
            throws FileNotFoundException {
        statistics.incrementReadOps(1);
        Path fOFS = new Path(getOFSPathName(f));
        Stat stats = orange.posix.stat(fOFS.toString());
        if (stats == null) {
            OFSLOG.error(makeAbsolute(f) + " not found!");
            throw new FileNotFoundException();
        }
        return orange.posix.isDir(stats.st_mode) != 0;
    }

    public boolean isInitialized() {
        return this.initialized;
    }

    /*
     * List the statuses of the files/directories in the given path if the path
     * is a directory.
     */
    @Override
    public FileStatus[] listStatus(Path f)
            throws IOException {
        FileStatus fStatus[] = new FileStatus[1];
        Path fOFS = new Path(getOFSPathName(f));
        OFSLOG.debug("Path f = " + makeAbsolute(f).toString());
        /*
         * Hadoop r2.x.x throws FileNotFoundException when the path does not
         * exist.
         */
        try {
            fStatus[0] = getFileStatus(f);
        } catch (IOException e) {
            return null;
        }
        if (!fStatus[0].isDirectory()) {
            /* Not a directory */
            return fStatus;
        }
        ArrayList<String> arrayList =
                orange.stdio.getEntriesInDir(fOFS.toString());
        if (arrayList == null) {
            return null;
        }
        Object[] fileNames = arrayList.toArray();
        String fAbs = makeAbsolute(f).toString() + "/";
        FileStatus[] statusArray = new FileStatus[fileNames.length];
        for (int i = 0; i < fileNames.length; i++) {
            try {
                statusArray[i] =
                        getFileStatus(new Path(fAbs + fileNames[i].toString()));
            } catch (FileNotFoundException e) {
                // TODO
                return null;
            } catch (IOException e) {
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
    public boolean mkdirs(Path f, FsPermission permission)
            throws IOException {
        int ret;
        long mode;
        Path[] parents;
        mode = permission.toShort();
        OFSLOG.debug("mkdirs attempting to create directory: "
                + makeAbsolute(f).toString());
        OFSLOG.debug("permission = " + permission);
        OFSLOG.debug("process user.name=" + System.getProperty("user.name"));
        OFSLOG.debug("mode = " + mode);
        /* Check to see if the directory already exists. */
        if (exists(f)) {
            if (isDirectory(f)) {
                OFSLOG.warn("directory=" + makeAbsolute(f).toString()
                        + " already exists");
                setPermission(f, permission);
                return true;
            } else {
                OFSLOG.warn("path exists but is not a directory: "
                        + makeAbsolute(f));
                return false;
            }
        }
        /*
         * At this point, a directory should get created below unless a parent
         * already exists as a file.
         */
        parents = getParentPaths(f);

        OFSLOG.debug("Parent directories: " + Arrays.toString(parents));

        if (parents != null) {
            /* Attempt creation of parent directories */
            for (int i = 0; i < parents.length; i++) {
                if (exists(parents[i])) {
                    if (!isDirectory(parents[i])) {
                        OFSLOG.warn("parent path is not a directory: "
                                + parents[i]);
                        return false;
                    }
                } else {
                    /* Create the missing parent and setPermission. */
                    statistics.incrementWriteOps(1);
                    ret =
                            orange.posix.mkdirTolerateExisting(
                                    getOFSPathName(parents[i]), 0700);
                    if (ret == 0) {
                        setPermission(parents[i], permission);
                    } else {
                        OFSLOG.error("mkdirTolerateExisting failed on parent directory = "
                                + parents[i]
                                + ", permission = "
                                + permission.toString());
                        return false;
                    }
                }
            }
        }
        /* Now create the directory f */
        statistics.incrementWriteOps(1);
        ret = orange.posix.mkdirTolerateExisting(getOFSPathName(f), 0700);
        if (ret == 0) {
            setPermission(f, permission);
            return true;
        } else {
            OFSLOG.error("mkdirTolerateExisting failed on path f ="
                    + makeAbsolute(f) + ", permission = "
                    + permission.toString());
            return false;
        }
    }

    /* Opens an FSDataInputStream at the indicated Path. */
    @Override
    public FSDataInputStream open(Path f, int bufferSize)
            throws IOException {
        Path fOFS = new Path(getOFSPathName(f));
        OFSLOG.debug("open: path=" + f);
        OFSLOG.debug("override bufferSize: old = " + bufferSize + ", new = "
                + ofsBufferSize);
        return new FSDataInputStream(new OrangeFileSystemFSInputStream(
                fOFS.toString(), ofsBufferSize, statistics));
    }

    /* Renames Path src to Path dst. */
    @Override
    public boolean rename(Path src, Path dst)
            throws IOException {
        statistics.incrementWriteOps(1);
        int ret = orange.posix.rename(getOFSPathName(src), getOFSPathName(dst));
        return ret == 0;
    }

    @Override
    public void setPermission(Path p, FsPermission permission)
            throws IOException {
        int mode;
        Path fOFS;
        statistics.incrementWriteOps(1);
        if (permission == null) {
            return;
        }
        OFSLOG.debug("permission (symbolic) = " + permission.toString());
        fOFS = new Path(getOFSPathName(p));
        mode = permission.toShort();
        if ((mode & 01000) == 01000) {
            OFSLOG.warn("permission contains sticky bit, removing it...");
            mode = mode ^ 01000;
            OFSLOG.warn("new mode = " + mode);
            FsPermission newModeAsPermission = new FsPermission((short) mode);
            OFSLOG.warn("new mode (symbolic) = "
                    + newModeAsPermission.toString());
        }
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
