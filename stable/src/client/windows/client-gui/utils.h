#ifndef __UTILS_H
#define __UTILS_H

/* util functions for the app */
/* will be singleton class usable by the entire app */

#include "main-app.h"

class Utils
{
public:
	static Utils* getInstance() { if (!instance) { instance = new Utils(); } return instance; }

	~Utils() {}

	OrangeFS_time winFiletimeToOrangeFS(const FILETIME &ft);
	void orangefsTimeToWindowsTime(OrangeFS_time t, LPFILETIME pft);
	wxString getTimeString(const OrangeFS_time &time);
	wxString getPermString(const OrangeFS_permissions &perms, bool dir);
	wxString getPrefixString(OrangeFS_size &size);
	void setWinFileAttrs(OrangeFS_attr &attrs, HANDLE file);
private:
	Utils() {}

	static Utils *instance;	/* singleton instance */
};

#endif