AC_DEFUN([AX_GETGROUPLIST],
[

	AC_MSG_CHECKING(for getgrouplist in grp.h)
	AC_EGREP_HEADER(getgrouplist,
		grp.h, 
		AC_MSG_RESULT(yes) 
		AC_DEFINE(HAVE_GETGROUPLIST, 1, getgrouplist is found),
		AC_MSG_RESULT(no)
	,)

	AC_MSG_CHECKING(for getgrouplist in unistd.h)
	AC_EGREP_HEADER(getgrouplist,
		unistd.h, 
		AC_MSG_RESULT(yes) 
		AC_DEFINE(HAVE_GETGROUPLIST, 1, getgrouplist is found),
		AC_MSG_RESULT(no)
	,)

	AC_MSG_CHECKING(gid_t getgrouplist group parm in gid.h)
	AC_EGREP_HEADER(getgrouplist.*gid_t.*group,
		grp.h,
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GETGROUPLIST_GID, 1, getgrouplist uses gid_t),
		AC_MSG_RESULT(no)
	)

	AC_MSG_CHECKING(gid_t getgrouplist group parm in unistd.h)
	AC_EGREP_HEADER(getgrouplist.*gid_t.*group,
		unistd.h,
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GETGROUPLIST_GID, 1, getgrouplist uses gid_t),
		AC_MSG_RESULT(no)
	)

	AC_MSG_CHECKING(int getgrouplist group parm in gid.h)
	AC_EGREP_HEADER(getgrouplist.*int.*group,
		grp.h,
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GETGROUPLIST_INT, 1, getgrouplist uses gid_t),
		AC_MSG_RESULT(no)
	)

	AC_MSG_CHECKING(gid_t getgrouplist group parm in unistd.h)
	AC_EGREP_HEADER(getgrouplist.*int.*group,
		unistd.h,
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GETGROUPLIST_INT, 1, getgrouplist uses gid_t),
		AC_MSG_RESULT(no)
	)

])
