#ifndef __MAIN_APP_H
#define __MAIN_APP_H

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/listbase.h>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <orangefs-client.h>
#include <map>
using std::cout;
using std::endl;
using std::cerr;
using std::map;
using std::stringstream;

const int MAX_FILES		= 256;
const int MAX_MNTENTS	= 256;
const OrangeFS_ds_position ORANGEFS_READDIR_START = INT32_MAX-1;
const char *TABFILE = "\\orangefstab";
const wxWindowID LIST_CTRL = 0x01;

enum
{
	ID_FILE_QUIT = 1,
	ID_FILE_ABOUT = 2,
	ID_VIEW_PERMISSIONS = 3,
	ID_VIEW_SIZE = 4,
	ID_VIEW_MODIFIED = 5
}ID;

class MainApp: public wxApp
{
public:
	virtual bool OnInit();
	void initFileSystem();
	void allocateMembers();
	void cleanupApp();
	short int getNumFiles() const { return num_files; }
	OrangeFS_size getFileSize(int index) const { return fileSize[index]; }
	wxString getFileName(int index) const { return fileListing[index]; }

private:
	wxArrayString fileListing;
	OrangeFS_credential *rootCred;
	OrangeFS_attr *rootSysAttr;
	OrangeFS_mntent **mntents;
	OrangeFS_size *fileSize;
	short int num_files;
};

class MainFrame: public wxFrame
{
public:
	MainFrame(const wxString &title, const wxPoint &pos, const wxSize &size);
	
	wxListCtrl *fileListCtrl;
	wxMenuBar *mainMenuBar;
	wxMenu *file;
	wxMenu *view;
	wxPanel *mainPane;
	map<wxString, int> columnIDs;
	
	void onQuit(wxCommandEvent &event);
	void onAbout(wxCommandEvent &event);
	void showPermissions(wxCommandEvent &event);
	void showFileSize(wxCommandEvent &event);
	void showLastModified(wxCommandEvent &event);
	void processListCtrlDoubleClick(wxCommandEvent &event);
	void addColumn(const wxString &name);
	void removeColumn(const wxString &name);
	wxString getPrefixString(OrangeFS_size &size);

	DECLARE_EVENT_TABLE()
};

BEGIN_EVENT_TABLE(MainFrame, wxFrame)
	EVT_MENU(ID_FILE_QUIT, MainFrame::onQuit)
	EVT_MENU(ID_FILE_ABOUT, MainFrame::onAbout)
	EVT_MENU(ID_VIEW_PERMISSIONS, MainFrame::showPermissions)
	EVT_MENU(ID_VIEW_SIZE, MainFrame::showFileSize)
	EVT_MENU(ID_VIEW_MODIFIED, MainFrame::showLastModified)
	EVT_LISTBOX_DCLICK( LIST_CTRL, MainFrame::processListCtrlDoubleClick)
END_EVENT_TABLE()

#endif