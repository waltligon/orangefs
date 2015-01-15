package org.apache.hadoop.fs.ofs;

import static org.junit.Assert.*;

import java.io.IOException;
import java.net.URI;

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.fs.permission.FsPermission;
import org.junit.After;
import org.junit.AfterClass;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Test;

public class OrangeFileSystemTest {

    private OrangeFileSystem ofs;

    @BeforeClass
    public static void setUpBeforeClass()
            throws Exception {}

    @AfterClass
    public static void tearDownAfterClass()
            throws Exception {}

    @Before
    public void setUp()
            throws Exception {
        Configuration conf = new Configuration();
        URI uri = URI.create("ofs://localhost-orangefs:3334");
        conf.set("fs.defaultFS", "ofs://localhost-orangefs:3334");
        conf.set("fs.ofs.systems", "localhost-orangefs:3334");
        conf.set("fs.ofs.mntLocations", "/mnt/orangefs");
        ofs = new OrangeFileSystem();
        try {
            ofs.initialize(uri, conf);
        } catch (IOException e) {
            e.printStackTrace();
            fail("initialize threw an IOException!");
        }

        // create a directory and populate with files
        ofs.mkdirs(new Path("/test"), FsPermission.getDirDefault());
        ofs.create(new Path("/test/testfile1"), FsPermission.getFileDefault(),
                false, 4 * 1024 * 1024, (short) 1, 128 * 1024 * 1024, null);
        ofs.create(new Path("/test/testfile2"), FsPermission.getFileDefault(),
                false, 4 * 1024 * 1024, (short) 1, 128 * 1024 * 1024, null);
    }

    @After
    public void tearDown()
            throws Exception {
        ofs.delete(new Path("/test"), true);
    }

    @Test
    public void testOrangeFileSystem() {
        assertNotNull(ofs);
    }

    @Test
    public void testAppendPathIntProgressable() {
        fail("Not yet implemented");
    }

    @Test
    public void testCompleteLocalOutputPathPath() {
        fail("Not yet implemented");
    }

    @Test
    public void testCopyFromLocalFileBooleanPathPath() {
        fail("Not yet implemented");
    }

    @Test
    public void testCopyToLocalFileBooleanPathPath() {
        fail("Not yet implemented");
    }

    @Test
    public void testCreatePathFsPermissionBooleanIntShortLongProgressable() {
        fail("Not yet implemented");
    }

    @Test
    public void testDeletePathBoolean() {
        Path path = new Path("/test");
        boolean recursive = false;
        try {
            ofs.delete(path, recursive);
        } catch (IOException e) {
            fail("ofs.delete(" + path + ", " + recursive
                    + "); threw IOException: " + e);
        }
    }

    @Test
    public void testExistsPath() {
        /*
        Path path1 = new Path("/test");
        Path path2 = new Path("/test/testfile1");
        Path path3 = new Path("/missing/path");
        try {
            assertTrue(ofs.exists(path1));
        } catch (IllegalArgumentException e) {
            fail("ofs.exists(" + path1 + "); threw IllegalArgumentException: "
                    + e);
        } catch (IOException e) {
            fail("ofs.exists(" + path1 + "); threw IOException: " + e);
        }
        try {
            assertTrue(ofs.exists(path2));
        } catch (IllegalArgumentException e) {
            fail("ofs.exists(" + path2 + "); threw IllegalArgumentException: "
                    + e);
        } catch (IOException e) {
            fail("ofs.exists(" + path2 + "); threw IOException: " + e);
        }
        try {
            assertFalse(ofs.exists(path3));
        } catch (IllegalArgumentException e) {
            fail("ofs.exists(" + path3 + "); threw IllegalArgumentException: "
                    + e);
        } catch (IOException e) {
            fail("ofs.exists(" + path3 + "); threw IOException: " + e);
        }
        */
    }

    @Test
    public void testGetFileStatusPath() {
        fail("Not yet implemented");
    }

    @Test
    public void testGetHomeDirectory() {
        System.out.println("home directory: "
                + ofs.getHomeDirectory().toString());
    }

    @Test
    public void testGetParentPaths() {
        fail("Not yet implemented");
    }

    @Test
    public void testGetUri() {
        fail("Not yet implemented");
    }

    @Test
    public void testGetWorkingDirectory() {
        fail("Not yet implemented");
    }

    @Test
    public void testInitializeURIConfiguration() {
        assertTrue(ofs.isInitialized());
    }

    @Test
    public void testIsDir() {
        fail("Not yet implemented");
    }

    @Test
    public void testListStatusPath() {
        fail("Not yet implemented");
    }

    @Test
    public void testMakeAbsolute() {
        fail("Not yet implemented");
    }

    @Test
    public void testMkdirsPathFsPermission() {
        fail("Not yet implemented");
    }

    @Test
    public void testOpenPathInt() {
        fail("Not yet implemented");
    }

    @Test
    public void testRenamePathPath() {
        fail("Not yet implemented");
    }

    @Test
    public void testSetPermissionPathFsPermission() {
        fail("Not yet implemented");
    }

    @Test
    public void testSetWorkingDirectoryPath() {
        fail("Not yet implemented");
    }

    @Test
    public void testStartLocalOutputPathPath() {
        fail("Not yet implemented");
    }

}
