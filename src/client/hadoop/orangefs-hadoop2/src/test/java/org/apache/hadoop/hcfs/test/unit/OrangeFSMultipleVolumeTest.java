package org.apache.hadoop.hcfs.test.unit;

import junit.framework.Assert;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.hcfs.test.connector.HcfsTestConnectorFactory;
import org.apache.hadoop.hcfs.test.connector.HcfsTestConnectorInterface;
import org.junit.BeforeClass;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;

public class OrangeFSMultipleVolumeTest {
   
    Logger log = LoggerFactory.getLogger(HCFSPerformanceIOTests.class);
    static Configuration  config = null;
    
    
    @BeforeClass
    public static void setup() throws Exception {
        HcfsTestConnectorInterface connector = HcfsTestConnectorFactory.getHcfsTestConnector();
        config = connector.createConfiguration();
    }
   
    public static FileSystem getFileSystem(Path p) throws IOException {
        return FileSystem.get(p.toUri(), config);
    }
    
    @Test
    public void testDefaultPath() throws IOException {
        Path p1 = new Path("ofs:///test1.txt");
        Path p2 = new Path("ofs://localhost-orangefs0:3334/test1.txt");
        Path p3 = new Path("ofs://localhost-orangefs1:3334/test1.txt");
        
        FileSystem fs1 = getFileSystem(p1);
        FileSystem fs2 = getFileSystem(p2);
        FileSystem fs3 = getFileSystem(p3);
        fs1.create(p1);
        
        /* the OrangeFS volume and default is based on configuration settings, but expected that default = gv0 */
        Assert.assertTrue(fs1.exists(p1));
        Assert.assertTrue(fs2.exists(p2));
        Assert.assertFalse(fs3.exists(p3));
        
        fs1.delete(p1);
    }

    
}
