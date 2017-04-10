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

        dnl opensuse11.2 32bit only reports the correct include path when in
        dnl specific directories, must be some search path broken-ness? 
        dnl switching to / fixes the problem and shouldn't break others

	NOSTDINCFLAGS="-Werror-implicit-function-declaration -nostdinc -isystem `$CC -print-file-name=include`"

        dnl SuSE and other distros that have a separate kernel obj directory 
        dnl need to have include files from both the obj directory and the
        dnl full source listed in the includes. Kbuild handles this when 
        dnl compiling but the configure checks don't handle this on their own.
        dnl The strategy here is just to set a new variable, lk_src_source,
        dnl when the provided kernel source has a source directory. If it
        dnl doesn't exist just set it lk_src. There may be a cleaner way to do
        dnl this, for now, this appears to do the trick.
        if test -d $lk_src/source; then
            lk_src_source="$lk_src/source"
        else
            lk_src_source=$lk_src
        fi

	CFLAGS="$USR_CFLAGS $NOSTDINCFLAGS -I$lk_src_source/include -I$lk_src_source/include/asm/mach-default -DKBUILD_STR(s)=#s -DKBUILD_BASENAME=KBUILD_STR(empty)  -DKBUILD_MODNAME=KBUILD_STR(empty)"

	dnl kernels > 2.6.32 now use generated/autoconf.h
        dnl look in lk_src for the generated autoconf.h
	if test -f $lk_src/include/generated/autoconf.h ; then
		CFLAGS="$CFLAGS -imacros $lk_src/include/generated/autoconf.h"
	else
		CFLAGS="$CFLAGS -imacros $lk_src/include/linux/autoconf.h"
	fi

        dnl we probably need additional includes if this build is intended
        dnl for a different architecture
	if test -n "${ARCH}" ; then
		CFLAGS="$CFLAGS -I$lk_src_source/arch/${ARCH}/include -I$lk_src_source/arch/${ARCH}/include/asm/mach-default"
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

            CFLAGS="$CFLAGS -I$lk_src_source/arch/${ARCH}/include -I$lk_src_source/arch/${ARCH}/include/asm/mach-default"

	fi

        dnl After the "UAPI header file split" in linux-3.9.7, the
        dnl uapi header file locations need to be added to CFLAGS.
        if test -n "${ARCH}" &&  
           test -d $lk_src_source/arch/${ARCH}/include/uapi; then
             CFLAGS="$CFLAGS -I$lk_src_source/arch/${ARCH}/include/uapi"
             CFLAGS="$CFLAGS -I$lk_src_source/arch/${ARCH}/include/generated/uapi"
        fi
        if test -d $lk_src_source/include/uapi; then
             CFLAGS="$CFLAGS -I$lk_src_source/include/uapi"
        fi

        dnl directories named "generated" under $lk_src are in paths that
        dnl need to be searched for include files
        for i in `find "$lk_src" -follow -name generated`
        do
          addThis="`echo $i | sed 's/generated.*$//'`"
          addThisToo="`echo $i | sed 's/generated.*$/generated/'`"
          CFLAGS="$CFLAGS -I$addThis -I$addThisToo"
        done

	dnl Check for kconfig.h... at some revision levels, many
	dnl tests use IS_ENABLED indirectly through includes... 
	AC_MSG_CHECKING(for kconfig.h) 
	AC_TRY_COMPILE([
		#include <linux/kconfig.h>
	], [
		;
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_KCONFIG, 1, Define if kconfig.h exists),
	AC_MSG_RESULT(no)
	)

	dnl Check for vmtruncate. vmtruncate has been deprecated for
	dnl a while, it is gone by 3.8. 
	dnl "The whole truncate sequence needs to be implemented in ->setattr"
	dnl      ./Documentation/filesystems/porting
	dnl google "__kfree_rcu breaks third-party kernel code" to learn
	dnl why -O2 is needed in CFLAGS for this test...
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -O2"
	AC_MSG_CHECKING(for vmtruncate) 
	AC_TRY_COMPILE([
		#define __KERNEL__
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
		#include <linux/fs.h>
		#include <linux/mm.h>
	], [
                struct iattr *iattr;
                struct inode *inode;
		vmtruncate(inode, iattr->ia_size);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_VMTRUNCATE, 1, Define if vmtruncate exists),
	AC_MSG_RESULT(no)
	)
	CFLAGS=$tmp_cflags

        dnl in 3.8 a "user namespace" parameter was added to 
        dnl posix_acl_from_xattr... 
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -O2"
	AC_MSG_CHECKING(for namespace parameter in posix_acl_from_xattr) 
	AC_TRY_COMPILE([
		#define __KERNEL__
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
		#include <linux/fs.h>
		#include <linux/posix_acl_xattr.h>
	], [
		int i;
		char *c;
		posix_acl_from_xattr(&init_user_ns, c, i);

	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_POSIX_ACL_USER_NAMESPACE, 1, Define if the user namespace has been added to posix_acl_from_xattr and posix_acl_to_xattr),
	AC_MSG_RESULT(no)
	)
	CFLAGS=$tmp_cflags

        dnl in 2.6.40 (maybe .39 too) inclusion of linux/fs.h breaks unless
        dnl optimization flag of some sort is set. To complicate matters 
        dnl checks in earlier versions break when optimization is turned on.
        need_optimize_flag=0
	AC_MSG_CHECKING(for sanity of linux/fs.h include)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
	], [],
		AC_MSG_RESULT(yes),
		AC_MSG_RESULT(no)
                need_optimize_flag=1,
	)
        if test $need_optimize_flag -eq 1; then
            CFLAGS="-Os $CFLAGS"
        fi

    dnl by 3.4 create's third argument changed from int to umode_t.
    tmp_cflags=$CFLAGS
    CFLAGS="$CFLAGS -Werror"
    AC_MSG_CHECKING([if kernel inode ops create uses umode_t])
	AC_TRY_COMPILE(
        [
		    #define __KERNEL__
            #ifdef HAVE_KCONFIG
            #include <linux/kconfig.h>
            #endif
		    #include <linux/fs.h>
            extern int mycreate(struct inode *,
                                struct dentry *,
                                umode_t,
                                struct nameidata *);
        ], [
			static struct inode_operations in_op = {
				  .create = mycreate
			};
		], [
            AC_MSG_RESULT(yes)
            AC_DEFINE(PVFS_KMOD_CREATE_USES_UMODE_T, 1,
                [Define if kernel inode ops create uses umode_t not int])
        ], [
            AC_MSG_RESULT(no)
        ]
	)
    CFLAGS=$tmp_cflags


    dnl by 3.4 mkdir's third argument changed from int to umode_t.
    tmp_cflags=$CFLAGS
    CFLAGS="$CFLAGS -Werror"
    AC_MSG_CHECKING([if kernel inode ops mkdir uses umode_t])
	AC_TRY_COMPILE(
        [
		    #define __KERNEL__
            #ifdef HAVE_KCONFIG
            #include <linux/kconfig.h>
            #endif
		    #include <linux/fs.h>
            extern int mymkdir(struct inode *,
                                struct dentry *,
                                umode_t);
        ], [
			static struct inode_operations in_op = {
				  .mkdir = mymkdir
			};
		], [
            AC_MSG_RESULT(yes)
            AC_DEFINE(PVFS_KMOD_MKDIR_USES_UMODE_T, 1,
                [Define if kernel inode ops mkdir uses umode_t not int])
        ], [
            AC_MSG_RESULT(no)
        ]
	)
    CFLAGS=$tmp_cflags


    dnl by 3.4 mknod's third argument changed from int to umode_t.
    tmp_cflags=$CFLAGS
    CFLAGS="$CFLAGS -Werror"
    AC_MSG_CHECKING([if kernel inode ops mknod uses umode_t])
	AC_TRY_COMPILE(
        [
		    #define __KERNEL__
            #ifdef HAVE_KCONFIG
            #include <linux/kconfig.h>
            #endif
		    #include <linux/fs.h>
            extern int mymknod(struct inode *,
                                struct dentry *,
                                umode_t,
                                dev_t);
        ], [
			static struct inode_operations in_op = {
				  .mknod = mymknod
			};
		], [
            AC_MSG_RESULT(yes)
            AC_DEFINE(PVFS_KMOD_MKNOD_USES_UMODE_T, 1,
                [Define if kernel inode ops mknod uses umode_t not int])
        ], [
            AC_MSG_RESULT(no)
        ]
	)
    CFLAGS=$tmp_cflags

    dnl between 3.6 and 3.9 create() lookup() and d_revalidate() lose
    dnl their struct nameidata argument.  d_revalidate seems to be
    dnl handled be we will test for the other two check create.
    dnl Create args for different versions:
    dnl  3.2: (struct inode *,struct dentry *,int, struct nameidata *);
    dnl  3.4: (struct inode *,struct dentry *,umode_t,struct nameidata *)
    dnl  3.6: (struct inode *,struct dentry *, umode_t, bool);
    dnl  a previous test (if kernel inode ops create uses umode_t) sets
    dnl  a define that helps us in this test.
    tmp_cflags=$CFLAGS
    CFLAGS="$CFLAGS -Werror"
    AC_MSG_CHECKING([if kernel inode ops create takes nameidata])
	AC_TRY_COMPILE(
        [
		    #define __KERNEL__
            #ifdef HAVE_KCONFIG
            #include <linux/kconfig.h>
            #endif
		    #include <linux/fs.h>
            extern int mycreate(struct inode *,
                                struct dentry *,
		    #ifdef PVFS_KMOD_CREATE_USES_UMODE_T
		                umode_t mode,
		    #else
                                int,
		    #endif
                                struct nameidata *);
        ], [
			static struct inode_operations in_op = {
				  .create = mycreate
			};
		], [
            AC_MSG_RESULT(yes)
            AC_DEFINE(PVFS_KMOD_CREATE_TAKES_NAMEIDATA, 1,
                [Define if kernel inode ops create takes nameidata not bool])
        ], [
            AC_MSG_RESULT(no)
        ]
	)
    CFLAGS=$tmp_cflags

    dnl check lookup
    tmp_cflags=$CFLAGS
    CFLAGS="$CFLAGS -Werror"
    AC_MSG_CHECKING([if kernel inode ops lookup takes nameidata])
	AC_TRY_COMPILE(
        [
		    #define __KERNEL__
            #ifdef HAVE_KCONFIG
            #include <linux/kconfig.h>
            #endif
		    #include <linux/fs.h>
            extern struct dentry *mylookup(struct inode *,
                                           struct dentry *,
                                           struct nameidata *);
        ], [
			static struct inode_operations in_op = {
				  .lookup = mylookup
			};
		], [
            AC_MSG_RESULT(yes)
            AC_DEFINE(PVFS_KMOD_LOOKUP_TAKES_NAMEIDATA, 1,
                [Define if kernel inode ops lookup takes nameidata])
        ], [
            AC_MSG_RESULT(no)
        ]
	)
    CFLAGS=$tmp_cflags

    dnl check revalidate
    tmp_cflags=$CFLAGS
    CFLAGS="$CFLAGS -Werror"
    AC_MSG_CHECKING([for if kernel dentry ops d_revalidate takes nameidata])
	AC_TRY_COMPILE(
        [
		    #define __KERNEL__
            #ifdef HAVE_KCONFIG
            #include <linux/kconfig.h>
            #endif
		    #include <linux/fs.h>
		    #include <linux/dcache.h>
            extern int myreval(struct dentry *, struct nameidata *);
        ], [
			static struct dentry_operations dc_op = {
				  .d_revalidate = myreval
			};
		], [
            AC_MSG_RESULT(yes)
            AC_DEFINE(PVFS_KMOD_D_REVALIDATE_TAKES_NAMEIDATA, 1,
                [Define if kernel dentry ops d_revalidate takes nameidata])
        ], [
            AC_MSG_RESULT(no)
        ]
	)
    CFLAGS=$tmp_cflags

    dnl kernel 3.6-3.9 added get_acl as a method rather than using
    dnl check_acl passed into generic_permissions
    AC_MSG_CHECKING([if kernel inode ops has get_acl ])
	AC_TRY_COMPILE(
        [
		    #define __KERNEL__
            #ifdef HAVE_KCONFIG
            #include <linux/kconfig.h>
            #endif
		    #include <linux/fs.h>
        ], [
			static struct inode_operations in_op = {
				  .get_acl = NULL
			};
		], [
            AC_MSG_RESULT(yes)
            AC_DEFINE(PVFS_KMOD_HAVE_GET_ACL, 1,
                [Define if kernel inode ops has get_acl])
        ], [
            AC_MSG_RESULT(no)
        ]
	)

        dnl if there are two different include paths (lk_src/include and 
        dnl lk_src_source/include) add the lk_src/include path to the CFLAGS
        dnl here.
        if test "$lk_src" != "$lk_src_source"; then
            CFLAGS="$CFLAGS -I$lk_src/include"
        fi

dnl newer 3.3 kernels and above use d_make_root instead of d_alloc_root
        AC_MSG_CHECKING(for d_alloc_root)
        AC_TRY_COMPILE(
        [
                #define __KERNEL__
                #ifdef HAVE_KCONFIG
                #include <linux/kconfig.h>
                #endif
                #include <linux/fs.h>
        ], [
                struct inode  *root_inode;
		        struct dentry *root_dentry;
                root_dentry=d_alloc_root(root_inode);
        ], [
                AC_MSG_RESULT(yes)
                AC_DEFINE(HAVE_D_ALLOC_ROOT, 1, [Define if kernel defines
                          d_alloc_root])
        ], [
                AC_MSG_RESULT(no)
        ]
        )

	AC_MSG_CHECKING(for i_size_write in kernel)
	dnl if this test passes, the kernel does not have it
	dnl if this test fails, the kernel already defined it
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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

        AC_MSG_CHECKING(for backing_dev_info in struct address_space)
	dnl backing_dev_info was removed from struct address_space
	dnl around 4.1
        AC_TRY_COMPILE([
                #define __KERNEL__
                #ifdef HAVE_KCONFIG
                #include <linux/kconfig.h>
                #endif
                #include <linux/fs.h>
                static struct address_space foo = {
                         .backing_dev_info = NULL,
                };
        ], [],
                AC_MSG_RESULT(yes)
                AC_DEFINE(BACKING_DEV_IN_ADDR_SPACE,
                          1,
                          Define if struct  backing_dev_info is defined),
                AC_MSG_RESULT(no)
        )

	AC_MSG_CHECKING(for iget5_locked function in kernel)
	dnl if this test passes, the kernel does not have it
	dnl if this test fails, the kernel already defined it
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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

        AC_MSG_CHECKING(for d_set_d_op function in kernel)
        dnl if this test passes, the kernel does not have it
        dnl if this test fails, the kernel already defined it
        AC_TRY_COMPILE([
                #define __KERNEL__
                #ifdef HAVE_KCONFIG
                #include <linux/kconfig.h>
                #endif
                #include <linux/fs.h>
                loff_t d_set_d_op(struct dentry *dentry, const struct dentry_operations *op)
                {
                        return 0;
                }
        ], [],
                AC_MSG_RESULT(no),
                AC_MSG_RESULT(yes)
                AC_DEFINE(HAVE_D_SET_D_OP, 1, Define if kernel has d_set_d_op),
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
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
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
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
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
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
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
            #ifdef HAVE_KCONFIG
            #include <linux/kconfig.h>
            #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
		static struct super_operations sop = {
		    .get_fs_key = NULL,
		};
	], [],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GET_FS_KEY_SUPER_OPERATIONS, 1, Define if struct super_operations in kernel has get_fs_key callback),
		AC_MSG_RESULT(no)
	)
	
	dnl checking if we have a  readdir callback in file_operations
	AC_MSG_CHECKING(for readdir member in file_operations structure)
	AC_TRY_COMPILE([
	    #define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
	    #include <linux/fs.h>
		 ], [
		 struct file_operations filop = {
				.readdir = NULL
		 };
	    ],
	    AC_MSG_RESULT(yes)
		 AC_DEFINE(HAVE_READDIR_FILE_OPERATIONS, 1, Define if struct file_operations in kernel has readdir callback),
	    AC_MSG_RESULT(no)
	    )

	dnl checking if we have a readdirplus callback in file_operations
	AC_MSG_CHECKING(for readdirplus member in file_operations structure)
	AC_TRY_COMPILE([
	    #define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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

		AC_MSG_CHECKING(for kiocbSetCancelled)
		dnl kiocbSetCancelled is gone by 3.11...
		AC_TRY_COMPILE([
			#define __KERNEL__
			#include <linux/wait.h>
			#include <linux/aio.h>
		],
                [
                        struct kiocb *iocb;
                        kiocbSetCancelled(iocb);
                ],
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_KIOCBSETCANCELLED, 1, Define if kiocbSetCancelled exists),
			AC_MSG_RESULT(no)
		)

		AC_MSG_CHECKING(for atomic ki_users)
		dnl ki_users member in struct kiocb is atomic_t (not int)
		dnl in 3.11
	        tmp_cflags=$CFLAGS
	        CFLAGS="$CFLAGS -Werror"
		AC_TRY_COMPILE([
			#define __KERNEL__
			#include <linux/wait.h>
			#include <linux/aio.h>
		],
                [
                        struct kiocb *iocb;
                        atomic_dec(&iocb->ki_users);
                ],
			AC_MSG_RESULT(yes)
			AC_DEFINE(KI_USERS_ATOMIC, 1, Define if ki_users is atomic),
			AC_MSG_RESULT(no)
		)
		tmp_cflags=$CFLAGS

		AC_MSG_CHECKING(for aio_put_req returns int)
		dnl aio_put_req is void by 3.11
		AC_TRY_COMPILE([
			#define __KERNEL__
			#include <linux/wait.h>
			#include <linux/aio.h>
		],
                [
                        struct kiocb *iocb;
                        int r;
                        r = aio_put_req(iocb);
                ],
			AC_MSG_RESULT(yes)
			AC_DEFINE(AIO_PUT_REQ_RETURNS_INT, 1, Define if aio_put_req returns int),
			AC_MSG_RESULT(no)
		)

		AC_MSG_CHECKING(for kiocb_set_cancel_fn)
		dnl by 3.11 there's a function for setting the ki_cancel
		dnl member in a struct of type kiocb.
		AC_TRY_COMPILE([
			#define __KERNEL__
			#include <linux/wait.h>
			#include <linux/aio.h>
		],
                [
                        struct kiocb *iocb;
                        int (*aio_cancel)(struct kiocb *, struct io_event *);
                        kiocb_set_cancel_fn(iocb, aio_cancel);
                ],
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_KIOCB_SET_CANCEL_FN, 1, Define if we have kiocb_set_cancel_fn),
			AC_MSG_RESULT(no)
		)

		tmp_cflags=$CFLAGS
		dnl if this test passes, the signature of aio_read has changed to the new one 
		CFLAGS="$CFLAGS -Werror"
		AC_MSG_CHECKING(for new prototype of aio_read callback of file_operations structure)
		AC_TRY_COMPILE([
			#define __KERNEL__
            #ifdef HAVE_KCONFIG
            #include <linux/kconfig.h>
            #endif
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

	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
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
        dnl
        dnl Need to use the second approach because vfs_statfs changes without
        dnl a cooresponding change in statfs in super_operations. I'm not that
        dnl concerned with reliance on Werror since we use it heavily
        dnl throughout these checks
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
                struct super_operations sop;
		int s(struct dentry *de, struct kstatfs *kfs)
		{
			return 0;
		}
		], 
                [
                    sop.statfs = s;
                ],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DENTRY_STATFS_SOP, 1, Define if super_operations statfs has dentry argument),
		AC_MSG_RESULT(no)
	)
	CFLAGS=$tmp_cflags

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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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

	AC_MSG_CHECKING(for get_sb_nodev)
	AC_TRY_COMPILE([
                #define __KERNEL__
                #ifdef HAVE_KCONFIG
                #include <linux/kconfig.h>
                #endif
                #include <linux/fs.h>
                int v_fill_sb(struct super_block *sb, void *data, int s)
                {
                        return 0;
                }
                ],
                [
                        int ret = 0;
                        struct super_block *sb = NULL;
#ifdef HAVE_VFSMOUNT_GETSB
                        ret = get_sb_nodev(NULL, 0, NULL, v_fill_sb, NULL );
#else
                        sb = get_sb_nodev(NULL, 0, NULL, v_fill_sb);
#endif
		], 
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GETSB_NODEV, 1, Define if get_sb_nodev function exists ),
		AC_MSG_RESULT(no)
	)

	AC_MSG_CHECKING(for file_system_type get_sb)
	AC_TRY_COMPILE([
                #define __KERNEL__
                #ifdef HAVE_KCONFIG
                #include <linux/kconfig.h>
                #endif
                #include <linux/fs.h>
                ],
                [
                    struct file_system_type f;
                    f.get_sb = NULL;
		], 
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FSTYPE_GET_SB, 1, Define if only filesystem_type has get_sb),
		AC_MSG_RESULT(no)
	)

	AC_MSG_CHECKING(for f_dentry in file struct)
        dnl the define for f_dentry eventually goes missing from files struct.
	AC_TRY_COMPILE([
                #define __KERNEL__
                #ifdef HAVE_KCONFIG
                #include <linux/kconfig.h>
                #endif
                #include <linux/fs.h>
                ],
                [
                    struct file f;
                    f.f_dentry = NULL;
		], 
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_F_DENTRY, 1, Define if file struct has f_dentry),
		AC_MSG_RESULT(no)
	)

	AC_MSG_CHECKING(for file_system_type mount exclusively)
	AC_TRY_COMPILE([
                #define __KERNEL__
                #ifdef HAVE_KCONFIG
                #include <linux/kconfig.h>
                #endif
                #include <linux/fs.h>
                ],
                [
#ifdef HAVE_FSTYPE_GET_SB
                    assert(0);
#else
                    struct file_system_type f;
                    f.mount = NULL;
#endif
		], 
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FSTYPE_MOUNT_ONLY, 1, Define if only filesystem_type has mount and HAVE_FSTYPE_GET_SB is false),
		AC_MSG_RESULT(no)
	)

	AC_MSG_CHECKING(for xattr support in kernel)
	dnl if this test passes, the kernel has it
	dnl if this test fails, the kernel does not have it
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
		 #ifdef HAVE_KCONFIG
		 #include <linux/kconfig.h>
		 #endif
		 #include <linux/fs.h>
		 #ifdef HAVE_XATTR 
		 #include <linux/xattr.h> 
		 #endif
		 ] )

	AC_CHECK_HEADERS([linux/posix_acl_xattr.h], [], [], 
		[#define __KERNEL__
		 #ifdef HAVE_KCONFIG
		 #include <linux/kconfig.h>
		 #endif
		 #include <linux/fs.h>
		 #ifdef HAVE_XATTR 
		 #include <linux/xattr.h> 
		 #endif
		 ] )

	dnl linux-2.6.11 had xattr_acl.h, but 2.6.12 did not!
	AC_CHECK_HEADERS([linux/xattr_acl.h], [], [], 
		[#define __KERNEL__
		 #ifdef HAVE_KCONFIG
		 #include <linux/kconfig.h>
		 #endif
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
		 #ifdef HAVE_KCONFIG
		 #include <linux/kconfig.h>
		 #endif
		 #include <linux/compat.h>
		 ] )
	AC_CHECK_HEADERS([linux/syscalls.h], [], [], 
		[#define __KERNEL__
		 #ifdef HAVE_KCONFIG
		 #include <linux/kconfig.h>
		 #endif
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

        dnl no bkl, no need for smp_lock.h
        AC_CHECK_HEADER([linux/smp_lock.h], [], [],
                [#define __KERNEL__
                 #include <linux/smp_lock.h>
                ])

	AC_MSG_CHECKING(for generic_file_readv api in kernel)
	dnl if this test passes, the kernel does not have it
	dnl if this test fails, the kernel has it defined with a different
	dnl signature!  deliberately, the signature for this method has been
	dnl changed for it to give a compiler error.

	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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

        dnl generic_permission in < 2.6.38 has three parameters
	dnl tmp_cflags=$CFLAGS
	dnl CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for three-param generic_permission)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
                struct inode *f;
	], 
	[ 
	        generic_permission(f, 0, NULL);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_THREE_PARAM_GENERIC_PERMISSION, 1, [Define if generic_permission takes three parameters]),
	AC_MSG_RESULT(no)
	)

        dnl generic_permission in >= 2.6.38 and 3.0.x has four parameters
	dnl tmp_cflags=$CFLAGS
	dnl CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for four-param generic_permission)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
                struct inode *f;
	], 
	[ 
	        generic_permission(f, 0, 0, NULL);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_FOUR_PARAM_GENERIC_PERMISSION, 1, [Define if generic_permission takes four parameters]),
	AC_MSG_RESULT(no)
	)

        dnl generic_permission in >= 3.1.x has two parameters
	dnl tmp_cflags=$CFLAGS
	dnl CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for two-param generic_permission)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
                struct inode *f;
	], 
	[ 
	        generic_permission(f, 0);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_TWO_PARAM_GENERIC_PERMISSION, 1, [Define if generic_permission takes two parameters]),
	AC_MSG_RESULT(no)
	)

        dnl set_nlink is defined in 3.2.x 
	dnl tmp_cflags=$CFLAGS
	dnl CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for set_nlink)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
		struct inode *i;
	], 
	[
		set_nlink(i, 0);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_I_SET_NLINK, 1, [Define if set_nlink exists]),
	AC_MSG_RESULT(no)
	)

        dnl inc_nlink is defined in 3.2.x 
	dnl tmp_cflags=$CFLAGS
	dnl CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for inc_nlink)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
		struct inode *i;
	], 
	[
		inc_nlink(i);
	], 
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_I_INC_NLINK, 1, [Define if inc_nlink exists]),
	AC_MSG_RESULT(no)
	)

        dnl drop_nlink is defined in 3.2.x 
	dnl tmp_cflags=$CFLAGS
	dnl CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for drop_nlink)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
		struct inode *i;
	], 
	[
		drop_nlink(i);
	], 
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_I_DROP_NLINK, 1, [Define if drop_nlink exists]),
	AC_MSG_RESULT(no)
	)

        dnl clear_nlink is defined in 3.2.x 
	dnl tmp_cflags=$CFLAGS
	dnl CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for clear_nlink)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
		struct inode *i;
	], 
	[
		clear_nlink(i);
	], 
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_I_CLEAR_NLINK, 1, [Define if clear_nlink exists]),
	AC_MSG_RESULT(no)
	)

        dnl check for posix_acl_equiv_mode umode_t type  
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for posix_acl_equiv_mode umode_t)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
		#include <linux/fs.h>
		#include <linux/posix_acl.h>
		struct posix_acl *acl;
		umode_t mode = 0;
	],
	[
		posix_acl_equiv_mode(acl, &mode);
	], 
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_POSIX_ACL_EQUIV_MODE_UMODE_T, 1, [Define if posix_acl_equiv_mode accepts umode_t type]),
	AC_MSG_RESULT(no)
	)
	CFLAGS=$tmp_cflags

        dnl check for three arg posix_acl_create
	dnl tmp_cflags=$CFLAGS
	dnl CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for three arg posix_acl_create)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
		#include <linux/fs.h>
		#include <linux/posix_acl.h>
		struct posix_acl *acl;
		umode_t mode = 0;
	],
	[
		posix_acl_create(&acl, GFP_KERNEL, &mode);
	], 
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_POSIX_ACL_CREATE_3, 1, [Define if posix_acl_create has three arguments]),
	AC_MSG_RESULT(no)
	)
	CFLAGS=$tmp_cflags

        dnl check for four arg posix_acl_create
	dnl tmp_cflags=$CFLAGS
	dnl CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for four arg posix_acl_create)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
		#include <linux/fs.h>
		#include <linux/posix_acl.h>
		struct inode *dir;
		struct posix_acl *default_acl;
		struct posix_acl *acl;
		umode_t mode = 0;
	],
	[
		posix_acl_create(dir, &mode, &default_acl, &acl);
	], 
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_POSIX_ACL_CREATE_4, 1, [Define if posix_acl_create has four arguments]),
	AC_MSG_RESULT(no)
	)
	CFLAGS=$tmp_cflags

        dnl check for three arg posix_acl_chmod
	dnl tmp_cflags=$CFLAGS
	dnl CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for three argument posix_acl_chmod)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
		#include <linux/fs.h>
		#include <linux/posix_acl.h>
		struct posix_acl *acl;
		struct inode *inode;
		umode_t mode = 0;
	],
	[
		posix_acl_chmod(&acl, GFP_KERNEL, inode->i_mode );
	], 
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_POSIX_ACL_CHMOD_3, 1, [Define if posix_acl_chmod exists and has three arguments]),
	AC_MSG_RESULT(no)
	)
	CFLAGS=$tmp_cflags

        dnl check for two arg posix_acl_chmod
	dnl tmp_cflags=$CFLAGS
	dnl CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for two argument posix_acl_chmod)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
		#include <linux/fs.h>
		#include <linux/posix_acl.h>
		struct inode *inode;
	],
	[
		posix_acl_chmod(inode, inode->i_mode);
	], 
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_POSIX_ACL_CHMOD_2, 1, [Define if posix_acl_chmod exists and has two arguments]),
	AC_MSG_RESULT(no)
	)
	CFLAGS=$tmp_cflags

        dnl check for posix_acl_clone
	dnl tmp_cflags=$CFLAGS
	dnl CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for posix_acl_clone)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
		#include <linux/fs.h>
		#include <linux/posix_acl.h>
		struct posix_acl *acl;
	],
	[
		posix_acl_clone(acl, GFP_KERNEL);
	], 
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_POSIX_ACL_CLONE, 1, [Define if posix_acl_clone exists]),
	AC_MSG_RESULT(no)
	)
	CFLAGS=$tmp_cflags

        dnl check for fsync with loff_t  
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for fsync with loff_t)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>

		int my_fsync(struct file *, loff_t, loff_t, int);

		int my_fsync(struct file *f, loff_t start, loff_t end, int datasync)
		{
		}
	],
	[
		struct file_operations fop;
		
		fop.fsync = my_fsync;
	], 
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_FSYNC_LOFF_T_PARAMS, 1, [Define if fsync has loff_t params]),
	AC_MSG_RESULT(no)
	)
	CFLAGS=$tmp_cflags


	AC_MSG_CHECKING(for generic_getxattr api in kernel)
	dnl if this test passes, the kernel does not have it
	dnl if this test fails, the kernel has it defined
	AC_TRY_COMPILE([
		#define __KERNEL__
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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

        dnl between 3.4 and 3.6 the encode_fh function in
        dnl "struct export_operations" stopped having a dentry pointer
        dnl and a connectable flag as arguments, intead encode_fh
        dnl has both the child and parent inodes as arguments.
	tmp_cflags=${CFLAGS}
	CFLAGS="${CFLAGS} -Werror"
        AC_MSG_CHECKING([if kernel export ops get inode from dentry])
        AC_TRY_COMPILE([
            #define __KERNEL__
            #ifdef HAVE_KCONFIG
            #include <linux/kconfig.h>
            #endif
            #include <linux/fs.h>
            #include <linux/exportfs.h>
            extern int myencode(struct dentry *,
                                __u32 *,
                                int *,
                                int);
            ], [
            static struct export_operations ex_op = {.encode_fh = myencode};
            ], [
            AC_MSG_RESULT(yes)
            AC_DEFINE(PVFS_ENCODE_FS_USES_DENTRY, 1,
               [Define if kernel export ops encode_fh has dentry arg])
            ], [
            AC_MSG_RESULT(no)
            ]
        )
	CFLAGS=$tmp_cflags

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
	    #ifdef HAVE_KCONFIG
	    #include <linux/kconfig.h>
	    #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
	    #ifdef HAVE_KCONFIG
	    #include <linux/kconfig.h>
	    #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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

	dnl by 3.11 the invalidatepage address_space_operation has three
	dnl has three arguments.
	AC_MSG_CHECKING(for three argument invalidatepage function)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
		], 
                [
			struct address_space_operations aso;

			struct page *p;
			unsigned int i,j;

			aso.invalidatepage(p,i,j);
		],
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_THREE_ARGUMENT_INVALIDATEPAGE, 1, Define if invalidatepage function has three arguments),
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
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
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
	    #ifdef HAVE_KCONFIG
	    #include <linux/kconfig.h>
	    #endif
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
	    #ifdef HAVE_KCONFIG
	    #include <linux/kconfig.h>
	    #endif
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
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
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

	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for two param permission)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
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
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
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
                #ifdef HAVE_KCONFIG
                #include <linux/kconfig.h>
                #endif
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
                #ifdef HAVE_KCONFIG
                #include <linux/kconfig.h>
                #endif
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
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
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

        dnl By 3.6, the kernel is focusing on the concept of "namespace",
        dnl and that has to be handled when peering at uids and gids.
        dnl We'll check for from_kuid and if we find it, we'll assume we
        dnl have to handle namespace whenever we mess with a kuid_t.
        tmp_cflags=$CFLAGS
        CFLAGS="$CFLAGS -Werror"
        AC_MSG_CHECKING(for from_kuid)
        AC_TRY_COMPILE([
                #define __KERNEL__
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
                #include <linux/sched.h>
                #include <linux/cred.h>
        ], [
                int uid = from_kuid(&init_user_ns, current_fsuid());
        ],
        AC_MSG_RESULT(yes)
        AC_DEFINE(HAVE_FROM_KUID, 1, [Define if from_kuid function is found]),
        AC_MSG_RESULT(no)
        )
        CFLAGS=$tmp_cflags

        dnl 2.6.32 added a mandatory name field to the bdi structure
        AC_MSG_CHECKING(if kernel backing_dev_info struct has a name field)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
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
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
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


	dnl 2.6.33 API change,
	dnl Removed .ctl_name from struct ctl_table.
        tmp_cflags=$CFLAGS
        CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING([whether struct ctl_table has ctl_name])
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/sysctl.h>
                static struct ctl_table c = { .ctl_name = 0, };
	],[ ],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_CTL_NAME, 1, Define if struct ctl_table has ctl_name member),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

	dnl Removed .strategy from struct ctl_table.
        tmp_cflags=$CFLAGS
        CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING([whether struct ctl_table has strategy])
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/sysctl.h>
                static struct ctl_table c = { .strategy = 0, };
	], [ ],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_STRATEGY_NAME, 1, Define if struct ctl_table has strategy member),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

	dnl 2.6.33 changed the parameter signature of xattr_handler get 
	dnl member functions to have a fifth argument and changed the first
	dnl parameter from struct inode to struct dentry. if the test fails
	dnl assume the old 4 param with struct inode
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for five-param xattr_handler.get)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
		#include <linux/dcache.h>
		#include <linux/xattr.h>
		static struct xattr_handler x;
		static int get_xattr_h( struct dentry *d, const char *n, 
					void *b, size_t s, int h)
		{ return 0; }
	], 
	[ 
	    x.get = get_xattr_h;
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_XATTR_HANDLER_GET_FIVE_PARAM, 1, [Define if kernel xattr_handle get function has dentry as first parameter and a fifth parameter]),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

	dnl 2.6.33 changed the parameter signature of xattr_handler set 
	dnl member functions to have a sixth argument and changed the first
	dnl parameter from struct inode to struct dentry. if the test fails
	dnl assume the old 5 param with struct inode
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for six-param xattr_handler.set)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
		#include <linux/dcache.h>
		#include <linux/xattr.h>
		static struct xattr_handler x;
		static int set_xattr_h( struct dentry *d, const char *n, 
					const void *b, size_t s, int f, int h)
		{ return 0; }
	], 
	[ 
	    x.set = set_xattr_h;
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_XATTR_HANDLER_SET_SIX_PARAM, 1, [Define if kernel xattr_handle set function has dentry as first parameter and a sixth parameter]),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

	dnl xattr_handler is also a const
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for const s_xattr member in super_block struct)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#ifdef HAVE_KCONFIG
		#include <linux/kconfig.h>
		#endif
		#include <linux/fs.h>
		#include <linux/xattr.h>
		struct super_block sb;
                const struct xattr_handler *x[] = { NULL };
	], 
	[ 
            sb.s_xattr = x;
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_CONST_S_XATTR_IN_SUPERBLOCK, 1, [Define if s_xattr member of super_block struct is const]),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

        dnl early 2.6 kernels do not contain true/false enum in stddef.h
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(stddef.h true/false enum)
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/stddef.h>
                int f = true;
	], 
	[ ],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_TRUE_FALSE_ENUM, 1, [Define if kernel stddef has true/false enum]),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags


	dnl fsync no longer has a dentry second parameter
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for dentry argument in fsync)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
		static struct file_operations f;
		static int local_fsync(struct file *f, struct dentry *d, int i)
		{ return 0; }
	], 
	[ 
	    f.fsync = local_fsync;
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_FSYNC_DENTRY_PARAM, 1, [Define if fsync function in file_operations struct wants a dentry pointer as the second parameter]),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

	dnl file_operations has unlocked_ioctl instead of ioctl as of 2.6.36
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for unlocked_ioctl in file_operations)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
		static struct file_operations f;
	], 
	[ 
	    f.unlocked_ioctl = NULL;
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_UNLOCKED_IOCTL_HANDLER, 1, [Define if file_operations struct has unlocked_ioctl member]),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

	dnl 2.6.36 removed inode_setattr with the other BKL removal changes
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for inode_setattr)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
                struct iattr *iattr;
                struct inode *inode;
                int ret;
	], 
	[ 
	        ret = inode_setattr(inode, iattr);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_INODE_SETATTR, 1, [Define if inode_setattr is defined]),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

        dnl dentry operations struct d_hash function has a different signature
        dnl in 2.6.38 and newer, second param is an inode
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for three-param dentry_operations.d_hash)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
                #include <linux/dcache.h>
                static struct dentry_operations d;
                static int d_hash_t(const struct dentry *d, 
                                    const struct inode *i, 
                                    struct qstr * q)
                { return 0; }
	], 
	[ 
                d.d_hash = d_hash_t;
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_THREE_PARAM_D_HASH, 1, [Define if d_hash member of dentry_operations has three params, the second inode paramsbeing the difference]),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

        dnl in 3.11 dentry operations struct d_hash function went back to just
        dnl having 2 parameters, similar to the way is was back in 2.6,
        dnl only the first parameter is "const struct dentry *" instead of
        dnl "struct dentry *"... 
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for two-param dentry_operations.d_hash with const)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
                #include <linux/dcache.h>
                static struct dentry_operations d;
                static int d_hash_t(const struct dentry *d, 
                                    struct qstr * q)
                { return 0; }
	], 
	[ 
                d.d_hash = d_hash_t;
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_TWO_PARAM_D_HASH_WITH_CONST, 1, [Define if d_hash member of dentry_operations has two params, where the first param is a const ]),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

        dnl dentry operations struct d_compare function has a different 
        dnl signature in 2.6.38 and newer, split out dentry/inodes, string and
        dnl qstr params
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for seven-param dentry_operations.d_compare)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
                #include <linux/dcache.h>
                static struct dentry_operations d;
                static int d_compare_t(const struct dentry *d1, 
                                       const struct inode *i1,
                                       const struct dentry *d2, 
                                       const struct inode *i2, 
                                       unsigned int len, 
                                       const char *str, 
                                       const struct qstr *qstr)
                { return 0; }
	], 
	[ 
                d.d_compare = d_compare_t;
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_SEVEN_PARAM_D_COMPARE, 1, [Define if d_compare member of dentry_operations has seven params]),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

        dnl dentry operations struct d_compare function has five parameters
        dnl in 3.11
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for five-param dentry_operations.d_compare)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
                #include <linux/dcache.h>
                static struct dentry_operations d;
                static int d_compare_t(const struct dentry *d1, 
                                       const struct dentry *d2, 
                                       unsigned int len, 
                                       const char *str, 
                                       const struct qstr *qstr)
                { return 0; }
	], 
	[ 
                d.d_compare = d_compare_t;
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_FIVE_PARAM_D_COMPARE, 1, [Define if d_compare member of dentry_operations has five params]),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

        dnl dentry operations struct d_delete argumentis constified in  
        dnl 2.6.38 and newer
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for constified dentry_operations.d_delete)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
                #include <linux/dcache.h>
                static struct dentry_operations d;
                static int d_delete_t(const struct dentry *d)
                { return 0; }
	], 
	[ 
                d.d_delete = d_delete_t;
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_D_DELETE_CONST, 1, [Define if d_delete member of dentry_operations has a const dentry param]),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

        dnl dentry member d_count is no longer atomic and has it's own spinlock
        dnl in 2.6.38 and newer
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for dentry.d_count atomic_t type)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
                #include <linux/dcache.h>
                struct dentry d;
                atomic_t x;
	], 
	[ 
                x = d.d_count;
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_DENTRY_D_COUNT_ATOMIC, 1, [Define if d_count member of dentry is of type atomic_t]),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

        dnl in 3.11 struct dentry no longer has a d_count member,
        dnl the ref count is in its own structure, and that structure
        dnl (struct lockref d_lockref) contains the ref count.
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for dentry with lockref struct)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
                #include <linux/dcache.h>
                struct dentry d;
                unsigned int x;
	], 
	[ 
                x = d.d_lockref.count
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_DENTRY_LOCKREF_STRUCT, 1, [Define if dentry struct has a lockref struct member]),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags


        dnl in 3.11 and up, a d_count() function is provided that returns the value
        dnl of d_lockref.count.  In SLES 11-SP3, the new structure is being used (struct lockref d_lockref)
        dnl but the function d_count() is not in their version of Linux (3.0.101-0.35-default), instead
        dnl there is a #define d_count d_lockref.count.
        tmp_cflags=$CFLAGS
        CFLAGS="$CFLAGS -Werror"
        AC_MSG_CHECKING(for function d_count)
        AC_TRY_COMPILE([
                #define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
                #include <linux/fs.h>
                #include <linux/dcache.h>
                struct dentry d;
                unsigned int x;
        ],
        [
                x = d_count(d);
        ],
        AC_MSG_RESULT(yes)
        AC_DEFINE(HAVE_D_COUNT_FUNCTION, 1, [define if d_count() is provided]),
        AC_MSG_RESULT(no)
        )
        CFLAGS=$tmp_cflags 

        dnl permission function pointer in the inode_operations struct now
        dnl takes three params with the third being an unsigned int (circa
        dnl 2.6.38
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for three-param inode_operations permission)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
                struct inode_operations i;
                int p(struct inode *i, int mode, unsigned int flags)
                { return 0; }
	], 
	[ 
            i.permission = p;
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_THREE_PARAM_PERMISSION_WITH_FLAG, 1, [Define if permission function pointer of inode_operations struct has three parameters and the third parameter is for flags (unsigned int)]),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

        dnl the acl_check parameter of the generic_permission function has a
        dnl third parameter circa 2.6.38
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for three-param acl_check of generic_permission)
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
                struct inode *i;
                int p(struct inode *i, int mode, unsigned int flags)
                { return 0; }
	], 
	[ 
            generic_permission(i, 0, 0, p);
	],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_THREE_PARAM_ACL_CHECK, 1, [Define if acl_check param of generic_permission function has three parameters]),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

        dnl SPIN_LOCK_UNLOCKED has gone away in 2.6.39 in lieu of 
        dnl DEFINE_SPINLOCK()
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for SPIN_LOCK_UNLOCKED )
	AC_TRY_COMPILE([
		#define __KERNEL__
		#include <linux/spinlock.h>
                spinlock_t test_lock = SPIN_LOCK_UNLOCKED;
                struct inode *i;
	], [ ],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_SPIN_LOCK_UNLOCKED, 1, [Define if SPIN_LOCK_UNLOCKED defined]),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

        dnl get_sb goes away in 2.6.39 for mount_X
	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for get_sb )
	AC_TRY_COMPILE([
		#define __KERNEL__
        #ifdef HAVE_KCONFIG
        #include <linux/kconfig.h>
        #endif
		#include <linux/fs.h>
                struct file_system_type f;
	], 
        [
                f.get_sb = NULL;
        ],
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_GET_SB_MEMBER_FILE_SYSTEM_TYPE, 1, [Define if get_sb is a member of file_system_type struct]),
	AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

	tmp_cflags=$CFLAGS
	CFLAGS="$CFLAGS -Werror"
	AC_MSG_CHECKING(for dirty_inode flag)
	AC_TRY_COMPILE([
                #define __KERNEL__
                #ifdef HAVE_KCONFIG
                #include <linux/kconfig.h>
                #endif
                #include <linux/fs.h>
                void di(struct inode *i, int f)
                {
                        return;
                }
                ],
                [
                        struct super_operations s;
                        s.dirty_inode = di;
		], 
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DIRTY_INODE_FLAGS, 1, Define if dirty_inode takes a flag argument ),
		AC_MSG_RESULT(no)
	)
        CFLAGS=$tmp_cflags

	CFLAGS=$oldcflags

])
