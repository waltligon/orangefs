#include "main-app.h"


extern MainApp *MAIN_APP;

OrangeFS_time Utils::winFiletimeToOrangeFS(const FILETIME &ft)
{
	ULARGE_INTEGER lInt;
	lInt.LowPart	= ft.dwLowDateTime;
	lInt.HighPart	= ft.dwHighDateTime;

	return (OrangeFS_time) lInt.QuadPart / 10000000ULL - 11644473600ULL;
}

/* function to take the file size in bytes and return
 * a string containing the new file size > 1024 with 
 * the associated storage size */
wxString Utils::getPrefixString(OrangeFS_size &size)
{
	const wxString prefixes[4] = { " KB" , " MB", " GB" , " TB" };
	wxString size_string = " bytes";
	wxString retString;
	short int count = 0;
	stringstream sizeStream;
	float newSize = (float) size;

	while (1)
	{
		if ((newSize / 1024.0) > 1.0)
		{
			size_string = prefixes[count];
			count++;
			newSize /= 1024;
		}
		else
		{
			break;
		}
	}

	sizeStream << newSize;
	retString += sizeStream.str();
	retString += size_string;

	return retString;
}

/* function to construct the Linux-style file permissions
 * string given an OrangeFS_permissions value */
wxString Utils::getPermString(const OrangeFS_permissions &perms, bool dir)	
{					
	OrangeFS_permissions tempPerms = perms;
	wxString retString;

	/* first, set the first character with the file type */
	if (dir)
	{
		retString += 'd';
	}
	else
	{
		retString += '-';
	}

	/* zero out the bits we're not interested in before each set */

	/* owner perms */
	tempPerms &= 0x000001ff;
	retString += (((tempPerms >> 6) & 4) == 4) ? 'r' : '-';
	retString += (((tempPerms >> 6) & 2) == 2) ? 'w' : '-';
	retString += (((tempPerms >> 6) & 1) == 1) ? 'x' : '-';

	/* group tempPerms */
	tempPerms &= 0x0000003f;
	retString += (((tempPerms >> 3) & 4) == 4) ? 'r' : '-';
	retString += (((tempPerms >> 3) & 2) == 2) ? 'w' : '-';
	retString += (((tempPerms >> 3) & 1) == 1) ? 'x' : '-';

	/* other perms */
	tempPerms &= 0x00000007;
	retString += ((tempPerms & 4) == 4) ? 'r' : '-';
	retString += ((tempPerms & 2) == 2) ? 'w' : '-';
	retString += ((tempPerms & 1) == 1) ? 'x' : '-';

	return retString;
}

wxString Utils::getTimeString(const OrangeFS_time &time)
{
	wxString retString;
	time_t binTime = time;

	retString = ctime(&binTime);
	return retString;
}

void Utils::orangefsTimeToWindowsTime(OrangeFS_time t, LPFILETIME pft)
   {
     // Note that LONGLONG is a 64-bit value
     LONGLONG ll;

     ll = Int32x32To64(t, 10000000) + 116444736000000000;
     pft->dwLowDateTime = (DWORD)ll;
     pft->dwHighDateTime = ll >> 32;
}

void Utils::setWinFileAttrs(OrangeFS_attr &attrs, HANDLE file)
{
	FILETIME createTime = { 0 };
	FILETIME modifiedTime = { 0 };
	FILETIME accessedTime = { 0 };
	SYSTEMTIME accessTime = { 0 };
	FileListHandler *listHandler = FileListHandler::getInstance();

	GetLocalTime(&accessTime);
	SystemTimeToFileTime(&accessTime, &accessedTime);

	/* go through all the attributes from the server and set accordingly for the local windows copy */
	/* first, set the appropriate file times */
	/* we will copy over the creat time and modified time, but update the access time */
	orangefsTimeToWindowsTime(attrs.ctime, &createTime);
	orangefsTimeToWindowsTime(attrs.mtime, &modifiedTime);
	if ( !SetFileTime(file, &createTime, &accessedTime, &modifiedTime) )
	{
		orangefs_debug_print("ERROR : call to SetFileTime failed...\n");
		MAIN_APP->printLastWinError();
		return;
	}

	/* We aren't interested in copying over the permissions, but we are interested in comparing
	 * the credentials for the current user and the owner of the file so we can determine if 
	 * the user can sync an updated copy back to the server if they have write permissions */
	// MetadataHandler *metadataHandler = MetadataHandler::getInstance();
	// wxString permString = listHandler->getPermString(metadataHandler->getFilePerms, metadataHandler->isDir(/* get file name from handle */));
}