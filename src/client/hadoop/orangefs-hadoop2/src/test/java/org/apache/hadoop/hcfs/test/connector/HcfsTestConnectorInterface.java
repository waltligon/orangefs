package org.apache.hadoop.hcfs.test.connector;

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FileSystem;

import java.io.IOException;


/* generic interface for creating HCFS file systems for testing purposes */

public interface HcfsTestConnectorInterface {

    /* return a fully configured instantiated file system for testing */
    public FileSystem create() throws IOException;

    /* returns a configuration file with properties for a given FS */
    public Configuration createConfiguration();

}
