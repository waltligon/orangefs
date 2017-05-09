/*
 * (C) 2017 Clemson University.
 *
 * See LICENSE in src/client/hadoop/orangefs-hadoop2 directory.
 */
package org.apache.hadoop.fs.ofs;

import java.io.IOException;
import java.net.URI;
import java.net.URISyntaxException;

import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.DelegateToFileSystem;

/**
 * The OrangeFs implementation of AbstractFileSystem.
 */
public class OrangeFs extends DelegateToFileSystem {
    OrangeFs(final URI theUri, final Configuration conf)
            throws IOException, URISyntaxException {
        super(theUri, new OrangeFileSystem(), conf, "ofs", false);
    }
}
