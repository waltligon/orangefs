
#ifndef __FILELISTHANDLER_H
#define __FILELISTHANDLER_H

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/listbase.h>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include "main-app.h"

#ifdef WIN32
	#include <atltime.h>
#endif

#include <map>
using std::cout;
using std::endl;
using std::cerr;
using std::map;
using std::stringstream;

const wxWindowID LIST_CTRL_SYNC = 0x1;

class FileListHandler: public wxListCtrl
{
public:
	static FileListHandler* getInstance() { if (!instance) { instance = new FileListHandler; } return instance; }

	~FileListHandler();

	void addColumn(const wxString &name);
	void removeColumn(const wxString &name);

	void displayFileStatus(wxCommandEvent &event);
	void processListCtrlDoubleClick(wxCommandEvent &event);
	bool getSyncStatus(int index) const;

private:
	FileListHandler();
	FileListHandler(const FileListHandler &) { }	/* copy constructor */
	FileListHandler& operator=(const FileListHandler &) { }	/* overloaded assignment operator */

	static FileListHandler *instance;	/* singleton instance */
	wxListCtrl *syncList;
	wxPanel *syncPane;
	map<wxString, int> syncColumnIDs;
	wxString localStorePath;	/* local file path to store synced files set in the settings dialog */
};

#endif