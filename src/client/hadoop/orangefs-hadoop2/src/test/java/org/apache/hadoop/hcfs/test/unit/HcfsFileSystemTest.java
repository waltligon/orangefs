package org.apache.hadoop.hcfs.test.unit;

import org.apache.hadoop.fs.*;
import org.apache.hadoop.fs.permission.FsAction;
import org.apache.hadoop.fs.permission.FsPermission;
import org.apache.hadoop.hcfs.test.connector.HcfsTestConnectorFactory;
import org.apache.hadoop.hcfs.test.connector.HcfsTestConnectorInterface;
import org.junit.*;

import java.io.IOException;

import static org.apache.hadoop.fs.FileSystemTestHelper.getTestRootPath;
import static org.junit.Assert.*;

/**
 * Unit test for HCFS classes.
 * 
 */
public class HcfsFileSystemTest{
    
    static FileSystem fs ; 
    

    /**
     * See MAPREDUCE-5902 for context on why this test is critical
     * for ecosystem interoperability.
     */
    @Test
    public void testEncodedPaths() throws Exception {
    	//FileSystem fs2 = FileSystem.getLocal(new Configuration());
    	FileSystem fs2 = fs;
    	Path encodedFiles=new Path("/tmp/encodedTest"+ System.currentTimeMillis());
    	fs2.mkdirs(encodedFiles);
    	fs2.create(new Path(encodedFiles,"a"));
    	fs2.create(new Path(encodedFiles,"a%2"));
    	fs2.create(new Path(encodedFiles,"a%2a"));
    	fs2.create(new Path(encodedFiles,"a%3a"));
    	fs2.create(new Path(encodedFiles,"a%4a"));
    	Assert.assertEquals(5, fs2.listStatus(encodedFiles).length);
    	fs2.delete(encodedFiles);
    }

    @BeforeClass
    public static void setup() throws Exception {
    	HcfsTestConnectorInterface connector = HcfsTestConnectorFactory.getHcfsTestConnector();
        fs= connector.create();
    }

    @AfterClass
    public static void after() throws IOException {
        fs.close();
    }

    @After
    public void tearDown() throws Exception {
  	  fs.delete(getTestRootPath(fs, "test"),true);
    }

    @Test
    public void testTolerantMkdirs() throws Exception {
        Path longPath=new Path("a/b/c/d");
        assertFalse(fs.exists(longPath));
        fs.mkdirs(longPath);
        assertTrue(fs.exists(longPath));
        fs.mkdirs(new Path("a"));
        assertTrue(fs.exists(longPath));
        assertTrue(fs.exists(new Path("a")));
        fs.mkdirs(new Path("a/b"));
        assertTrue(fs.exists(longPath));
        assertTrue(fs.exists(new Path("a/b")));
        fs.mkdirs(new Path("a/b/c"));
        assertTrue(fs.exists(longPath));
        assertTrue(fs.exists(new Path("a/b/c")));

        /* delete the directories */

        fs.delete(new Path("a"), true);
        assertFalse(fs.exists(longPath));

    }

    /**
     * BZ908898 : Test that confirms that ownership is preserved in OrangeFS
     * FileStatus.
     */
    @Test
    public void testOwner() throws Exception {
        final String me= System.getProperties().getProperty("user.name");
        Path myFile=new Path("to_owned_by_me.txt");
        fs.create(myFile);
        Assert.assertEquals(fs.getFileStatus(myFile).getOwner(), me);
        fs.delete(myFile);
    }

    @Test
    public void testTextWriteAndRead() throws Exception {

        String testString="Is there anyone out there?";
        String readChars=null;

        FSDataOutputStream dfsOut=null;
        dfsOut=fs.create(new Path("test1.txt"));
        dfsOut.writeUTF(testString);
        dfsOut.close();

        FSDataInputStream dfsin=null;

        dfsin=fs.open(new Path("test1.txt"));
        readChars=dfsin.readUTF();
        dfsin.close();

        assertEquals(testString, readChars);

        fs.delete(new Path("test1.txt"), true);

        assertFalse(fs.exists(new Path("test1")));
    }

    @Test
    public void testPermissions() throws Exception {

        Path myFile=new Path("filePerm.txt");
        fs.create(myFile);
        short perm=0777;
        fs.setPermission(myFile, new FsPermission(perm));
        assertEquals(fs.getFileStatus(myFile).getPermission().toShort(), perm);

        perm=0700;
        fs.setPermission(myFile, new FsPermission(perm));
        assertEquals(fs.getFileStatus(myFile).getPermission().toShort(), perm);

        fs.delete(myFile);
        assertFalse(fs.exists(myFile));

        /* directory permissions */
        Path directory = new Path("aa/bb/cc");
        perm = 0700;
        fs.mkdirs(directory, new FsPermission(perm));
        assertEquals(fs.getFileStatus(directory).getPermission().toShort(), perm);
        fs.delete(new Path("aa"),true);
        assertFalse(fs.exists(directory));


        perm = 0777;
        fs.mkdirs(directory, new FsPermission(perm));
        assertEquals(fs.getFileStatus(directory).getPermission().toShort(), perm);
        fs.delete(new Path("aa"),true);
        assertFalse(fs.exists(directory));
    }

    @Test
    public void testZDirs() throws Exception {
        final Path subDir1=new Path("td_dir.1");
        final Path baseDir=new Path("td_testDirs1");
        final Path test1=new Path("td_test1");
        final Path test2=new Path("td_test/dir.2");

        assertFalse(fs.exists(baseDir));
        assertFalse(fs.isDirectory(baseDir));

        // make the dir
        fs.mkdirs(baseDir);

        assertTrue(fs.isDirectory(baseDir));
        // fs.setWorkingDirectory(baseDir);

        fs.mkdirs(subDir1);

        assertTrue(fs.isDirectory(subDir1));

        assertFalse(fs.exists(test1));

        assertFalse(fs.isDirectory(test2));

        fs.create(new Path(baseDir, "dummyfile"));
        FileStatus[] p=fs.listStatus(baseDir);
        assertEquals(p.length, 1);

        fs.delete(baseDir, true);
        assertFalse(fs.exists(baseDir));

        fs.delete(subDir1, true);
        assertFalse(fs.exists(subDir1));


        fs.delete(baseDir);
        fs.delete(test1);
        fs.delete(test2);
    }

    @Test
    public void testFiles() throws Exception {

        Path subDir1=new Path("tf_dir.1");
        Path baseDir=new Path("tf_testDirs1");
        Path file1=new Path("tf_dir.1/foo.1");
        Path file2=new Path("tf_dir.1/foo.2");

        fs.mkdirs(baseDir);
        assertTrue(fs.isDirectory(baseDir));
        // fs.setWorkingDirectory(baseDir);

        fs.mkdirs(subDir1);

        FSDataOutputStream s1=fs.create(file1, true, 4096, (short) 1, (long) 4096, null);
        FSDataOutputStream s2=fs.create(file2, true, 4096, (short) 1, (long) 4096, null);

        s1.close();
        s2.close();

        FileStatus[] p=fs.listStatus(subDir1);
        assertEquals(p.length, 2);

        fs.delete(file1, true);
        p=fs.listStatus(subDir1);
        assertEquals(p.length, 1);

        fs.delete(file2, true);
        p=fs.listStatus(subDir1);
        assertEquals(p.length, 0);

        fs.delete(baseDir, true);
        assertFalse(fs.exists(baseDir));

        fs.delete(subDir1);
        fs.delete(file1);
        fs.delete(file2);
    }

    public void testFileIO() throws Exception {

        Path subDir1=new Path("tfio_dir.1");
        Path file1=new Path("tfio_dir.1/foo.1");
        Path baseDir=new Path("tfio_testDirs1");

        fs.mkdirs(baseDir);
        assertTrue(fs.isDirectory(baseDir));
        // fs.setWorkingDirectory(baseDir);

        fs.mkdirs(subDir1);

        FSDataOutputStream s1=fs.create(file1, true, 4096, (short) 1, (long) 4096, null);

        int bufsz=4096;
        byte[] data=new byte[bufsz];

        for(int i=0;i<data.length;i++)
            data[i]=(byte) (i%16);

        // write 4 bytes and read them back; read API should return a byte per
        // call
        s1.write(32);
        s1.write(32);
        s1.write(32);
        s1.write(32);
        // write some data
        s1.write(data, 0, data.length);
        // flush out the changes
        s1.close();

        // Read the stuff back and verify it is correct
        FSDataInputStream s2=fs.open(file1, 4096);
        int v;

        v=s2.read();
        assertEquals(v, 32);
        v=s2.read();
        assertEquals(v, 32);
        v=s2.read();
        assertEquals(v, 32);
        v=s2.read();
        assertEquals(v, 32);

        assertEquals(s2.available(), data.length);

        byte[] buf=new byte[bufsz];
        s2.read(buf, 0, buf.length);
        for(int i=0;i<data.length;i++)
            assertEquals(data[i], buf[i]);

        assertEquals(s2.available(), 0);

        s2.close();

        fs.delete(file1, true);
        assertFalse(fs.exists(file1));
        fs.delete(subDir1, true);
        assertFalse(fs.exists(subDir1));
        fs.delete(baseDir, true);
        assertFalse(fs.exists(baseDir));

        fs.delete(subDir1);
        fs.delete(file1);
        fs.delete(baseDir);

    }

    // BZ908899
    @Test
    public void testPermissionsChanging() throws Exception {
        Path theFile=new Path("/mnt/ofs/changePerms/a");

        fs.create(theFile);

        FsPermission originalPermissions=this.fs.getFileStatus(theFile).getPermission();
        FsPermission changeTo=new FsPermission(FsAction.WRITE, FsAction.WRITE, FsAction.WRITE);
        this.fs.setPermission(theFile, changeTo);

        /**
         * Sanity check: Assert that the original permissions are different than
         * the ones we changed to.
         */
        Assert.assertNotSame(originalPermissions, changeTo);

        /**
         * Assert that we indeed changed the privileges to the exact expected
         * values.
         */
        Assert.assertTrue(this.fs.getFileStatus(theFile).getPermission().getGroupAction().equals(changeTo.getGroupAction()));
        Assert.assertTrue(this.fs.getFileStatus(theFile).getPermission().getUserAction().equals(changeTo.getUserAction()));
        Assert.assertTrue(this.fs.getFileStatus(theFile).getPermission().getOtherAction().equals(changeTo.getOtherAction()));
        fs.delete(new Path("mnt"),true);
        
    }
}
