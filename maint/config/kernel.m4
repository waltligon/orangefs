AC_DEFUN([AX_KERNEL_FEATURES],
[
	dnl 
	dnl kernel feature tests.  Set CFLAGS once here and use it for all
	dnl kernel features.  reset to the old value at the end. 
	dnl 
	dnl on some systems, there is a /usr/include/linux/xattr_acl.h , so the
	dnl check for xattr_acl.h down below will always pass, even if it
	dnl should fail.  this hack (-nostdinc -isystem ...) will bring in just
	dnl enough system headers dnl for kernel compilation

	dnl -Werror can be overkill, but for these kernel feature tests
	dnl 'implicit function declaration' usually ends up in an undefined
	dnl symbol somewhere.

	NOSTDINCFLAGS="-Werror-implicit-function-declaration -nostdinc -isystem `$CC -print-file-name=include`"

	CFLAGS="$USR_CFLAGS $NOSTDINCFLAGS -I$lk_src/include -I$lk_src/include/asm/mach-default -DKBUILD_STR(s)=#s -DKBUILD_BASENAME=KBUILD_STR(empty)  -DKBUILD_MODNAME=KBUILD_STR(empty) -imacros $lk_src/include/linux/autoconf.h"

        dnl we probably need additional includes if this build is intended
        dnl for a different architecture
	if test -n "${ARCH}" ; then
		CFLAGS="$CFLAGS -I$lk_src/arch/${ARCH}/include -I$lk_src/arch/${ARCH}/include/asm/mach-default"
        else
            SUBARCH=`uname -m | sed -e s/i.86/i386/ -e s/sun4u/sparc64/ \
            -e s/arm.*/arm/ -e s/sa110/arm/ \
            -e s/s390x/s390/ -e s/parisc64/parisc/ \
            -e s/ppc.*/powerpc/ -e s/mips.*/mips/ \
            -e s/sh.*/sh/`
            if test "x$SUBARCH" = "xi386"; then
                ARCH=x86    
            elif test "x$SUBARCH" = "xx86_64"; then
                ARCH=x86    
            elif test "x$SUBARCH" = "xsparc64"; then
                ARCH=sparc    
            else
                ARCH=$SUBARCH
            fi

            CFLAGS="$CFLAGS -I$lk_src/arch/${ARCH}/include -I$lk_src/arch/${ARCH}/include/asm/mach-default"
	fi

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

	AC_MSG_CHECKING(for iget_locked function in kernel)
	dnl if this test passes, the kernel does not have it
	dnl if this test fails, the kernel already defined it
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		loff_t iget_locked(struct inode *inode)
		{
			return 0;
		}
	], [],
		AC_MSG_RESULT(no),
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IGET_LOCKED, 1, Define if kernel has iget_locked),
	)

	AC_MSG_CHECKING(for iget4_locked function in kernel)
	dnl if this test passes, the kernel does not have it
	dnl if this test fails, the kernel already defined it
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		loff_t iget4_locked(struct inode *inode)
		{
			return 0;
		}
	], [],
		AC_MSG_RESULT(no),
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IGET4_LOCKED, 1, Define if kernel has iget4_locked),
	)

	AC_MSG_CHECKING(for iget5_locked function in kernel)
	dnl if this test passes, the kernel does not have it
	dnl if this test fails, the kernel already defined it
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		loff_t iget5_locked(struct inode *inode)
		{
			return 0;
		}
	], [],
		AC_MSG_RESULT(no),
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IGET5_LOCKED, 1, Define if kernel has iget5_locked),
	)

	dnl Check if the kernel defines the xtvec structure.
	dnl This is part of a POSIX extension.
	AC_MSG_CHECKING(for struct xtvec in kernel)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/uio.h>
		static struct xtvec xv = { 0, 0 };
	], [],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_STRUCT_XTVEC, 1, Define if struct xtvec is defined in the kernel),
		AC_MSG_RESULT(no)
	)

	dnl 2.6.20 deprecated kmem_cache_t; some old ones do not have struct
	dnl kmem_cache, but may have kmem_cache_s.  It's a mess.  Just look
	dnl for this, and assume _t if not found.
	dnl This test relies on gcc complaining about declaring a struct
	dnl in a parameter list.  Fragile, but nothing better is available
	dnl to check for the existence of a struct.  We cannot see the
	dnl definition of the struct in the kernel, it's private to the
	dnl slab implementation.  And C lets you declare structs freely as
	dnl long as you don't try to deal with their contents.
        tmp_cflags=$CFLAGS
        CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for struct kmem_cache in kernel)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/kernel.h>
		#include <linux/slab.h>

		int foo(struct kmem_cache *s)
		{
		    return (s == NULL) ? 3 : 4;
		}
	], [],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_STRUCT_KMEM_CACHE, 1, Define if struct kmem_cache is defined in kernel),
		AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

	dnl 2.6.20 removed SLAB_KERNEL.  Need to use GFP_KERNEL instead
	AC_MSG_CHECKING(for SLAB_KERNEL flag in kernel)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/slab.h>
		static int flags = SLAB_KERNEL;
	], [],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SLAB_KERNEL, 1, Define if SLAB_KERNEL is defined in kernel),
		AC_MSG_RESULT(no)
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
	if test "x$enable_kernel_sendfile" = "xyes"; then
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
	fi

	dnl checking if we have a readv callback in super_operations 
	AC_MSG_CHECKING(for readv callback in struct file_operations in kernel)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		static struct file_operations fop = {
		    .readv = NULL,
		};
	], [],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_READV_FILE_OPERATIONS, 1, Define if struct file_operations in kernel has readv callback),
		AC_MSG_RESULT(no)
	)
	dnl checking if we have a writev callback in super_operations 
	AC_MSG_CHECKING(for writev callback in struct file_operations in kernel)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		static struct file_operations fop = {
		    .writev = NULL,
		};
	], [],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_WRITEV_FILE_OPERATIONS, 1, Define if struct file_operations in kernel has writev callback),
		AC_MSG_RESULT(no)
	)

	dnl checking if we have a find_inode_handle callback in super_operations 
	AC_MSG_CHECKING(for find_inode_handle callback in struct super_operations in kernel)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		static struct super_operations sop = {
		    .find_inode_handle = NULL,
		};
	], [],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FIND_INODE_HANDLE_SUPER_OPERATIONS, 1, Define if struct super_operations in kernel has find_inode_handle callback),
		AC_MSG_RESULT(no)
	)

	dnl 2.6.18.1 removed this member
	AC_MSG_CHECKING(for i_blksize in struct inode)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		static struct inode i = {
			.i_blksize = 0,
			};
		], [],
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_I_BLKSIZE_IN_STRUCT_INODE, 1, Define if struct inode in kernel has i_blksize member),
			AC_MSG_RESULT(no)
	)

	dnl 2.6.16 removed this member
	AC_MSG_CHECKING(for i_sem in struct inode)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		static struct inode i = {
			.i_sem = {0},
			};
		], [],
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_I_SEM_IN_STRUCT_INODE, 1, Define if struct inode in kernel has i_sem member),
			AC_MSG_RESULT(no)
	)

	dnl checking if we have a statfs_lite callback in super_operations 
	AC_MSG_CHECKING(for statfs_lite callback in struct super_operations in kernel)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		static struct super_operations sop = {
		    .statfs_lite = NULL,
		};
	], [],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_STATFS_LITE_SUPER_OPERATIONS, 1, Define if struct super_operations in kernel has statfs_lite callback),
		AC_MSG_RESULT(no)
	)

	dnl checking if we have a fill_handle callback in inode_operations 
	AC_MSG_CHECKING(for fill_handle callback in struct inode_operations in kernel)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		static struct inode_operations iop = {
		    .fill_handle = NULL,
		};
	], [],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FILL_HANDLE_INODE_OPERATIONS, 1, Define if struct inode_operations in kernel has fill_handle callback),
		AC_MSG_RESULT(no)
	)

	dnl checking if we have a getattr_lite callback in inode_operations 
	AC_MSG_CHECKING(for getattr_lite callback in struct inode_operations in kernel)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		static struct inode_operations iop = {
		    .getattr_lite = NULL,
		};
	], [],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GETATTR_LITE_INODE_OPERATIONS, 1, Define if struct inode_operations in kernel has getattr_lite callback),
		AC_MSG_RESULT(no)
	)

	dnl checking if we have a get_fs_key callback in super_operations 
	AC_MSG_CHECKING(for get_fs_key callback in struct super_operations in kernel)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		static struct super_operations sop = {
		    .get_fs_key = NULL,
		};
	], [],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GET_FS_KEY_SUPER_OPERATIONS, 1, Define if struct super_operations in kernel has get_fs_key callback),
		AC_MSG_RESULT(no)
	)
	
	dnl checking if we have a readdirplus callback in file_operations
	AC_MSG_CHECKING(for readdirplus member in file_operations structure)
	AC_TRY_COMPILE([
	    #define __KERNEL__
	    #include <linux/fs.h>
		 ], [
		 struct file_operations filop = {
				.readdirplus = NULL
		 };
	    ],
	    AC_MSG_RESULT(yes)
		 AC_DEFINE(HAVE_READDIRPLUS_FILE_OPERATIONS, 1, Define if struct file_operations in kernel has readdirplus callback),
	    AC_MSG_RESULT(no)
	    )

	dnl checking if we have a readdirplus_lite callback in file_operations
	AC_MSG_CHECKING(for readdirplus_lite member in file_operations structure)
	AC_TRY_COMPILE([
	    #define __KERNEL__
	    #include <linux/fs.h>
		 ], [
		 struct file_operations filop = {
				.readdirplus_lite = NULL
		 };
	    ],
	    AC_MSG_RESULT(yes)
		 AC_DEFINE(HAVE_READDIRPLUSLITE_FILE_OPERATIONS, 1, Define if struct file_operations in kernel has readdirplus_lite callback),
	    AC_MSG_RESULT(no)
	    )


	dnl checking if we have a readx callback in file_operations
	AC_MSG_CHECKING(for readx member in file_operations structure)
	AC_TRY_COMPILE([
	    #define __KERNEL__
	    #include <linux/fs.h>
		 ], [
		 struct file_operations filop = {
				.readx = NULL
		 };
	    ],
	    AC_MSG_RESULT(yes)
		 AC_DEFINE(HAVE_READX_FILE_OPERATIONS, 1, Define if struct file_operations in kernel has readx callback),
	    AC_MSG_RESULT(no)
	    )

	dnl checking if we have a writex callback in file_operations
	AC_MSG_CHECKING(for writex member in file_operations structure)
	AC_TRY_COMPILE([
	    #define __KERNEL__
	    #include <linux/fs.h>
		 ], [
		 struct file_operations filop = {
				.writex = NULL
		 };
	    ],
	    AC_MSG_RESULT(yes)
		 AC_DEFINE(HAVE_WRITEX_FILE_OPERATIONS, 1, Define if struct file_operations in kernel has writex callback),
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

		tmp_cflags=$CFLAGS
		dnl if this test passes, the signature of aio_read has changed to the new one 
		CFLAGS="$CFLAGS -Werror"
		AC_MSG_CHECKING(for new prototype of aio_read callback of file_operations structure)
		AC_TRY_COMPILE([
			#define __KERNEL__
			#include <linux/fs.h>
			extern ssize_t my_aio_read(struct kiocb *, const struct iovec *, unsigned long, loff_t);
			static struct file_operations fop = {
					  .aio_read = my_aio_read,
			};
		], [],
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_AIO_NEW_AIO_SIGNATURE, 1, Define if VFS AIO support in kernel has a new prototype),
			AC_MSG_RESULT(no)
		)
		CFLAGS=$tmp_cflags

	fi

	AC_MSG_CHECKING(for dentry argument in kernel super_operations statfs)
	dnl Rely on the fact that there is an external vfs_statfs that is
	dnl of the same type as the .statfs in struct super_operations to
	dnl verify the signature of that function pointer.  There is a single
	dnl commit in the git history where both changed at the same time
	dnl from super_block to dentry.
	dnl
	dnl The alternative approach of trying to define a s_op.statfs is not
	dnl as nice because that only throws a warning, requiring -Werror to
	dnl catch it.  This is a problem if the compiler happens to spit out
	dnl other spurious warnings that have nothing to do with the test.
	dnl
	dnl If this test passes, the kernel uses a struct dentry argument.
	dnl If this test fails, the kernel uses something else (old struct
	dnl super_block perhaps).
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		int vfs_statfs(struct dentry *de, struct kstatfs *kfs)
		{
			return 0;
		}
		], [],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DENTRY_STATFS_SOP, 1, Define if super_operations statfs has dentry argument),
		AC_MSG_RESULT(no)
	)

	AC_MSG_CHECKING(for vfsmount argument in kernel file_system_type get_sb)
	dnl Same trick as above.  A single commit changed mayn things at once:
	dnl type and signature of file_system_type.get_sb, and signature of
	dnl get_sb_bdev.  This test is a bit more tenuous, as get_sb_bdev
	dnl isn't used directly in a file_system_type, but is a popular helper
	dnl for many FSes.  And it has not exactly the same signature.
	dnl
	dnl If this test passes, the kernel has the most modern known form,
	dnl which includes a stfuct vfsmount argument.
	dnl If this test fails, the kernel uses something else.
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		int get_sb_bdev(struct file_system_type *fs_type, int flags,
				const char *dev_name, void *data,
				int (*fill_super)(struct super_block *, void *,
						  int),
				struct vfsmount *vfsm)
		{
			return 0;
		}
		], [],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_VFSMOUNT_GETSB, 1, Define if file_system_type get_sb has vfsmount argument),
		AC_MSG_RESULT(no)
	)

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
	   dnl if this test passes, there is a const void* argument
	   AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		], 
		[
			struct inode_operations inode_ops;
			int ret;
			struct dentry * dent = NULL;
			const char * name = NULL;
			const void * val = NULL;
			size_t size = 0;
			int flags = 0;

			ret = inode_ops.setxattr(dent, name, val, size, flags);
		],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SETXATTR_CONST_ARG, 1, Define if kernel setxattr has const void* argument),
		AC_MSG_RESULT(no)
		)
	fi

        dnl the proc handler functions have changed over the years.
        dnl pre-2.6.8: proc_handler(ctl_table       *ctl,
        dnl                         int             write,
        dnl                         struct file     *filp,
        dnl                         void            *buffer,
        dnl                         size_t          *lenp)
        dnl
        dnl 2.6.8-2.6.31: proc_handler(ctl_table       *ctl,
        dnl                            int             write,
        dnl                            struct file     *filp,
        dnl                            void            *buffer,
        dnl                            size_t          *lenp,
        dnl                            loff_t          *ppos)
        dnl > 2.6.31: proc_handler(ctl_table       *ctl,
        dnl                        int             write,
        dnl                        void            *buffer,
        dnl                        size_t          *lenp,
        dnl                        loff_t          *ppos)
 
	dnl Test to see if sysctl proc handlers have a file argument
	AC_MSG_CHECKING(for file argument to sysctl proc handlers)
	AC_TRY_COMPILE([
	    #define __KERNEL__
	    #include <linux/fs.h>
	    #include <linux/sysctl.h>
	    ], [
                struct ctl_table * ctl = NULL;
                int write = 0;
                struct file * filp = NULL;
                void __user * buffer = NULL;
                size_t * lenp = NULL;
                loff_t * ppos = NULL;

                proc_dointvec_minmax(ctl, write, filp, buffer, lenp, ppos);
	    ],
	    AC_MSG_RESULT(yes)
	    AC_DEFINE(HAVE_PROC_HANDLER_FILE_ARG, 1, Define if sysctl proc handlers have 6th argument),
	    AC_MSG_RESULT(no)
	    )

	AC_MSG_CHECKING(for ppos argument to sysctl proc handlers)
	dnl if this test passes, there is a ppos argument
	AC_TRY_COMPILE([
	    #define __KERNEL__
	    #include <linux/fs.h>
	    #include <linux/sysctl.h>
	    ], [
                struct ctl_table * ctl = NULL;
                int write = 0;
                void __user * buffer = NULL;
                size_t * lenp = NULL;
                loff_t * ppos = NULL;

                proc_dointvec_minmax(ctl, write, buffer, lenp, ppos);
	    ],
	    AC_MSG_RESULT(yes)
	    AC_DEFINE(HAVE_PROC_HANDLER_PPOS_ARG, 1, Define if sysctl proc handlers have ppos argument),
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

	AC_CHECK_HEADERS([linux/mount.h], [], [], 
		[#define __KERNEL__
		 #include <linux/mount.h>
		 ] )
	AC_CHECK_HEADERS([linux/ioctl32.h], [], [], 
		[#define __KERNEL__
		 #include <linux/ioctl32.h>
		 ] )
	AC_CHECK_HEADERS([linux/compat.h], [], [], 
		[#define __KERNEL__
		 #include <linux/compat.h>
		 ] )
	AC_CHECK_HEADERS([linux/syscalls.h], [], [], 
		[#define __KERNEL__
		 #include <linux/syscalls.h>
		 ] )
	AC_CHECK_HEADERS([asm/ioctl32.h], [], [], 
		[#define __KERNEL__
		 #include <asm/ioctl32.h>
		 ] )
	AC_CHECK_HEADERS([linux/exportfs.h], [],[],
		[#define __KERNEL__
		 #include <linux/exportfs.h>
		])

	AC_MSG_CHECKING(for generic_file_readv api in kernel)
	dnl if this test passes, the kernel does not have it
	dnl if this test fails, the kernel has it defined with a different
	dnl signature!  deliberately, the signature for this method has been
	dnl changed for it to give a compiler error.

	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		int generic_file_readv(struct inode *inode)
		{
			return 0;
		}
	], [],
		AC_MSG_RESULT(no),
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GENERIC_FILE_READV, 1, Define if kernel has generic_file_readv),
	)

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

        AC_MSG_CHECKING(for fh_to_dentry member in export_operations in kernel)
	AC_TRY_COMPILE([
	    #define __KERNEL__
	    #include <linux/exportfs.h>
	    ], [
	    struct export_operations x;
	    x.fh_to_dentry = NULL;
	    ],
	    AC_MSG_RESULT(yes)
	    AC_DEFINE(HAVE_FHTODENTRY_EXPORT_OPERATIONS, 1, Define if export_operations has an fh_to_dentry member),
	    AC_MSG_RESULT(no)
	)

        AC_MSG_CHECKING(for encode_fh member in export_operations in kernel)
	AC_TRY_COMPILE([
	    #define __KERNEL__
	    #include <linux/exportfs.h>
	    ], [
	    struct export_operations x;
	    x.encode_fh = NULL;
	    ],
	    AC_MSG_RESULT(yes)
	    AC_DEFINE(HAVE_ENCODEFH_EXPORT_OPERATIONS, 1, Define if export_operations has an encode_fh member),
	    AC_MSG_RESULT(no)
	)

	dnl Using -Werror is not an option, because some arches throw lots of
	dnl warnings that would trigger false negatives.  We know that the
	dnl change to the releasepage() function signature was accompanied by
	dnl a similar change to the exported function try_to_release_page(),
	dnl and that one we can check without using -Werror.  The test fails
	dnl unless the previous declaration was identical to the one we suggest
	dnl below.  New kernels use gfp_t, not int.
	AC_MSG_CHECKING(for second arg type int in address_space_operations releasepage)
	AC_TRY_COMPILE([
	    #define __KERNEL__
	    #include <linux/buffer_head.h>
	    extern int try_to_release_page(struct page *page, int gfp_mask);
	    ], [],
	    AC_MSG_RESULT(yes)
	    AC_DEFINE(HAVE_INT_ARG2_ADDRESS_SPACE_OPERATIONS_RELEASEPAGE, 1, Define if sceond argument to releasepage in address_space_operations is type int),
	    AC_MSG_RESULT(no)
	)

	dnl Similar logic for the follow_link member in inode_operations.  New
	dnl kernels return a void *, not int.
	AC_MSG_CHECKING(for int return in inode_operations follow_link)
	AC_TRY_COMPILE([
	    #define __KERNEL__
	    #include <linux/fs.h>
	    extern int page_follow_link_light(struct dentry *,
	                                      struct nameidata *);
	    ], [],
	    AC_MSG_RESULT(yes)
	    AC_DEFINE(HAVE_INT_RETURN_INODE_OPERATIONS_FOLLOW_LINK, 1, Define if return value from follow_link in inode_operations is type int),
	    AC_MSG_RESULT(no)
	)

	dnl kmem_cache_destroy function may return int only on pre 2.6.19 kernels
	dnl else it returns a void.
	AC_MSG_CHECKING(for int return in kmem_cache_destroy)
	AC_TRY_COMPILE([
	    #define __KERNEL__
	    #include <linux/slab.h>
	    extern int kmem_cache_destroy(kmem_cache_t *);
	    ], [],
	    AC_MSG_RESULT(yes)
	    AC_DEFINE(HAVE_INT_RETURN_KMEM_CACHE_DESTROY, 1, Define if return value from kmem_cache_destroy is type int),
	    AC_MSG_RESULT(no)
	)

	dnl more 2.6 api changes.  return type for the invalidatepage
	dnl address_space_operation is 'void' in new kernels but 'int' in old
	dnl I had to turn on -Werror for this test because i'm not sure how
	dnl else to make dnl "initialization from incompatible pointer type"
	dnl fail.  
	AC_MSG_CHECKING(for older int return in invalidatepage)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		], 
                [
			struct address_space_operations aso;

			int ret;
			struct page * page = NULL;
			unsigned long offset;

			ret = aso.invalidatepage(page, offset);
		],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INT_RETURN_ADDRESS_SPACE_OPERATIONS_INVALIDATEPAGE, 1, Define if return type of invalidatepage should be int),
		AC_MSG_RESULT(NO)
		)

	dnl In 2.6.18.1 and newer, including <linux/config.h> will throw off a
	dnl warning 
	tmp_cflags=${CFLAGS}
	CFLAGS="${CFLAGS} -Werror"
	AC_MSG_CHECKING(for warnings when including linux/config.h)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/config.h>
		], [], 
		AC_MSG_RESULT(no)
		AC_DEFINE(HAVE_NOWARNINGS_WHEN_INCLUDING_LINUX_CONFIG_H, 1, Define if including linux/config.h gives no warnings),
		AC_MSG_RESULT(yes)
	)
	CFLAGS=$tmp_cflags

	AC_MSG_CHECKING(for compat_ioctl member in file_operations structure)
	AC_TRY_COMPILE([
	    #define __KERNEL__
	    #include <linux/fs.h>
		 ], [
		 struct file_operations filop = {
				.compat_ioctl = NULL
		 };
	    ],
	    AC_MSG_RESULT(yes)
		 AC_DEFINE(HAVE_COMPAT_IOCTL_HANDLER, 1, Define if there exists a compat_ioctl member in file_operations),
	    AC_MSG_RESULT(no)
	    )

	dnl Gives wrong answer if header is missing; don't try then.
	if test x$ac_cv_header_linux_ioctl32_h = xyes ; then
	AC_MSG_CHECKING(for register_ioctl32_conversion kernel exports)
	dnl if this test passes, the kernel does not have it
	dnl if this test fails, the kernel has it defined
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/kernel.h>
		#include <linux/ioctl32.h>
		int register_ioctl32_conversion(void)
		{
			return 0;
		}
	], [],
		AC_MSG_RESULT(no),
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_REGISTER_IOCTL32_CONVERSION, 1, Define if kernel has register_ioctl32_conversion),
	)
	fi

	AC_MSG_CHECKING(for int return value of kmem_cache_destroy)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/slab.h>
		], [
		int i = kmem_cache_destroy(NULL);
		],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KMEM_CACHE_DESTROY_INT_RETURN, 1, Define if kmem_cache_destroy returns int),
		AC_MSG_RESULT(no)
	)

	dnl As of 2.6.19, combined readv/writev into aio_read and aio_write
	dnl functions.  Detect this by not finding a readv member.
	AC_MSG_CHECKING(for combined file_operations readv and aio_read)
	AC_TRY_COMPILE([
	    #define __KERNEL__
	    #include <linux/fs.h>
		 ], [
		 struct file_operations filop = {
			.readv = NULL
		 };
	    ],
	    AC_MSG_RESULT(no),
	    AC_MSG_RESULT(yes)
	    AC_DEFINE(HAVE_COMBINED_AIO_AND_VECTOR, 1, Define if struct file_operations has combined aio_read and readv functions),
	    )

	dnl Check for kzalloc
	AC_MSG_CHECKING(for kzalloc)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/slab.h>
	], [
		void * a;
		a = kzalloc(1024, GFP_KERNEL);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_KZALLOC, 1, Define if kzalloc exists),
	AC_MSG_RESULT(no)
	)

	dnl Check for two arg register_sysctl_table()
	AC_MSG_CHECKING(for two arguments to register_sysctl_table)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/sysctl.h>
		#include <linux/proc_fs.h>
	], [
		register_sysctl_table(NULL, 0);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_TWO_ARG_REGISTER_SYSCTL_TABLE, 1, Define if register_sysctl_table takes two arguments),
	AC_MSG_RESULT(no)
	)

	dnl FS_IOC_GETFLAGS and FS_IOC_SETFLAGS appeared 
	dnl somewhere around 2.6.20.1 as generic versions of fs-specific flags
	AC_MSG_CHECKING(for generic FS_IOC ioctl flags)
	AC_TRY_COMPILE([
	    #define __KERNEL__
	    #include <linux/fs.h>
	], [
	    int flags = FS_IOC_GETFLAGS;
	],
	AC_MSG_RESULT(yes),
	AC_DEFINE(HAVE_NO_FS_IOC_FLAGS, 1, Define if FS_IOC flags missing from fs.h)
	AC_MSG_RESULT(no)
	)

	dnl old linux kernels define struct page with a 'count' member, whereas
	dnl other kernels (since at least 2.6.20) define struct page with a
	dnl '_count'
	AC_MSG_CHECKING(for obsolete struct page count without underscore)
	AC_TRY_COMPILE([
	    #define __KERNEL__
	    #include <linux/mm.h>
	], [
	    struct page *p;
	    int foo;
	    foo = atomic_read(&(p)->count);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_OBSOLETE_STRUCT_PAGE_COUNT_NO_UNDERSCORE, 1, Define if struct page defines a count member without leading underscore),
	AC_MSG_RESULT(no)
	)

	dnl old linux kernels do not have class_create and related functions
        dnl
        dnl check for class_device_destroy() to weed out RHEL4 kernels that
        dnl have some class functions but not others
	AC_MSG_CHECKING(if kernel has device classes)
	AC_TRY_COMPILE([
	    #define __KERNEL__
	    #include <linux/device.h>
	], [
	    class_device_destroy(NULL, "pvfs2")
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_KERNEL_DEVICE_CLASSES, 1, Define if kernel has device classes),
	AC_MSG_RESULT(no)
	)

	dnl 2.6.23 removed the destructor parameter from kmem_cache_create
	AC_MSG_CHECKING(for destructor param to kmem_cache_create)
	AC_TRY_COMPILE([
	    #define __KERNEL__
	    #include <linux/slab.h>
	], [
	   kmem_cache_create("config-test", 0, 0, 0, NULL, NULL);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_KMEM_CACHE_CREATE_DESTRUCTOR_PARAM, 1, [Define if kernel kmem_cache_create has destructor param]),
	AC_MSG_RESULT(no)
	)

        dnl 2.6.27 changed the constructor parameter signature of
	dnl kmem_cache_create.  Check for this newer one-param style
        dnl If they don't match, gcc complains about
	dnl passing argument ... from incompatible pointer type, hence the
	dnl need for the -Werror.  Note that the next configure test will
        dnl determine if we have a two param constructor or not.
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for one-param kmem_cache_create constructor)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/kernel.h>
		#include <linux/slab.h>
		void ctor(void *req)
		{
		}
	], [
		kmem_cache_create("config-test", 0, 0, 0, ctor);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_KMEM_CACHE_CREATE_CTOR_ONE_PARAM, 1, [Define if kernel kmem_cache_create constructor has newer-style one-parameter form]),
	AC_MSG_RESULT(no)
	)
	CFLAGS=$tmp_cflags

        dnl 2.6.27 changed the parameter signature of
	dnl inode_operations->permission.  Check for this newer two-param style
        dnl If they don't match, gcc complains about
	dnl passing argument ... from incompatible pointer type, hence the
	dnl need for the -Werror and -Wall.
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror -Wall"
	AC_MSG_CHECKING(for two param permission)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/kernel.h>
		#include <linux/slab.h>
		#include <linux/fs.h>
		#include <linux/namei.h>
		int ctor(struct inode *i, int a)
		{
			return 0;
		}
		struct inode_operations iop = {
			.permission = ctor,
		};
	], [
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_TWO_PARAM_PERMISSION, 1, [Define if kernel's inode_operations has two parameters permission function]),
	AC_MSG_RESULT(no)
	)
	CFLAGS=$tmp_cflags


        dnl 2.6.24 changed the constructor parameter signature of
	dnl kmem_cache_create.  Check for this newer two-param style and
	dnl if not, assume it is old.  Note we can get away with just
	dnl struct kmem_cache (and not kmem_cache_t) as that change happened
	dnl in older kernels.  If they don't match, gcc complains about
	dnl passing argument ... from incompatible pointer type, hence the
	dnl need for the -Werror.
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for two-param kmem_cache_create constructor)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/kernel.h>
		#include <linux/slab.h>
		void ctor(struct kmem_cache *cachep, void *req)
		{
		}
	], [
		kmem_cache_create("config-test", 0, 0, 0, ctor);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_KMEM_CACHE_CREATE_CTOR_TWO_PARAM, 1, [Define if kernel kmem_cache_create constructor has new-style two-parameter form]),
	AC_MSG_RESULT(no)
	)
	CFLAGS=$tmp_cflags

	AC_MSG_CHECKING(if kernel address_space struct has a spin_lock field named page_lock)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
	], [
		struct address_space as;
		spin_lock(&as.page_lock);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_SPIN_LOCK_PAGE_ADDR_SPACE_STRUCT, 1, [Define if kernel address_space struct has a spin_lock member named page_lock instead of rw_lock]),
	AC_MSG_RESULT(no)
	)

        tmp_cflags=$CFLAGS
        CFLAGS="$CFLAGS -Werror"
        AC_MSG_CHECKING(if kernel address_space struct has a rwlock_t field named tree_lock)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
	], [
		struct address_space as;
		read_lock(&as.tree_lock);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_RW_LOCK_TREE_ADDR_SPACE_STRUCT, 1, [Define if kernel address_space struct has a rw_lock_t member named tree_lock]),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

        tmp_cflags=$CFLAGS
        CFLAGS="$CFLAGS -Werror"
        AC_MSG_CHECKING(if kernel address_space struct has a spinlock_t field named tree_lock)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
	], [
		struct address_space as;
		spin_lock(&as.tree_lock);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_SPIN_LOCK_TREE_ADDR_SPACE_STRUCT, 1, [Define if kernel address_space struct has a spin_lock_t member named tree_lock]),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

	AC_MSG_CHECKING(if kernel address_space struct has a priv_lock field - from RT linux)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
	], [
		struct address_space as;
		spin_lock(&as.priv_lock);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_RT_PRIV_LOCK_ADDR_SPACE_STRUCT, 1, [Define if kernel address_space struct has a spin_lock for private data instead of rw_lock -- used by RT linux]),
	AC_MSG_RESULT(no)
	)

	AC_MSG_CHECKING(if kernel defines mapping_nrpages macro - from RT linux)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
	], [
		struct address_space idata;
		int i = mapping_nrpages(&idata);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_MAPPING_NRPAGES_MACRO, 1, [Define if kernel defines mapping_nrpages macro -- defined by RT linux]),
	AC_MSG_RESULT(no)
	)

	dnl Starting with 2.6.25-rc1, .read_inode goes away.
	AC_MSG_CHECKING(if kernel super_operations contains read_inode field)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
	], [
		struct super_operations sops;
		sops.read_inode(NULL);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_READ_INODE, 1, [Define if kernel super_operations contains read_inode field]),
	AC_MSG_RESULT(no)
	)

	dnl Starting with 2.6.26, drop_inode and put_inode go away
	AC_MSG_CHECKING(if kernel super_operations contains drop_inode field)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
	], [
		struct super_operations sops;
		sops.drop_inode(NULL);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_DROP_INODE, 1, [Define if kernel super_operations contains drop_inode field]),
	AC_MSG_RESULT(no)
	)

	dnl Starting with 2.6.26, drop_inode and put_inode go away
	AC_MSG_CHECKING(if kernel super_operations contains put_inode field)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
	], [
		struct super_operations sops;
		sops.put_inode(NULL);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_PUT_INODE, 1, [Define if kernel super_operations contains put_inode field]),
	AC_MSG_RESULT(no)
	)

	dnl older 2.6 kernels don't have MNT_NOATIME
	AC_MSG_CHECKING(if mount.h defines MNT_NOATIME)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/mount.h>
	], [
		int flag = MNT_NOATIME;
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_MNT_NOATIME, 1, [Define if mount.h contains
	MNT_NOATIME flags]),
	AC_MSG_RESULT(no)
	)

	dnl older 2.6 kernels don't have MNT_NODIRATIME
	AC_MSG_CHECKING(if mount.h defines MNT_NODIRATIME)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/mount.h>
	], [
		int flag = MNT_NODIRATIME;
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_MNT_NODIRATIME, 1, [Define if mount.h contains
	MNT_NODIRATIME flags]),
	AC_MSG_RESULT(no)
	)

        dnl newer 2.6 kernels (2.6.28) use d_obtain_alias instead of d_alloc_anon
        AC_MSG_CHECKING(for d_alloc_anon)
        AC_TRY_COMPILE([
                #define __KERNEL__
                #include <linux/dcache.h>
        ], [
                struct inode *i;
                d_alloc_anon(i);
        ],
        AC_MSG_RESULT(yes)
        AC_DEFINE(HAVE_D_ALLOC_ANON, 1, [Define if dcache.h contains 
                  d_alloc_annon]),
        AC_MSG_RESULT(no)
        )

        AC_MSG_CHECKING(for s_dirty in struct super_block)
        AC_TRY_COMPILE([
                #define __KERNEL__
                #include <linux/fs.h>
        ], [
                struct super_block *s;
                list_empty(&s->s_dirty);
        ],
        AC_MSG_RESULT(yes)
        AC_DEFINE(HAVE_SB_DIRTY_LIST, 1, [Define if struct super_block has s_dirty list]),
        AC_MSG_RESULT(no)
        )

        dnl newer 2.6 kernels (2.6.29-ish) use current_fsuid() macro instead
        dnl of accessing task struct fields directly
        tmp_cflags=$CFLAGS
        CFLAGS="$CFLAGS -Werror"
        AC_MSG_CHECKING(for current_fsuid)
        AC_TRY_COMPILE([
                #define __KERNEL__
                #include <linux/sched.h>
                #include <linux/cred.h>
        ], [
                int uid = current_fsuid();
        ],
        AC_MSG_RESULT(yes)
        AC_DEFINE(HAVE_CURRENT_FSUID, 1, [Define if cred.h contains current_fsuid]),
        AC_MSG_RESULT(no)
        )
        CFLAGS=$tmp_cflags

        dnl 2.6.32 added a mandatory name field to the bdi structure
        AC_MSG_CHECKING(if kernel backing_dev_info struct has a name field)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/fs.h>
		#include <linux/backing-dev.h>
	], [
                struct backing_dev_info foo = 
                {
                    .name = "foo"
                };
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_BACKING_DEV_INFO_NAME, 1, [Define if kernel backing_dev_info struct has a name field]),
	AC_MSG_RESULT(no)
	)

        dnl some 2.6 kernels have functions to explicitly initialize bdi structs
        tmp_cflags=$CFLAGS
        CFLAGS="$CFLAGS -Werror"
        AC_MSG_CHECKING(for bdi_init)
        AC_TRY_COMPILE([
                #define __KERNEL__
		#include <linux/fs.h>
		#include <linux/backing-dev.h>
        ], [
                int ret = bdi_init(NULL);
        ],
        AC_MSG_RESULT(yes)
        AC_DEFINE(HAVE_BDI_INIT, 1, [Define if bdi_init function is present]),
        AC_MSG_RESULT(no)
        )
        CFLAGS=$tmp_cflags


	CFLAGS=$oldcflags

])
