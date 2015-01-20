package org.apache.hadoop.hcfs.test.connector;

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FileSystem;

import java.io.IOException;
import java.io.InputStream;


/*
 * Generic HCFS file system test connector. This test connector takes a fully qualified
 * o.a.h.f.FileSystem implementor class as an environment variable.
 */
public class HcfsTestConnector implements HcfsTestConnectorInterface {

    public Configuration createConfiguration() {
        Configuration c = new Configuration();
        InputStream config =
                HcfsTestConnector.class.getClassLoader().getResourceAsStream(
                        "core-site.xml");
        c.addResource(config);

        return c;
    }

    public FileSystem create() throws IOException {
        return FileSystem.get(createConfiguration());
    }
}
