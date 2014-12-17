package org.apache.hadoop.hcfs.test.unit;

import junit.framework.Assert;
import org.apache.hadoop.fs.FileAlreadyExistsException;
import org.apache.hadoop.fs.Path;
import org.apache.hadoop.hcfs.test.connector.HcfsTestConnectorFactory;
import org.apache.hadoop.hcfs.test.connector.HcfsTestConnectorInterface;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;

import java.io.FileNotFoundException;
import java.io.IOException;

import static org.apache.hadoop.fs.FileSystemTestHelper.*;

public class HcfsMainOperationsBaseTest extends org.apache.hadoop.fs.FSMainOperationsBaseTest {

  @Before
  public void setUp() throws Exception {
	HcfsTestConnectorInterface connector = HcfsTestConnectorFactory.getHcfsTestConnector();  
    fSys = connector.create();
    super.setUp();
  }

  @After
  public void tearDown() throws Exception {
	  fSys.delete(getTestRootPath(fSys, "test"),true);
  }
  
  @Test
  public void testMkdirsFailsForSubdirectoryOfExistingFile() throws Exception {
    Path testDir = getTestRootPath(fSys, "test/hadoop");
    Assert.assertFalse(exists(fSys, testDir));
    fSys.mkdirs(testDir);
    Assert.assertTrue(exists(fSys, testDir));
    
    createFile(getTestRootPath(fSys, "test/hadoop/file"));
    
    Path testSubDir = getTestRootPath(fSys, "test/hadoop/file/subdir");
    
    try{
    	Assert.assertFalse(fSys.mkdirs(testSubDir));
    }catch(FileAlreadyExistsException ex){
    	// catch exception as expected.
    }
    
    Assert.assertFalse(exists(fSys, testSubDir));
    
    Path testDeepSubDir = getTestRootPath(fSys, "test/hadoop/file/deep/sub/dir");
    Assert.assertFalse(exists(fSys, testSubDir));
    try{
    	Assert.assertFalse(fSys.mkdirs(testDeepSubDir));
    }catch(FileAlreadyExistsException ex){
    	// catch exception as expected.
    }
    Assert.assertFalse(exists(fSys, testDeepSubDir));
    
  }
  
  @Test
  public void testWDAbsolute() throws IOException {
    Path absoluteDir = getTestRootPath(fSys,
        "test/existingDir");
    fSys.mkdirs(absoluteDir);
    fSys.setWorkingDirectory(absoluteDir);
    Assert.assertEquals(absoluteDir, fSys.getWorkingDirectory());
  }
  
  @Test
  public void testGlobStatusThrowsExceptionForNonExistentFile() throws Exception {
    try {
      // This should throw a FileNotFoundException
      fSys.globStatus(getTestRootPath(fSys, "test/hadoopfsdf/?"));
      /* the API doesn't mention 'FileNotFoundException'.  Instead it says empty array or null for return
       */
      // Assert.fail("Should throw FileNotFoundException");
    } catch (FileNotFoundException fnfe) {
      // expected
    }
  }
  
  
  
  @Test
  public void testDeleteNonExistentFile() throws IOException {
    Path path = getTestRootPath(fSys, "test/hadoop/file");    
    Assert.assertFalse("Doesn't exist", exists(fSys, path));
    Assert.assertFalse("No deletion", fSys.delete(path, true));
  }
  
  protected Path getDefaultWorkingDirectory() throws IOException {
      return fSys.getWorkingDirectory();
  }


}
