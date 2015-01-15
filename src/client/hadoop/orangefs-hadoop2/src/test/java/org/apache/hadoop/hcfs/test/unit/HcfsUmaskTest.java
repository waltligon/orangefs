package org.apache.hadoop.hcfs.test.unit;

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FileStatus;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.hcfs.test.connector.HcfsTestConnectorFactory;
import org.apache.hadoop.hcfs.test.connector.HcfsTestConnectorInterface;
import org.junit.After;
import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Test;

import java.io.IOException;

import static org.apache.hadoop.fs.FileSystemTestHelper.getTestRootPath;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

/**
 * Unit test for HCFS classes.
 * 
 */
public class HcfsUmaskTest{
    
    static FileSystem fs ; 
    
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
    public void testMkdirsWithUmask() throws Exception {
        Configuration conf = fs.getConf();
        String oldUmask = conf.get("fs.permissions.umask-mode");
        Path dir = new Path("dirUmask022");
        conf.set("fs.permissions.umask-mode", "022");
        assertTrue(fs.mkdirs(dir));
        conf.set("fs.permissions.umask-mode", oldUmask);
        FileStatus status = fs.getFileStatus(dir);
        assertTrue(status.isDirectory());
        assertEquals((short)0755, status.getPermission().toShort());

    } 
}
