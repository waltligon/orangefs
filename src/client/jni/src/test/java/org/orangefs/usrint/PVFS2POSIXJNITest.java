/*
 * (C) 2014 Clemson University
 *
 * See COPYING in top-level directory.
 */
package org.orangefs.usrint;

import org.junit.*;

import java.io.IOException;
import java.nio.ByteBuffer;

import static org.junit.Assert.*;

public class PVFS2POSIXJNITest 
{
	//file descriptors 	
	public static int fd_creat;
	public static int fd_creat2;
	public static int fd_dup;
	public static int fd_dup2;
	//open file descriptor
	public static int fd_open;
	//open directory descriptor
    public static int dir_open;
	
	//PVFS2POSIXJNI object under test 
	public PVFS2POSIXJNI posix;
	
	//Default Constructor, Always create Pvfs2posixjni Object
	public PVFS2POSIXJNITest()
	{
		posix = new PVFS2POSIXJNI();
	}
	
	@BeforeClass
	public static void setUpBeforeClass() throws Exception 
	{
        System.out.println("SET UP BEFORE CLASS");
	}

	@AfterClass
	public static void tearDownAfterClass() throws Exception 
	{
		System.out.println("TEAR DOWN AFTER CLASS");
	}

	@Before
	public void setUp() throws Exception 
	{   
		// MAKE DIRECTORY
		int ret = posix.mkdir("/mnt/orangefs/test_dir_0", posix.f.S_IRWXU);
		assertEquals(0, ret);
		//MAKE FILE INSIDE DIRECTORY
		fd_creat = posix.creat("/mnt/orangefs/test_dir_0/test_file", posix.f.S_IRWXU);
		assertTrue(fd_creat >= 0);
		// MAKE DIRECTORY
		int ret2 = posix.mkdir("/mnt/orangefs/test_dir_1", posix.f.S_IRWXU);
		assertEquals(0, ret2);
		// MAKE FILE
		fd_creat2 = posix.creat("/mnt/orangefs/test_file_0", posix.f.S_IRWXU);
	    assertTrue(fd_creat2 >= 0);
	  
	    //OPEN FILE
	    fd_open = posix.open("/mnt/orangefs/test_file_0", posix.f.O_RDWR, posix.f.S_IRWXU);
        //OPEN DIRECTORY
	    dir_open = posix.open("/mnt/orangefs/test_dir_0", posix.f.O_DIRECTORY, posix.f.S_IRWXU);
	    
	    System.out.println("SET UP BEFORE TEST");
	   
	}

	@After
	public void tearDown() throws Exception 
	{
		posix.close(fd_open);
		posix.close(dir_open);
		
		int ret = posix.unlink("/mnt/orangefs/test_dir_0/test_file");
		assertEquals(0, ret);
		int ret2 = posix.rmdir("/mnt/orangefs/test_dir_0");
		assertEquals(0, ret2);
		int ret3 = posix.rmdir("/mnt/orangefs/test_dir_1");
		assertEquals(0, ret3);
		int ret4 = posix.unlink("/mnt/orangefs/test_file_0");
		assertEquals(0, ret4);
		
		System.out.println("TEAR DOWN AFTER TEST ");
	}	
	
	

	@Test
	public void testPVFS2POSIXJNI() 
	{
		//PVFS2POSIXJNI object being used
	}

    @Test
	// Added flags in flags.java and posixjni.c
	public void testAccess() 
    {		
		int ret = posix.access("/mnt/orangefs/test_file_0", posix.f.F_OK);
		assertEquals(0,ret);
		//Tested - Access file using different flags with read and write permissions
		int ret2 = posix.access("/mnt/orangefs/test_file_0" ,posix.f.R_OK|posix.f.W_OK|posix.f.X_OK);
		assertEquals(0,ret2);
	}

	@Test
	public void testChdir() 
	{
		int ret = posix.chdir("/tmp");
		assertEquals(0, ret); 
		int ret2 = posix.chdir("/mnt/orangefs/test_dir_0");
		assertEquals(0, ret2); 
	}

	@Test
	public void testChmod() 
	{	
		int ret = posix.chmod("/mnt/orangefs/test_file_0", 00007);
		assertEquals(0, ret);
	}

	@Test
	public void testChown() 
	{
		int ret = posix.chown("/mnt/orangefs/test_file_0", -1, -1);
		assertEquals(0, ret);
	
	}

	@Test
	public void testClose() 
	{
		assertTrue(fd_open >= 0);
		int ret = posix.close(fd_open);
		assertEquals(0, ret); 
	}

	@Test
	public void testCreat() 
	{
    //ALREADY TESTED IN SETUP		
	}

	@Test
	public void testCwdInit() 
	{
		fail("Not yet implemented"); 
	}

	@Test
	public void testDup() 
	{
		fd_dup = posix.dup(fd_open);
		assertTrue(fd_dup >= 0);
	}

	@Test
	public void testDup2() 
	{
		int ret = posix.dup2(fd_open, fd_dup2);
		System.out.println(fd_open);
		System.out.println("ret: " + ret);
		assertTrue(ret >= 0);
	}

	@Test
	public void testFaccessat() 
	{    
		int ret = posix.faccessat(dir_open, "test_file", posix.f.F_OK, posix.f.S_IRWXU);
		assertEquals(0, ret);
		ret = posix.faccessat(dir_open, "/mnt/orangefs/test_file_1", posix.f.F_OK, posix.f.S_IRWXU);
		assertEquals(0, ret);
	}
	
	@Test
	public void testFallocate() 
	{
		int offset = 0;
		long length = 25;
		int ret1 = posix.fallocate(fd_open, 0, offset, length);
		assertEquals(0, ret1);
		posix.close(fd_open);
	}

	@Test
	public void testFchdir() 
	{
		int ret = posix.fchdir(dir_open);
		assertEquals(0,ret);
	}

	@Test
	public void testFchmod() 
	{
		//tested with file_descriptor
		int ret = posix.fchmod(fd_creat, posix.f.S_IRWXG|posix.f.S_ISUID);
		assertEquals(0, ret);
		//tested with an open_file_descriptor
		ret = posix.fchmod(fd_open, posix.f.S_IRWXU);
		assertEquals(0, ret);
	}

	@Test
	public void testFchmodat() 
	{
		//CHANGE PERMISSIONS OF FILE RELATIVE TO A DIRECTORY		
		int ret = posix.fchmodat(dir_open, "test_file", posix.f.O_RDONLY, posix.f.S_IRWXO);
		assertEquals(0,ret);
		//CHANGE PERMISSIONS OF FILE WITH ABSOLUTE PATH
		ret = posix.fchmodat(dir_open, "/mnt/orangefs/testfile_0", posix.f.S_IRWXU,07000);
		assertEquals(0,ret);
	}

	@Test
	public void testFchown() 
	{
		int ret = posix.fchown(fd_creat, -1, -1);
		assertEquals(0,ret);
		
	}

	@Test
	public void testFchownat() 
	{
		int ret = posix.fchownat(dir_open, "test_file", -1, -1, posix.f.S_IRWXU);
		assertEquals(ret,0);	
	}

	@Test
	public void testFdatasync() 
	{
		int ret = posix.fdatasync(fd_open);
		assertEquals(0,ret);
	}


	@Test
	public void testFlistxattr() 
	{
		//String attr_list = "--------------------------";
	    //long ret = posix.flistxattr(fd_open, attr_list, 25);
	    //assertTrue(ret >= 0);
	    //System.out.println(ret);
		fail("errno=34 given");
	}

	@Test
	public void testFremovexattr() 
	{
		//assertTrue(ret,0);
		fail("Not yet implemented"); // TODO
	}

	@Test
	public void testFstat() 
	{
		Stat ret = posix.fstat(fd_open);
		assertTrue(ret != null);
		System.out.println(ret);
	}

	@Test
	public void testFstatat() 
	{
		Stat ret = posix.fstatat(dir_open,"/mnt/orangefs/test_dir_0/test_file", posix.f.S_IRWXU);
		assertTrue(ret != null);
	}

	@Test
	public void testFsync() 
	{
		int ret = posix.fsync(fd_open);
		assertEquals(0, ret);
	}

	@Test
	public void testFtruncate() 
	{
		int trunc_size = 5000;
		int ret = posix.ftruncate(fd_open, trunc_size);
		assertEquals(0, ret);
	}

	@Test
	public void testFutimes() 
	{
		int ret = posix.futimes(fd_open, -1, -1);
		assertEquals(0,ret);
	}

	@Test
	public void testFutimesat() 
	{
		int ret = posix.futimesat(dir_open,"/test_file", -1, -1);
		assertEquals(ret,0);
	}

	@Test
	public void testGetdtablesize() 
	{
	// Returns Current limit on number of open files 
	// per process, At least 20 by default 
		
		int ret = posix.getdtablesize();
		assertTrue(ret >=20);
	}

	@Test
	public void testGetumask() 
	{
		System.out.println(posix.getumask());
	}

	@Test
	public void testIsDir() 
	{
		//Returns non-zero if supplied mode corresponds to a directory
		// Confirm is a directory
		Stat buf = posix.stat("/mnt/orangefs/test_dir_1");
		int ret = posix.isDir(buf.st_mode);
		assertTrue(ret >= 0);
		// Confirm is not a directory
		buf = posix.stat("/mnt/orangefs/test_file_0");
		int ret2 = posix.isDir(buf.st_mode);
		assertEquals(0,ret2);
	}

	@Test
	public void testLchown() 
	{
		//posix.symlink("/mnt/orangefs/posix_file_setup3", "/mnt/orangefs/posix_file_setup3link0");
		//int ret = posix.lchown("/mnt/orangefs/posix_file_setup3link0", -1, -1);
		//assertEquals(0, ret);
		//ret = posix.unlink("/mnt/orangefs/posix_file_setup3link0");
		//assertEquals(0 , ret);    
	}
	
//CANNOT CREATE HARD LINKS AND CANNOT VERIFY HARD LINKS
//NOT IMPLEMENTED IN THE C FILE
	@Test
	public void testLink() 
	{
		//call link to create a link to a file
		//This is a hard link, if I link file_name hard_lnk then hard_link is another name for the filename
		// If I delete the filename data is till accessible via hard link unlink symlink that creates soft link
		//int ret = posix.link("/mnt/orangefs/test_file_0","/mnt/orengefs/test_link");
		//assertEquals(0 , ret);
		//ret = posix.unlink("/mnt/orangefs/test_link");
		fail("Not yet implemented"); // TODO
	}

	@Test
	public void testLinkat() 
	{
		//Implementing with absolute path names
		//int dirfd = posix.open("/mnt/orangefs/test_dir_0", posix.f.O_DIRECTORY, posix.f.S_IRWXU);
		//int newdirfd = posix.mkdir("/mnt/orangefs/posix_dir_linkdir", posix.f.S_IRWXU);
		//int ret = posix.linkat(dirfd, "test_file", dirfd, "test_link", posix.f.O_DIRECT);
		//assertEquals(0, ret);
		//ret = posix.unlink("/mnt/orangefs/test_dir_0/test_link");
		//assertEquals(0, ret);
		//posix.close(dirfd);
		fail("Not yet implemented"); // TODO
	}
	
	@Test
	public void testListxattr() 
	{
	    String attrList = "--------------------------";
		long ret = posix.listxattr("/mnt/orangefs/test_file_0", attrList, 25);
		assertTrue(ret >= 0);
		System.out.println(attrList);
		//fail("Not yet implemented"); // TODO
	}
	

	@Test
	public void testLlistxattr() 
	{
		String Llist = "-------------------------";
		long ret = posix.llistxattr("/mnt/orangefs/test_dir_1", "Llist", 25);
		assertTrue(ret >= 0);
		//fail("Not yet implemented"); // TODO
	}
	
//NO MAN ENTRY
	@Test
	public void testLremovexattr() {
		//long ret = posix.lremovexattr("/mnt/orangefs/test_dir_1", name);
		fail("Not yet implemented"); // TODO
	}

	@Test
	public void testLseek() throws IOException {
		int ret = posix.symlink("/mnt/orangefs/test_file_0", "/mnt/orangefs/test_link");
		assertEquals(0 , ret);
		int fd = posix.open("/mnt/orangefs/test_link", posix.f.O_RDONLY, posix.f.S_IRUSR);
		long ret_seek1  = posix.lseek(fd, 20, posix.f.SEEK_CUR);
		long ret_seek2 = posix.lseek(fd, ret_seek1, posix.f.SEEK_SET);
		assertEquals(ret_seek1, ret_seek2);
		ret = posix.unlink("/mnt/orangefs/test_link");
		assertEquals(0 , ret);
	}

	@Test
	public void testLstat() 
	{
		//Stat requires file with execute permission
		int ret1 = posix.chmod("/mnt/orangefs/test_file_0",posix.f.S_IXUSR );
		assertEquals(0,ret1);
		//creat linked file to check lstat 
		int ret = posix.symlink("/mnt/orangefs/test_file_0", "/mnt/orangefs/test_link");
		assertEquals(0 , ret);
		
		Stat struct = null;
		struct = posix.lstat("/mnt/orangefs/test_link");
		assertTrue(struct != null);
		//Uncomment to see structure printed *
		//System.out.println(struct);
		
		ret = posix.unlink("/mnt/orangefs/test_link");
		assertEquals(0 , ret);
		System.out.println(ret);
		
	    ret1 = posix.chmod("/mnt/orangefs/test_file_0", posix.f.S_IRWXU);
		assertEquals(0, ret1);
	}

	@Test
	public void testMkdir() 
	{
		//Already tested in SetUP
		
	}

	@Test
	public void testMkdirat() throws IOException {
		
		int dirfd = posix.open("/mnt/orangefs/test_dir_1", posix.f.O_DIRECTORY, posix.f.S_IRWXU);
	    //Test with absolute path	
		int ret = posix.mkdirat(dirfd, "/mnt/orangefs/test_dir_1/mkdirat", posix.f.S_IRWXU);
		assertEquals(0,ret);
	    int ret2 = posix.rmdir("/mnt/orangefs/test_dir_1/mkdirat");
	    //Test with relative path (DOES NOT WORK)
	    ret = posix.mkdirat(dirfd, "mkdirat1", posix.f.S_IRWXU);
	    assertEquals(0, ret);
	    //Close and remove
        ret = posix.close(dirfd);
        ret2 = posix.rmdir("/mnt/orangefs/test_dir_1/mkdirat1");
        assertEquals(0, ret2);
	}

	@Test
	public void testMknod() 
	{	
		int ret = posix.mknod("/mnt/orangefs/mknod_regf_file", posix.f.S_IFREG, 0);
		assertEquals(0, ret);	
		ret = posix.mknod("mnt/orangefs/mknod_char_file", posix.f.S_IFCHR,0);
		assertEquals(0, ret);
		ret = posix.mknod("/mnt/orangefs/mknod_fifo_file", posix.f.S_IFIFO,0 );
		assertEquals(0, ret);
	}

	@Test
	public void testMknodat() 
	{
		int ret = posix.mknodat(dir_open, "mknod_regf_file", posix.f.S_IFREG, 0);
		assertEquals(0, ret);	
		ret = posix.mknodat(dir_open, "mknod_char_file", posix.f.S_IFCHR,0);
		assertEquals(0, ret);
		ret = posix.mknodat(dir_open, "mknod_fifo_file", posix.f.S_IFIFO,0 );
		assertEquals(0, ret); 
	}

	 
	@Test
	public void testOpen() throws IOException {
		 //open a file using O_CREAT
		 int fd = posix.open("/mnt/orangefs/test_open_creat", posix.f.O_CREAT, posix.f.S_IRWXU);
		 assertTrue(fd >= 0);		 
		 posix.close(fd);
		 posix.unlink("/mnt/orangefs/test_open_creat");
	}


	@Test
	public void testOpenat() 
	{		
	    //Test with relative path 	
		int fd_openat = posix.openat(dir_open, "test_file", posix.f.O_RDONLY, posix.f.S_IRWXU);
		assertTrue(fd_openat >=0);
		posix.close(fd_openat);
		//Test with absolute path
		fd_openat = posix.openat(dir_open, "/mnt/orangefs/test_dir_0/test_file", posix.f.O_RDONLY, posix.f.S_IRWXU);
		assertTrue(fd_openat >= 0);
		posix.close(fd_openat);
		
		// The following code gives a SEGFAULT
		/*
		//Test with relative path and O_CREAT
		fd_openat = posix.openat(dir_open, "test_create_file", posix.f.O_CREAT, posix.f.S_IRWXU);
		assertTrue(fd_openat >= 0);
		//Test with absolute path and O_CREAT
		fd_openat = posix.openat(dir_open, "test_create_file2", posix.f.O_CREAT, posix.f.S_IRWXU);
		assertTrue(fd_openat >= 0);
		
		posix.unlink("/mnt/orangefs/test_dir_0/test_creat_file");
        */
		fail("O_CREAT flag failure");
	}

	@Test
	public void testPread() 
	{	
		int byte_bufsize = 100;
		byte[] buf_read = new byte[byte_bufsize];
		long offset = 5;
		long count = 20;
		//Write bytes to test_file_0 so they may be read in
		ByteBuffer buf_write = ByteBuffer.allocateDirect(100);
		posix.write(fd_open, buf_write, 100);
		//Test reading of bytes at offset
		posix.lseek(fd_open, 0, posix.f.SEEK_SET);
		long ret_pread = posix.pread(fd_open, buf_read, count, offset);
		System.out.println("ret_pread = " + ret_pread);
		assertTrue(ret_pread == count);
	}

	@Test
	public void testPwrite() 
	{
		long offset = 5;
		long count = 20;
		byte[] buf = new byte[100];
		long ret = posix.pwrite(fd_open, buf, count, offset);
		assertTrue(ret <= count );
		System.out.println(ret);
		//Verify if the file descriptor is actually at the location we expect it to be after completing write 
		long seek_ret = posix.lseek(fd_open, ret+offset, posix.f.SEEK_SET);
	    assertEquals(seek_ret, ret);
	}

	@Test
	public void testRead() 
	{
		long count = 50;
		int file_size = 100;
		
		//Write to file 
		ByteBuffer buf_write = ByteBuffer.allocateDirect(100);
		long ret = posix.write(fd_open, buf_write, 100);	
		System.out.println("write_ret: " + ret);
		//START READING FROM BEGINNING OF FILE
		posix.lseek(fd_open, 0, posix.f.SEEK_SET);
		ByteBuffer buf_read = ByteBuffer.allocateDirect(file_size);
		ret  = posix.read(fd_open, buf_read, count);
		//FUNCTION READS UPTO COUNT BYTES
		assertTrue(ret <= count); 
		System.out.println("ret:" + ret);
		System.out.println("count" + count);
		//Verify that the file pointer is actually at 
		//the position we expect it to be after reading 
		long seek_ret = posix.lseek(fd_open, ret, posix.f.SEEK_SET);
		assertEquals(seek_ret, ret);	
	}


	@Test
	public void testReadlink() 
	{		
		//TESTED AFTER CREATING LINK IN SYMLINK 
		/*
		long bufsiz = 100 ;
		String buf = "/mnt/orangefs/posix_file_setup2link"   ;
		long ret = posix.readlink("/mnt/orangefs/posix_file_setup2",buf, bufsiz);
		assertTrue(ret >=0);
        */
	}
//Doesn't work Out of bound error
	@Test
	public void testReadlinkat() 
	{
		/* 
		//read value of a symbolic link relative to a directory file 
		//returns number of bytes placed in buf
		int newfd = posix.open("/mnt/orangefs/test_dir_0", posix.f.O_RDONLY, posix.f.S_IRWXU);
		int ret = posix.symlinkat("/mnt/ornagefs/test_dir_0/test_file", newfd, "/mnt/orangefs/test_dir_setup/posix_creat_softlink");
		assertEquals(0 , ret);
		String buf = "posix_creat_softlink";
		long bufsiz = 0;
		long ret1 = posix.readlinkat(newfd, "/test_file", buf, bufsiz);
		System.out.println(ret1);
		ret = posix.unlinkat(newfd, "posix_creat_softlink", 0);
		assertEquals(0,ret);*/
	}
// NO MAN ENTRY
	
	@Test
	public void testRemovexattr() 
	{
		//assertEquals(ret , 0);
		fail("Not yet implemented"); // TODO
	}

	@Test
	public void testRename() 
	{
		int ret = posix.rename("/mnt/orangefs/test_file_0","/mnt/orangefs/renamed");
		assertEquals(0 , ret);
		ret = posix.rename("/mnt/orangefs/renamed", "/mnt/orangefs/test_file_0");
		assertEquals(0, ret);
	}

	
	@Test
	public void testRenameat() throws IOException {
	
		int dirfd = posix.open("/mnt/orangefs/test_dir_0", posix.f.O_DIRECTORY, posix.f.S_IRWXU);
		int dirfd_tmp = posix.open("/mnt/orangefs/test_dir_1", posix.f.O_DIRECTORY, posix.f.S_IRWXU);
		//Relative path gives a SEGFAULT
		/*
		int ret = posix.renameat(dirfd, "test_file", dirfd_tmp, "rename_test_file");
		assertEquals(0, ret);
		//Rename to original for teardown
		ret = posix.renameat(dirfd_tmp, "rename_test_file", dirfd, "test_file");
		assertEquals(0, ret); 		
		*/
		
		//Test with absolute path
		int ret2 = posix.renameat(dirfd, "/mnt/orangefs/test_dir_0/test_file", dirfd_tmp, "/mnt/orangefs/test_dir_1/rename_test_file");
		assertEquals(0, ret2);
		//rename to original for teardown
		int ret = posix.renameat(dirfd_tmp, "/mnt/orangefs/test_dir_1/rename_test_file", dirfd, "/mnt/orangefs/test_dir_0/test_file");
		assertEquals(0, ret);
		
		posix.close(dirfd);
		posix.close(dirfd_tmp);
		
		fail("Relative path names not working");
		
	}

	@Test
	public void testRmdir() 
	{		
        //Already tested in TearDown
	}

	@Test
	public void testStat() 
	{
		//Set execute permissions on file that lead to path,
		//REQUIRED for Stat
		
		Stat ret = posix.stat("/mnt/orangefs/test_file_0");
		System.out.println(ret);
	}

	@Test
	public void testSymlink() 
	{		
		//CREATING LINK FOR posix_linkfile 

		int ret = posix.symlink("/mnt/orangefs/posix_linkfile", "/mnt/orangefs/posix_linkfile_softlink");	
		assertEquals(0 , ret);
		long bufsiz = 1000 ;
		String buf = "/mnt/orangefs/posix_linkfile_softlink"   ;
		long ret1 = posix.readlink("/mnt/orangefs/posix_linkfile_softlink",buf, bufsiz);
		assertTrue(ret1 >=0);
		System.out.println(ret1);
		ret = posix.unlink("/mnt/orangefs/posix_linkfile_softlink");
		assertEquals(0 , ret);
	}

	@Test
	public void testSymlinkat() throws IOException {
		int newfd = posix.open("/mnt/orangefs/test_dir_1", posix.f.O_RDONLY, posix.f.S_IRWXU);
		int ret = posix.symlinkat("/mnt/ornagefs/test_dir_1/test_file1", newfd, "/mnt/orangefs/test_dir_1/posix_creat_softlink");
		assertEquals(0 , ret);
		ret = posix.unlinkat(newfd, "posix_creat_softlink", 0);
		assertEquals(0,ret);
	}

	//Could not verify from terminal but call succeeds
	@Test
	public void testSync() 
	{
		posix.sync(); 
	}

	@Test
	public void testTruncate() 
	{	
		Stat ret = posix.stat("/mnt/orangefs/test_file_0");
		System.out.println(ret);
		int ret2= posix.truncate("/mnt/orangefs/test_file_0", 10);
		ret= posix.stat("/mnt/orangefs/test_file_0");
		System.out.println(ret);
		assertEquals(0, ret2);

	}

	@Test
	public void testUmask() 
	{	
		//Umask returns the previous mode
		int original = posix.umask(0777);
		int changed = posix.umask(original);
		assertEquals(changed, 0777);
	}

	@Test
	public void testUnlink() 
	{
	//Tested Already in TearDown	

	
	}

	@Test
	public void testUnlinkat() 
	{
		//TESTED IN SYMLINKAT
		// int ret = posix.unlinkat(newdirfd, "/mnt/orangefs/posix_creat_softlink", posix.f.AT_REMOVEDIR);
		//assertEquals(ret , 0);		
	
	}
	@Test
	public void testUtime() 
	{
		// time argument is a structure
		//Structure dosn't work 
		//while array in utimes work
		
		int ret = posix.utime("/mnt/orangefs/posix_creat_test1", -1,-1);
		assertEquals(0 , ret);
	}

	@Test
	public void testUtimes() 
	{
		//time argument is an array
		// SET ACCESS AND MODIFICATION TIME TO CURRENT TIME
		int ret = posix.utimes("/mnt/orangefs/test_file_0", -1,-1);
		assertEquals(0,ret);
	
	}
		

	@Test
	public void testWrite() 
	{
		long count = 50;
		int file_size = 100;
		//Open file in write mode
		ByteBuffer buf = ByteBuffer.allocateDirect(file_size);
		long ret = posix.write(fd_open, buf, count);
		System.out.println(fd_open);
		//Verify if bytes written is same as desired 
		assertEquals(count,ret);
		// Verify if file descriptor is at desired location
		long seek_ret = posix.lseek(fd_open, ret, posix.f.SEEK_SET);
		assertEquals(ret, seek_ret);
		
	}
}

