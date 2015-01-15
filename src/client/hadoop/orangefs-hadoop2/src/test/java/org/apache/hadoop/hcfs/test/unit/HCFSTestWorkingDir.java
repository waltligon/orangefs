package org.apache.hadoop.hcfs.test.unit;

import junit.framework.Assert;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.hcfs.test.connector.HcfsTestConnectorFactory;
import org.apache.hadoop.hcfs.test.connector.HcfsTestConnectorInterface;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.File;

import static org.apache.hadoop.fs.FileSystemTestHelper.getTestRootPath;

public class HCFSTestWorkingDir{

    Logger log = LoggerFactory.getLogger(HCFSTestWorkingDir.class);
    FileSystem fSys;

    @Before
    public void setUp() throws Exception {
        HcfsTestConnectorInterface connector=
                HcfsTestConnectorFactory.getHcfsTestConnector();
        fSys=connector.create();
    }

    /**
     * This test fails in hadoop 1.2.0, if we do not have
     * logic in the GlusterVolume to use "getInitialWorkingDirectory" 
     * as the starting working directory.  
     */
    @Test
    public void test() throws Exception {
        Path outpath = new Path("to_");
        if(fSys.exists(outpath)){
            fSys.delete(outpath,true);
        }
        File tmpfile = new File("/tmp/test_copyfromlocal");
        tmpfile.createNewFile();
        log.info(tmpfile.getAbsolutePath());
        Assert.assertTrue(tmpfile.exists());
        
        fSys.copyFromLocalFile(false,  false, 
                new Path(tmpfile.getPath()), 
                outpath);
     
        Assert.assertTrue(fSys.exists(outpath));
        fSys.delete(outpath,true);
    }
    
    @After
    public void tearDown() throws Exception {
        fSys.delete(getTestRootPath(fSys, "test"), true);
    }
}
