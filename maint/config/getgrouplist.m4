AC_DEFUN([AX_GETGROUPLIST],
[
	AC_MSG_CHECKING(for getgrouplist in distro)
	dnl if this test passes, the distro does have it
	dnl if this test fails, the distro does not have it

	AC_EGREP_HEADER(getgrouplist, grp.h, 
	AC_MSG_RESULT(yes) AC_DEFINE(HAVE_GETGROUPLIST, 1, Define if distro has getgrouplist), 
		AC_EGREP_HEADER(getgrouplist, unistd.h, 
		AC_MSG_RESULT(yes) AC_DEFINE(HAVE_GETGROUPLIST, 1, Define if distro has getgrouplist), 
		AC_MSG_RESULT(no),) 
	,)

])
