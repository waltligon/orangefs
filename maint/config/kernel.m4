AC_DEFUN([AX_KERNEL_FEATURES],
[
	dnl 
	dnl kernel feature tests.  Set CFLAGS once here and use it for all
	dnl kernel features.  reset to the old value at the end. 
	dnl 
	dnl on some systems, there is a /usr/include/linux/xattr_acl.h , so the
	dnl check for xattr_acl.h down below will always pass, even if it
	dnl should fail.  this hack will bring in just enough system headers
	dnl for kernel compilation

	NOSTDINCFLAGS="-nostdinc -isystem `$CC -print-file-name=include`"

	CFLAGS="$USR_CFLAGS $NOSTDINCFLAGS -I$lk_src/include -I$lk_src/include/asm-i386/mach-generic -I$lk_src/include/asm-i386/mach-default -DKBUILD_STR(s)=#s -DKBUILD_BASENAME=KBUILD_STR(empty)  -DKBUILD_MODNAME=KBUILD_STR(empty)"


	AC_MSG_CHECKING(for i_size_write in kernel)
	dnl if this test passes, the kernel does not have it
	dnl if this test fails, the kernel already defined it
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		void i_size_write(struct inode *inode,
				loff_t i_size)
		{
			return;
		}
	], [],
		AC_MSG_RESULT(no),
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_I_SIZE_WRITE, 1, Define if kernel has i_size_write),
	)

	AC_MSG_CHECKING(for i_size_read in kernel)
	dnl if this test passes, the kernel does not have it
	dnl if this test fails, the kernel already defined it
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		loff_t i_size_read(struct inode *inode)
		{
			return 0;
		}
	], [],
		AC_MSG_RESULT(no),
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_I_SIZE_READ, 1, Define if kernel has i_size_read),
	)

	AC_MSG_CHECKING(for parent_ino in kernel)
	dnl if this test passes, the kernel does not have it
	dnl if this test fails, the kernel already defined it
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		ino_t parent_ino(struct dentry *dentry)
		{
			return (ino_t)0;
		}
	], [],
		AC_MSG_RESULT(no),
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PARENT_INO, 1, Define if kernel has parent_ino),
	)

	dnl The name of this field changed from memory_backed to capabilities
	dnl in 2.6.12.
	AC_MSG_CHECKING(for memory_backed in struct backing_dev_info in kernel)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/mm.h>
		#include <linux/backing-dev.h>
		static struct backing_dev_info bdi = {
		    .memory_backed = 0
		};
	], [],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BDI_MEMORY_BACKED, 1, Define if struct backing_dev_info in kernel has memory_backed),
		AC_MSG_RESULT(no)
	)

	dnl checking if we have a sendfile callback 
	AC_MSG_CHECKING(for sendfile callback in struct file_operations in kernel)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		static struct file_operations fop = {
		    .sendfile = NULL,
		};
	], [],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SENDFILE_VFS_SUPPORT, 1, Define if struct file_operations in kernel has sendfile callback),
		AC_MSG_RESULT(no)
	)

	AC_MSG_CHECKING(for aio support in kernel)
	dnl if this test passes, the kernel has it
	dnl if this test fails, the kernel does not have it
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/wait.h>
		#include <linux/aio.h>
		  static struct kiocb iocb;
		], [],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_AIO, 1, Define if kernel has aio support)
		have_aio=yes,
		AC_MSG_RESULT(no)
		have_aio=no
	)

	AC_MSG_CHECKING(for touch_atime support in kernel)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		void touch_atime(struct vfsmount *mnt, struct dentry *dentry)
		{
			return;
		}
		], [],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_TOUCH_ATIME, 1, Define if kernel has touch_atime routine),
		AC_MSG_RESULT(no)
	)
	if test "x$have_aio" = "xyes" -a "x$enable_kernel_aio" = "xyes"; then
		AC_MSG_CHECKING(for ki_dtor in kiocb structure of kernel)
		dnl if this test passes, the kernel does have it and we enable
		dnl support for AIO.   if this test fails, the kernel does not
		dnl have this member and we disable support for AIO
		AC_TRY_COMPILE([
			#define __KERNEL__
			#include <linux/wait.h>
			#include <linux/aio.h>
			static struct kiocb io_cb = {
					  .ki_dtor = NULL,
			};
		], [],
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_AIO_VFS_SUPPORT, 1, Define if we are enabling VFS AIO support in kernel),
			AC_MSG_RESULT(no)
		)
	fi

	AC_MSG_CHECKING(for xattr support in kernel)
	dnl if this test passes, the kernel has it
	dnl if this test fails, the kernel does not have it
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
			  static struct inode_operations in_op = {
				  .getxattr = NULL
			  };
		], [],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_XATTR, 1, Define if kernel has xattr support)
		have_xattr=yes,
		AC_MSG_RESULT(no)
		have_xattr=no
	)

	if test "x$have_xattr" = "xyes"; then
	   dnl Test to check if setxattr function has a const void * argument
	   AC_MSG_CHECKING(for const argument to setxattr function)
	   tmp_cflags=$CFLAGS
	   dnl if this test passes, there is a const void* argument
	   CFLAGS="$CFLAGS -Werror"
	   AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		int pvfs2_setxattr(struct dentry *dentry, const char *name,
		const void *value, size_t size, int flags) { return (0);};
		static struct inode_operations in_op = {
			.setxattr = pvfs2_setxattr
		};
		], [],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SETXATTR_CONST_ARG, 1, Define if kernel setxattr has const void* argument),
		AC_MSG_RESULT(no)
		)
		CFLAGS=$tmp_cflags
	fi

	dnl Test to see if sysctl proc handlers have a 6th argument
	AC_MSG_CHECKING(for 6th argument to sysctl proc handlers)
	dnl if this test passes, there is a 6th argument
	AC_TRY_COMPILE([
	    #define __KERNEL__
	    #include <linux/fs.h>
	    #include <linux/sysctl.h>
	    ], [
	    proc_dointvec_minmax(NULL, 0, NULL, NULL, NULL, NULL);
	    ],
	    AC_MSG_RESULT(yes)
	    AC_DEFINE(HAVE_PROC_HANDLER_SIX_ARG, 1, Define if sysctl proc handlers have 6th argument),
	    AC_MSG_RESULT(no)
	    )

	AC_CHECK_HEADERS([linux/posix_acl.h], [], [], 
		[#define __KERNEL__
		 #include <linux/fs.h>
		 #ifdef HAVE_XATTR 
		 #include <linux/xattr.h> 
		 #endif
		 ] )

	AC_CHECK_HEADERS([linux/posix_acl_xattr.h], [], [], 
		[#define __KERNEL__
		 #include <linux/fs.h>
		 #ifdef HAVE_XATTR 
		 #include <linux/xattr.h> 
		 #endif
		 ] )

	dnl linux-2.6.11 had xattr_acl.h, but 2.6.12 did not!
	AC_CHECK_HEADERS([linux/xattr_acl.h], [], [], 
		[#define __KERNEL__
		 #include <linux/fs.h>
		 #ifdef HAVE_XATTR
		 #include <linux/xattr.h>
		 #endif
		 ] )

	AC_MSG_CHECKING(for generic_permission api in kernel)
	dnl if this test passes, the kernel does not have it
	dnl if this test fails, the kernel has it defined with a different
	dnl signature!  deliberately, the signature for this method has been
	dnl changed for it to give a compiler error.

	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		int generic_permission(struct inode *inode)
		{
			return 0;
		}
	], [],
		AC_MSG_RESULT(no),
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GENERIC_PERMISSION, 1, Define if kernel has generic_permission),
	)

	AC_MSG_CHECKING(for generic_getxattr api in kernel)
	dnl if this test passes, the kernel does not have it
	dnl if this test fails, the kernel has it defined
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
				#include <linux/xattr.h>
		int generic_getxattr(struct inode *inode)
		{
			return 0;
		}
	], [],
		AC_MSG_RESULT(no),
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GENERIC_GETXATTR, 1, Define if kernel has generic_getxattr),
	)

	AC_MSG_CHECKING(for arg member in read_descriptor_t in kernel)
	AC_TRY_COMPILE([
	    #define __KERNEL__
	    #include <linux/fs.h>
	    ], [
	    read_descriptor_t x;
	    x.arg.data = NULL;
	    ],
	    AC_MSG_RESULT(yes)
	    AC_DEFINE(HAVE_ARG_IN_READ_DESCRIPTOR_T, 1, Define if read_descriptor_t has an arg member),
	    AC_MSG_RESULT(no)
	)

	AC_MSG_CHECKING(for second arg type int in address_space_operations releasepage)
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_TRY_COMPILE([
	    #define __KERNEL__
	    #include <linux/fs.h>
	    extern int rp(struct page *page, int foo);
	    ], [
	    struct address_space_operations aso = {
		.releasepage = rp
	    };
	    ],
	    AC_MSG_RESULT(yes)
	    AC_DEFINE(HAVE_INT_ARG2_ADDRESS_SPACE_OPERATIONS_RELEASEPAGE, 1, Define if sceond argument to releasepage in address_space_operations is type int),
	    AC_MSG_RESULT(no)
	    )
	CFLAGS=$tmp_cflags

	AC_MSG_CHECKING(for int return in inode_operations follow_link)
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_TRY_COMPILE([
	    #define __KERNEL__
	    #include <linux/fs.h>
	    extern int fl(struct dentry *d, struct nameidata *n);
	    ], [
	    struct inode_operations io = {
		.follow_link = fl
	    };
	    ],
	    AC_MSG_RESULT(yes)
	    AC_DEFINE(HAVE_INT_RETURN_INODE_OPERATIONS_FOLLOW_LINK, 1, Define if return value from follow_link in inode_operations is type int),
	    AC_MSG_RESULT(no)
	    )
	CFLAGS=$tmp_cflags

	CFLAGS=$oldcflags
])
