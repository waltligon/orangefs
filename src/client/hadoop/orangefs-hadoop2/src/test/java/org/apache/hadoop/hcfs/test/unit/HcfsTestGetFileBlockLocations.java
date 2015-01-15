package org.apache.hadoop.hcfs.test.unit;

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.TestGetFileBlockLocations;
import org.apache.hadoop.hcfs.test.connector.HcfsTestConnectorFactory;
import org.apache.hadoop.hcfs.test.connector.HcfsTestConnectorInterface;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

public class HcfsTestGetFileBlockLocations extends TestGetFileBlockLocations{
	
	private static String TEST_ROOT_DIR = "ofs://localhost:3334/testFileBlockLocations";
	private static final int FileLength = 4 * 1024 * 1024; // 4MB
	
    @Override
    protected void setUp() throws IOException {
        try{
        	HcfsTestConnectorInterface connector = HcfsTestConnectorFactory.getHcfsTestConnector();
            Configuration cfg = connector.createConfiguration();
            cfg.writeXml(new FileOutputStream(new File("/tmp/core-site.xml")));
            Configuration.addDefaultResource("/tmp/core-site.xml");
            cfg.writeXml(System.out);
        }
        catch(Exception e)
        {
            throw new IOException(e);
        }
        super.setUp();
    }
}