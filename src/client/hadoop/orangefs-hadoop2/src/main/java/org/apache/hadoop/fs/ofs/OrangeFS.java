package org.orangefs.hadoop.fs.ofs;

import java.io.FileNotFoundException;
import java.io.IOException;
import java.net.URI;
import java.net.URISyntaxException;
import java.util.ArrayList;
import java.util.EnumSet;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.apache.hadoop.HadoopIllegalArgumentException;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.AbstractFileSystem;
import org.apache.hadoop.fs.BlockLocation;
import org.apache.hadoop.fs.CreateFlag;
import org.apache.hadoop.fs.FSDataInputStream;
import org.apache.hadoop.fs.FSDataOutputStream;
import org.apache.hadoop.fs.FileAlreadyExistsException;
import org.apache.hadoop.fs.FileChecksum;
import org.apache.hadoop.fs.FileStatus;
import org.apache.hadoop.fs.FsServerDefaults;
import org.apache.hadoop.fs.FsStatus;
import org.apache.hadoop.fs.ParentNotDirectoryException;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.fs.UnresolvedLinkException;
import org.apache.hadoop.fs.UnsupportedFileSystemException;
import org.apache.hadoop.fs.permission.FsPermission;
import org.apache.hadoop.security.AccessControlException;
import org.apache.hadoop.util.Progressable;
import org.orangefs.usrint.Orange;
import org.orangefs.usrint.OrangeFileSystemOutputStream;
import org.orangefs.usrint.PVFS2POSIXJNIFlags;
import org.orangefs.usrint.PVFS2STDIOJNIFlags;
import org.orangefs.usrint.Stat;

public class OrangeFS extends AbstractFileSystem {
	
    private Orange orange;
    public PVFS2POSIXJNIFlags pf;
    public PVFS2STDIOJNIFlags sf;
    private String ofsMount;
    private Path workingDirectory;
    private static final Log OFSLOG = LogFactory.getLog(OrangeFS.class);
    
    /**
     * This constructor has the signature needed by
     * {@link AbstractFileSystem#createFileSystem(URI, Configuration)}
     * 
     * @param theUri
     *          which must be that of OrangeFS (ofs)
     * @param conf
     * @throws IOException
     */
    public OrangeFS(final URI uri, final Configuration conf)
    		throws IOException, URISyntaxException {
    	/* TODO authorityNeeded? */
    	super(URI.create(uri.getScheme()), uri.getScheme(), false, uri.getPort());
    	
    	OFSLOG.info("uri components:\n" +
    			"\turi = " + uri.toString() +
    			"\n\turi.getScheme() = " + uri.getScheme() +
    			"\n\turi.getAuthority = " + uri.getAuthority() +
    			"\n\turi.getHost = " + uri.getHost() +
    			"\n\turi.getPort = " + uri.getPort() +
    			"\n\turi.getPath = " + uri.getPath());
    	
    	OFSLOG.info("fs.ofs.mntLocation = " + conf.get("fs.ofs.mntLocation",
    			null));
    	
    	OFSLOG.info("conf:\n\t" + conf.toString());
    	
    	OFSLOG.info("args to superclass of OrangeFS:\n" 
    			+ "\turi = " + uri.toString() +
    			"\n\tsupportedScheme = " + uri.getScheme() +
    			"\n\tauthorityNeeded = " + "true" + 
    			"\n\tdefaultPort = " + uri.getPort());

    	orange = Orange.getInstance();
		pf = orange.posix.f;
        sf = orange.stdio.f;
        workingDirectory = new Path("/user/" + System.getProperty("user.name")); 
        OFSLOG.debug("workingDirectory = " + workingDirectory.toString());

        /* TODO: add capability to parse multiple OrangeFS mounts, to enable
         * the use of multiple OrangeFS file systems with Hadoop.
         * 
         * potentially use Configuration.iterator()
         * 
         * This would follow the registry-based model of hierarchical URIs.
         * The authority could be used as a key to the associated OrangeFS
         * mount prefix. When a path is supplied to any OrangeFS native method
         * it should be an OrangeFS path (ie. matching the pvfs2tab entry. */
        this.ofsMount = conf.get("fs.ofs.mntLocation", null);
        if (this.ofsMount == null || this.ofsMount.length() == 0) {
            throw new IOException(
                    "Missing fs.ofs.mntLocation. Check your configuration.");
        }
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

	@Override
	public FSDataOutputStream createInternal(
			Path f,
			EnumSet<CreateFlag> flag,
			FsPermission absolutePermission,
			int bufferSize,
			short replication,
			long blockSize,
			Progressable progress,
			org.apache.hadoop.fs.Options.ChecksumOpt checksumOpt,
			boolean createParent) throws AccessControlException,
			                             FileAlreadyExistsException,
			                             FileNotFoundException,
			                             ParentNotDirectoryException,
			                             UnsupportedFileSystemException,
			                             UnresolvedLinkException,
			                             IOException {
        Path fOFS = null;
        Path fParent = null;
        FSDataOutputStream fsdos = null;
        Boolean exists = null;
       
        fOFS = new Path(getOFSPathName(f));
        OFSLOG.debug("create parameters: {\n\tPath f= " + f.toString()
        		+ "\n\tEnumSet<CreateFlag> flag = " + flag.toString()
                + "\n\tFsPermission permission= " + absolutePermission.toString()
                + "\n\tint bufferSize= " + bufferSize
                + "\n\tshort replication= " + replication
                + "\n\tlong blockSize= " + blockSize
                + "\n\tProgressable progress= " + progress.toString()
                + "\n\torg.apache.hadoop.fs.Options.ChecksumOpt checksumOpt= "
                + checksumOpt
                + "\n\tboolean createParent= " + createParent);

		CreateFlag.validate(flag);
        
        /* Does this path exist? */
        if (exists(f)) {
        	exists = true;
        }
        
        CreateFlag.validate(f, exists,flag);
        
        if (exists) {
            /* Delete existing file */
            if (flag.contains(CreateFlag.OVERWRITE)) {
                delete(f, false);
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
                mkdir(fParent, absolutePermission, true);
            }
        }
        fsdos = new FSDataOutputStream(new OrangeFileSystemOutputStream(fOFS
                .toString(), bufferSize, replication, false), statistics);
        /* Set the desired permission. */
        setPermission(f, absolutePermission);
        return fsdos;		
	}

	@Override
	public boolean delete(Path f, boolean recursive)
			throws AccessControlException, FileNotFoundException,
			UnresolvedLinkException, IOException {
        boolean ret = false;
        Path fOFS = new Path(getOFSPathName(f));
        OFSLOG.debug("Path f = " + f);
        OFSLOG.debug((recursive) ? "Recursive Delete!" : "Non-recursive Delete");
        if(isDir(f)) {
            if(!recursive) {
                OFSLOG.debug("Couldn't delete Path f = " + f + " since it is "
                        + "a directory but recursive is false.");
                return false;
            }
            // Call recursive delete on path
            OFSLOG.debug("Path f =" + f
                    + " is a directory and recursive is true."
                    + " Recursively deleting directory.");
            //TODO log error also instead of simply failing here
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

	@Override
	public BlockLocation[] getFileBlockLocations(Path arg0, long arg1, long arg2)
			throws AccessControlException, FileNotFoundException,
			UnresolvedLinkException, IOException {
		//TODO: expose this data somehow. is returning null here safe?
		return null;
	}

	@Override
	public FileChecksum getFileChecksum(Path arg0)
			throws AccessControlException, FileNotFoundException,
			UnresolvedLinkException, IOException {
		//TODO: Potentially implement this in future.
		return null;
	}

	@Override
	public FileStatus getFileStatus(Path f) throws AccessControlException,
			FileNotFoundException, UnresolvedLinkException, IOException {
        Stat stats = null;
        FileStatus fileStatus = null;
        boolean isdir = false;
        int block_replication = 0; /* TODO: handle replication. */
        int intPermission = 0;
        String octal = null;
        FsPermission permission = null;
        String username = null;
        String groupname = null;
        
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
        
        username =  orange.stdio.getUsername((int) stats.st_uid);
        if (username == null) {
            throw new IOException("getUsername returned null");
        }
        groupname = orange.stdio.getGroupname((int) stats.st_gid);
        if (groupname == null) {
            throw new IOException("getGroupname returned null");       	
        }
        /**/
        OFSLOG.debug("uid, username = <" + stats.st_uid + ", " + username + ">");
        OFSLOG.debug("gid, groupname = <" + stats.st_gid + ", " + groupname + ">");
        /**/
        OFSLOG.debug("f = " + f.toString());
        OFSLOG.debug("makeQualified(f) = " + makeQualified(f));
        OFSLOG.debug("potential fix for failed statement: " + makeQualified(makeAbsolute(f)));
        fileStatus = new FileStatus(stats.st_size, isdir, block_replication,
                stats.st_blksize, stats.st_mtime * 1000, stats.st_atime * 1000,
                permission, username, groupname, 
                makeQualified(f));
        return fileStatus;
	}

	@Override
	public FsStatus getFsStatus() throws AccessControlException,
			FileNotFoundException, IOException {
		/* TODO Returning null for now since I'm not sure how we would return
		 * this info since OrangeFS doesn't keep track of file sizes.
		 * For performance reasons a file's size is something that's
		 * dynamically calculated when needed.
		 */
		return null;
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

	@Override
	public FsServerDefaults getServerDefaults() throws IOException {
		/* TODO Is it okay to return null here? Many of these features aren't
		 * supported by OrangeFS. */
		return null;
	}

	@Override
	public int getUriDefaultPort() {
		// TODO Auto-generated method stub
		return 3334;
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

	@Override
	public FileStatus[] listStatus(Path f) throws AccessControlException,
			FileNotFoundException, UnresolvedLinkException, IOException {
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
	
	/* Should be no need for this since Path makeQualified(uri, workingDirectory) */
    public Path makeAbsolute(Path path) {
        if (path.isAbsolute()) {
            return path;
        }
        return new Path(workingDirectory, path);
    }

	@Override
	public void mkdir(Path dir,
            FsPermission permission,
            boolean createParent) throws AccessControlException,
                                         FileAlreadyExistsException,
			                             FileNotFoundException,
			                             UnresolvedLinkException,
			                             IOException {
		OFSLOG.debug("mkdir: dir = " + dir.toString());
        int ret = 0;
        long mode = 0;
        Path[] parents = null;
        mode = permission.toShort();
        OFSLOG.debug("mkdirs attempting to create directory: "
                + makeAbsolute(dir).toString());
        OFSLOG.debug("permission = " + permission);
        /* Check to see if the directory already exists. */
        if (exists(dir)) {
            if (isDir(dir)) {
                OFSLOG.warn("directory=" + makeAbsolute(dir).toString()
                        + " already exists");
                //setPermission(dir, permission); //TODO
                throw new FileAlreadyExistsException(
                		"dir already exists: " + makeAbsolute(dir).toString());
            }
            else {
                OFSLOG.warn("path exists but is not a directory: "
                        + makeAbsolute(dir));
                /* TODO consider throwing different exception! */
                throw new FileAlreadyExistsException(
                		"path exists but is not a directory: " +
            			makeAbsolute(dir).toString());
            }
        }
        parents = getParentPaths(dir);
        if (parents != null) {
            /* Attempt creation of parent directories */
            for (int i = 0; i < parents.length; i++) {
                if (exists(parents[i])) {
                    if (!isDir(parents[i])) {
                        OFSLOG.warn("parent path is not a directory: "
                                + parents[i]);
                        throw new ParentNotDirectoryException(
                        		"Parent path exists already as a file! "
                        		+ "Parent directory cannot be created: "
                                + parents[i]);
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
                        /* TODO Need to distinguish between different types of
                         * errors in order to throw the proper exception. */
                        throw new org.apache.hadoop.security.AccessControlException(
                    			"mkdir failed (ret = " + ret 
                    			+ ") on parent directory = "
            					+ parents[i] + ", permission = "
                                + permission.toString());
                    }
                }
            }
        }
        /* Now create the directory f */
        ret = orange.posix.mkdir(getOFSPathName(dir), mode);
        if (ret == 0) {
            setPermission(dir, permission);
        }
        else {
        	/*
            OFSLOG.error("mkdir failed on parent path f =" + makeAbsolute(dir)
                    + ", permission = " + permission.toString());
            */
        	
            /* TODO Need to distinguish between different types of
             * errors in order to throw the proper exception. */
            throw new IOException(
        			"mkdir failed (ret = " + ret 
        			+ ") on target directory = "
					+ dir + ", permission = "
                    + permission.toString());
        }		
	}

	@Override
	public FSDataInputStream open(Path f, int bufferSize)
			throws AccessControlException, FileNotFoundException,
			UnresolvedLinkException, IOException {
        Path fOFS = new Path(getOFSPathName(f));
        return new FSDataInputStream(new OrangeFileSystemFSInputStream(fOFS
                .toString(), bufferSize, statistics));
	}

	@Override
	public void renameInternal(Path src, Path dst)
			throws AccessControlException, FileAlreadyExistsException,
			FileNotFoundException, ParentNotDirectoryException,
			UnresolvedLinkException, IOException {
		if(exists(dst)) {
			throw new FileAlreadyExistsException("rename dst target already exists! "
					+ "dst = " + getOFSPathName(dst));
		}
        int ret = orange.posix.rename(getOFSPathName(src), getOFSPathName(dst));
        /* TODO do a better job relaying OrangeFS error code so we can
         * throw the proper exception here. */
        if(ret != 0) {
        	throw new IOException("Rename operation {" + getOFSPathName(src)
        			+ ", " + getOFSPathName(dst)
        			+ "} failed with ret = " + ret);
        }
	}

	@Override
	public void setOwner(Path f, String username, String groupname)
			throws AccessControlException, FileNotFoundException,
			UnresolvedLinkException, IOException {
		int ret = -1;
		int uid = -1;
		int gid = -1;
		if (username == null && groupname == null) {
			throw new HadoopIllegalArgumentException(
					"method parameters username and groupname cannot both be"
					+ " null");
		}
		OFSLOG.debug("username, groupname = "
				+ username + ", " + groupname);
		if (username != null) {
			uid = orange.stdio.getUid(username);
			if (uid == -1) {
				throw new HadoopIllegalArgumentException(
						"invalid username: "
						+ "uid = " + uid);
			}
		}
		if (groupname != null) {
			gid = orange.stdio.getGid(groupname);
			if (gid == -1) {
				throw new HadoopIllegalArgumentException(
						"invalid groupname: "
						+ "gid = " + gid);
			}
		}
		/* chown ignores uid or gid == -1, to support setting either uid,
		 * gid, or both. */
		ret = orange.posix.chown(getOFSPathName(f).toString(), uid, gid);
		if (ret != 0) {
			/* TODO Do a better job indicating what the true OrangeFS error
			 * was. This one is probably good enough to test with. */
			throw new org.apache.hadoop.security.AccessControlException(
					"chown failed");
		}
	}

	@Override
	public void setPermission(Path f, FsPermission permission)
			throws AccessControlException, FileNotFoundException,
			UnresolvedLinkException, IOException {
        int mode = 0;
        Path fOFS = null;
        if (permission == null) {
        	throw new org.apache.hadoop.security.AccessControlException(
        			"FsPermission permission is null");
        }
        fOFS = new Path(getOFSPathName(f));
        mode = permission.toShort();
        OFSLOG.debug("permission (symbolic) = " + permission.toString());
        if (orange.posix.chmod(fOFS.toString(), mode) < 0) {
        	/* TODO Determine the OrangeFS error code so that the appropriate
        	 * exception may be thrown. */
            throw new IOException("Failed to set permissions on path = "
                    + makeAbsolute(f) + ", mode = " + mode);
        }
	}

	@Override
	public boolean setReplication(Path f, short replication)
			throws AccessControlException, FileNotFoundException,
			UnresolvedLinkException, IOException {
		/* OrangeFS doesn't currently support replication. Coming soon... */
		OFSLOG.warn("OrangeFS doesn't currently support replication.\n"
				+ "setReplication called with args: Path = " + f
				+ ", replication = " + replication);
		return false;
	}

	@Override
	public void setTimes(Path f, long mtime, long atime)
			throws AccessControlException, FileNotFoundException,
			UnresolvedLinkException, IOException {
		int ret = -1;
		long existing_mtime = -1;
		long existing_atime = -1;
		long new_mtime = -1;
		long new_atime = -1;
		
        Stat stats = null;
        Path fOFS = new Path(getOFSPathName(f));
        OFSLOG.debug("f = " + makeAbsolute(f));
        stats = orange.posix.stat(fOFS.toString());
        if (stats == null) {
            OFSLOG.debug("stat(" + makeAbsolute(f) + ") returned null");
            throw new FileNotFoundException();
        }
        /* Multiply by 1000 to get milliseconds from seconds */
        existing_mtime = stats.st_mtime * 1000;
        existing_atime = stats.st_atime * 1000;
        OFSLOG.debug("existing_mtime = " + existing_mtime);
        OFSLOG.debug("existing_atime = " + existing_atime);
		OFSLOG.debug("args: mtime, atime = "
				+ mtime + ", " + atime);
		if (mtime == -1 && atime == -1) {
			throw new HadoopIllegalArgumentException(
					"method parameters mtime and atime cannot both be"
					+ " -1");
		}
		if (mtime != -1) {
			new_mtime = mtime; // + maths
		}
		else {
			new_mtime = existing_mtime;
		}
		if (atime != -1) {
			new_atime = atime;
		}
		else {
			new_atime = existing_atime;
		}
		/* Update file mtime and/or atime using utimes */
		ret = orange.posix.utimes(fOFS.toString(), new_atime, new_mtime);
		if (ret == -1) {
			/* TODO provide appropriate exception based on OrangeFS error */
			throw new IOException("utimes failed given args: Path = " + 
					fOFS.toString() + ", mtime = " + new_mtime + 
					", atime = " + new_atime);
		}
		return;
	}

	@Override
	public void setVerifyChecksum(boolean arg0) throws AccessControlException,
			IOException {
		/* TODO Consider supporting checksum */
		OFSLOG.warn("Checksum is not currently supported by OrangeFS.");
		return;
	}

}
