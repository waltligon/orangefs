package org.apache.hadoop.hcfs.test.connector;


public class HcfsTestConnectorFactory {

    /* Loads an HCFS file system adapter via environment variable */
    public static HcfsTestConnectorInterface getHcfsTestConnector()
            throws RuntimeException {
        String testConnector = System.getProperty("HCFS_FILE_SYSTEM_CONNECTOR");
        if (testConnector == null || "".equals(testConnector)) {
            testConnector = HcfsTestConnector.class.getCanonicalName();
        }

        return getHcfsTestConnector(testConnector);
    }

    public static HcfsTestConnectorInterface getHcfsTestConnector(
            String hcfsName) throws RuntimeException {
        try {
            return (HcfsTestConnectorInterface) Class.forName(hcfsName)
                    .newInstance();
        } catch (Exception e) {
            throw new RuntimeException(
                    "Cannont instatiate HCFS File System from HCFS_FILE_SYSTEM env variable. Error:\n "
                            + e);
        }

    }

}
