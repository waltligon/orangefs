package org.apache.hadoop.hcfs.test.unit;

import org.apache.hadoop.fs.FSDataOutputStream;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.fs.ofs.OrangeFileSystem;
import org.apache.hadoop.hcfs.test.connector.HcfsTestConnectorFactory;
import org.apache.hadoop.hcfs.test.connector.HcfsTestConnectorInterface;
import org.junit.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;

import static org.apache.hadoop.fs.FileSystemTestHelper.getTestRootPath;

/**
 *  A class for performance and IO related unit tests.
 * 
 * - Write buffering
 * - Read buffering
 * - Object caching / File lookup caching.
 * - Seeking
 */
public class HCFSPerformanceIOTests {
    
    static FileSystem fs ; 
    Logger log = LoggerFactory.getLogger(HCFSPerformanceIOTests.class);
    
    @BeforeClass
    public static void setup() throws Exception {
    	HcfsTestConnectorInterface connector = HcfsTestConnectorFactory.getHcfsTestConnector();
        fs= connector.create();
    }
    
    @AfterClass
    public static void after() throws IOException {
        fs.close();
    }

    public Path bufferoutpath(){
        return getTestRootPath(fs, "buffering_test"+HCFSPerformanceIOTests.class.getName());
    }

    @After
    public void tearDown() throws Exception {
  	  fs.delete(bufferoutpath(),true);
    }

    //String to append to file we are writing.
    static final String CONTENT="1234";

    /**
     * This is a complex test.  It documents the expected behaviour of the 
     * FileSystem buffering.  
     * 
     * It assumes that the configuration value of FS is == the {@link OrangeFileSystem} DEFAULT_OFS_FILE_BUFFER_SIZE.
     * Then, it starts writing to a stream.  
     */
    @Test
    public void testBufferSpill() throws Exception {
        
        /**
         * Sanity check: This test expects that an override is being performed, i.e., that
         * the buffering is going to be set to the optimal size, because the file system 
         * detected that the configured original buffer size was == to the "bad default" value which 
         * we have decide to override, for the sack of "reasonable defaults" out of the box.
         */
        Assert.assertEquals(
                OrangeFileSystem.DEFAULT_OFS_FILE_BUFFER_SIZE,
                fs.getConf().getInt("io.file.buffer.size",-1));
        
        FSDataOutputStream os = fs.create(bufferoutpath());
        
        int written=0;
        
        /**
         * Now, we assert that no data is spilled to disk until we reach the optimal size.
         */
        while(written < OrangeFileSystem.DEFAULT_OFS_FILE_BUFFER_SIZE){
            os.write(CONTENT.getBytes());
            written+=CONTENT.getBytes().length;
            Assert.assertTrue("asserting that file not written yet...",fs.getLength(bufferoutpath())==0);
        }
        os.flush();
        
        Assert.assertTrue("asserting that is now written... ",fs.getLength(bufferoutpath()) >= OrangeFileSystem.DEFAULT_OFS_FILE_BUFFER_SIZE);

        os.close();
    }
}
